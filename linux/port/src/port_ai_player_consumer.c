/**
 * @file port_ai_player_consumer.c
 * @brief AI 播放器平台消费端实现（Linux ALSA）：把解码 PCM 写进声卡
 *
 * 契约：agent-core/audio_player 的 AI_PLAYER_CONSUMER_T（六回调），经
 * tuya_ai_player_set_consumer() 注入（port 侧入口 port_ai_player_consumer_install）。
 *
 * 与上游 consumer_speaker 的差异（有意）：
 *   - 上游依赖平台音频服务层（tkl_audio）且设备随输入路径打开/关闭；本实现
 *     自持设备生命周期——open 打开、close 关闭，与输入路径完全解耦；
 *   - 输出固定 16k/16bit/mono（service_init 的配置约定；consumer 的 start
 *     不带格式参数，故设备在 open 时按此格式配置）。
 *
 * 各回调语义（对齐 svc_ai_player.c 的调用点）：
 *   open   —— service_init 装载默认消费端后由 set_consumer 替换时调用（本实现
 *             打开 ALSA 设备并完成 hw_params/prepare）；设备打不开时降级为
 *             "丢弃输出"，不阻断播放链路（write 直接返回错误，svc 忽略返回值）
 *   start  —— 首个 player 进入 PLAYING 时调用（__switch_player_mode）；设备
 *             prepare（上次 stop 的 drain 之后设备回 SETUP，须重新 prepare）
 *   write  —— 播放线程写 PCM；阻塞写 + 循环写满（svc 忽略返回值，短写/失败
 *             必须在本函数内消化）；xrun(-EPIPE)/挂起(-ESTRPIPE) 经
 *             snd_pcm_recover 恢复后重试
 *   stop   —— player 全部停止时调用；snd_pcm_drain 播完设备缓冲（TTS 尾音
 *             不截断），失败忽略（start 会 prepare 恢复）
 *   close  —— 服务退出/set_consumer 替换时调用；drop + close（不等缓冲）
 *   set_volume —— 全局音量（0-100）。无硬件混音器：软件增益（16bit 样本乘
 *             volume/100 定点），volume=0 写零样本（保持设备时序，而非快进丢弃）。
 *             上游应用层还有 per-player 数字音量与混音闪避增益，与此正交叠加。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#if defined(ENABLE_AI_PLAYER)

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <string.h>

#include "tal_log.h"
#include "svc_ai_player.h" /* AI_PLAYER_CONSUMER_T / PLAYER_CONSUMER_HANDLE / TAL_AUDIO_* */
#include "port_ai_player_consumer.h"

/* ---------------------------------------------------------------------------
 * 输出格式：与 service_init 的约定一致（上游固定 16k/16bit/mono）。
 * 镜像类型（ai_player_types.h 的 TAL_AUDIO_*）逐值断言：core 侧镜像一旦
 * 漂移（采样率/位宽/声道取值变化），这里编译失败而不是静默按错误格式播放。
 * --------------------------------------------------------------------------- */
#define TKL_AI_PLAYER_SAMPLE_RATE 16000
#define TKL_AI_PLAYER_CHANNELS    1
#define TKL_AI_PLAYER_FRAME_BYTES 2 /* 16bit × 1ch */

typedef char _ai_player_assert_sample[(TAL_AUDIO_SAMPLE_16K == TKL_AI_PLAYER_SAMPLE_RATE) ? 1 : -1];
typedef char _ai_player_assert_databits[(TAL_AUDIO_DATABITS_16 == 16) ? 1 : -1];
typedef char _ai_player_assert_channel[(TAL_AUDIO_CHANNEL_MONO == TKL_AI_PLAYER_CHANNELS) ? 1 : -1];

/* ALSA 设备名：默认走 "default"（经 PulseAudio/dmix 等插件自动重采样，
 * 普通桌面环境开箱可用）；无音频服务/直连硬件的场景可用 -DTKL_AI_PLAYER_PCM_DEVICE='"plughw:0,0"' 覆盖 */
#ifndef TKL_AI_PLAYER_PCM_DEVICE
#define TKL_AI_PLAYER_PCM_DEVICE "default"
#endif

/* 设备缓冲：20ms period × 4 ≈ 80ms 积压（阻塞写的延迟上限；drain 的等待上限） */
#define TKL_AI_PLAYER_PERIOD_FRAMES (TKL_AI_PLAYER_SAMPLE_RATE / 50)
#define TKL_AI_PLAYER_PERIODS       (4)

/* 软件增益分块缓冲：write 的 len 上限为解码缓冲（AI_PLAYER_DECODEBUF_SIZE，
 * 默认 8KB），4KB 分块足够；仅在 volume ∈ (0,100) 时使用 */
#define TKL_AI_PLAYER_GAIN_BUF_SIZE (4096)

typedef struct {
    snd_pcm_t      *pcm;    /* NULL = 未打开（无声卡/已 close）：write 降级丢弃 */
    uint32_t        volume; /* 0-100，默认 100（满）；0 = 静音（写零样本） */
    pthread_mutex_t lock;   /* 保护 pcm 生命周期：write/stop 在播放线程，close/open
                             * 可能在 set_consumer（主线程）——见 svc_ai_player.c */
    uint8_t         gain_buf[TKL_AI_PLAYER_GAIN_BUF_SIZE];
    /* 静音专用零缓冲：**没有任何代码路径写入它**，全零由静态初始化保证——
     * 静音不能复用 gain_buf（增益路径用过的 gain_buf 里残留上次的音频样本，
     * 直接复用会把旧样本当"零"播出去） */
    uint8_t         zero_buf[TKL_AI_PLAYER_GAIN_BUF_SIZE];
} TKL_AI_PLAYER_CONSUMER_CTX_T;

/* 单例：svc 全局只挂一个消费端（tuya_ai_player_set_consumer 也是全局替换）。
 * 结构体常驻（不随 close 释放），close 后再 write 只会看到 pcm==NULL 而安全返回。 */
static TKL_AI_PLAYER_CONSUMER_CTX_T s_ai_player_consumer = {
    .pcm    = NULL,
    .volume = 100,
    .lock   = PTHREAD_MUTEX_INITIALIZER,
};

/* ---------------------------------------------------------------------------
 * 打开/关闭
 * --------------------------------------------------------------------------- */

static OPERATE_RET __ai_player_consumer_open(PLAYER_CONSUMER_HANDLE *handle)
{
    snd_pcm_t          *pcm    = NULL;
    snd_pcm_hw_params_t *params = NULL;
    unsigned int        rate    = TKL_AI_PLAYER_SAMPLE_RATE;
    unsigned int        periods = TKL_AI_PLAYER_PERIODS;
    snd_pcm_uframes_t   period  = TKL_AI_PLAYER_PERIOD_FRAMES;
    int                 err     = 0;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) { /* 幂等：已打开则复用（防重复 open 泄漏） */
        if (handle) {
            *handle = (PLAYER_CONSUMER_HANDLE)&s_ai_player_consumer;
        }
        pthread_mutex_unlock(&s_ai_player_consumer.lock);
        return OPRT_OK;
    }

    err = snd_pcm_open(&pcm, TKL_AI_PLAYER_PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        PR_ERR("ai player consumer: snd_pcm_open(%s) failed: %s", TKL_AI_PLAYER_PCM_DEVICE, snd_strerror(err));
        goto __fail;
    }

    /* hw_params 全流程：交错访问 / S16_LE / 单声道 / 16k（near 让插件层协商） */
    snd_pcm_hw_params_alloca(&params);
    if ((err = snd_pcm_hw_params_any(pcm, params)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_channels(pcm, params, TKL_AI_PLAYER_CHANNELS)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, 0)) < 0) {
        goto __fail;
    }
    if (rate != TKL_AI_PLAYER_SAMPLE_RATE) {
        /* default 设备的 plug 层会做重采样，协商值不同不致命；如实记录便于排查 */
        PR_WARN("ai player consumer: rate negotiated to %u (expected %d)", rate, TKL_AI_PLAYER_SAMPLE_RATE);
    }
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm, params, &period, 0)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params_set_periods_near(pcm, params, &periods, 0)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_hw_params(pcm, params)) < 0) {
        goto __fail;
    }
    if ((err = snd_pcm_prepare(pcm)) < 0) {
        goto __fail;
    }

    s_ai_player_consumer.pcm = pcm;
    if (handle) {
        *handle = (PLAYER_CONSUMER_HANDLE)&s_ai_player_consumer;
    }
    pthread_mutex_unlock(&s_ai_player_consumer.lock);

    PR_NOTICE("ai player consumer: ALSA %s opened (%d Hz, 16 bit, mono, period=%lu x %u)",
              TKL_AI_PLAYER_PCM_DEVICE, TKL_AI_PLAYER_SAMPLE_RATE, (unsigned long)period, periods);
    return OPRT_OK;

__fail:
    if (pcm) {
        snd_pcm_close(pcm);
    }
    if (handle) {
        *handle = NULL;
    }
    pthread_mutex_unlock(&s_ai_player_consumer.lock);

    /* 降级而非致命：播放链路（解码/重采样/混音）照常，只是没有声音输出 */
    PR_ERR("ai player consumer: ALSA open failed: %s (audio output disabled)", snd_strerror(err));
    return OPRT_COM_ERROR;
}

static OPERATE_RET __ai_player_consumer_close(PLAYER_CONSUMER_HANDLE handle)
{
    (void)handle;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) {
        /* drop：不等设备缓冲播完（close 用于 set_consumer 替换/服务退出，
         * 立即停止；"播完尾音"的语义在 stop 的 drain 里） */
        snd_pcm_drop(s_ai_player_consumer.pcm);
        snd_pcm_close(s_ai_player_consumer.pcm);
        s_ai_player_consumer.pcm = NULL;
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * 播放控制
 * --------------------------------------------------------------------------- */

static OPERATE_RET __ai_player_consumer_start(PLAYER_CONSUMER_HANDLE handle)
{
    int err = 0;

    (void)handle;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) {
        /* open 时已 prepare；上次 stop 的 drain 之后设备回 SETUP 状态，
         * 重播前重新 prepare（对已 PREPARED 的设备是幂等操作） */
        err = snd_pcm_prepare(s_ai_player_consumer.pcm);
        if (err < 0) {
            PR_ERR("ai player consumer: snd_pcm_prepare failed: %s", snd_strerror(err));
        }
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return (err < 0) ? OPRT_COM_ERROR : OPRT_OK;
}

/**
 * @brief 阻塞写满 len 字节（循环消化短写；xrun/挂起经 recover 恢复重试）
 *
 * 调用方（svc_ai_player.c）忽略 write 返回值，故短写必须在这里循环写满；
 * len 非帧大小整数倍时尾部不足一帧的字节被丢弃（mono/16bit 帧 = 2 字节，
 * 上游写入的 samples*2 恒为整数帧，不会触发）。
 */
static OPERATE_RET __ai_player_consumer_write_all(const uint8_t *data, uint32_t len)
{
    uint32_t frames = len / TKL_AI_PLAYER_FRAME_BYTES;
    uint32_t done   = 0;

    while (done < frames) {
        snd_pcm_sframes_t n = snd_pcm_writei(s_ai_player_consumer.pcm, data + done * TKL_AI_PLAYER_FRAME_BYTES,
                                             frames - done);
        if (n < 0) {
            /* -EPIPE: xrun（欠载）；-ESTRPIPE: 设备挂起；-EINTR: 信号打断。
             * snd_pcm_recover 对三者分别 prepare/resume/重试，成功后继续写 */
            int rc = snd_pcm_recover(s_ai_player_consumer.pcm, (int)n, 1);
            if (rc < 0) {
                PR_ERR("ai player consumer: pcm write error: %s (recover failed: %s)", snd_strerror((int)n),
                       snd_strerror(rc));
                return OPRT_COM_ERROR;
            }
            PR_WARN("ai player consumer: pcm write recovered: %s", snd_strerror((int)n));
            continue;
        }
        done += (uint32_t)n;
    }

    return OPRT_OK;
}

/**
 * @brief 软件增益：16bit 样本 × volume/100（定点 q = volume*256/100）
 *
 * 目标平台（Linux x86/arm64）为小端，与 S16_LE 的样本字节序一致，直接按
 * int16_t 处理；q ≤ 256 时 32767×q>>8 ≤ 32767，无溢出。
 */
static void __ai_player_consumer_apply_gain(uint8_t *dst, const uint8_t *src, uint32_t len, uint32_t volume)
{
    uint32_t       q = volume * 256 / 100;
    const int16_t *in = (const int16_t *)src;
    int16_t       *out = (int16_t *)dst;
    uint32_t       i   = 0;

    for (i = 0; i < len / sizeof(int16_t); i++) {
        out[i] = (int16_t)(((int32_t)in[i] * (int32_t)q) >> 8);
    }
}

static OPERATE_RET __ai_player_consumer_write(PLAYER_CONSUMER_HANDLE handle, const void *buf, uint32_t len)
{
    OPERATE_RET rt = OPRT_OK;

    (void)handle;

    if (!buf || !len) {
        return OPRT_INVALID_PARM;
    }

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (!s_ai_player_consumer.pcm) {
        pthread_mutex_unlock(&s_ai_player_consumer.lock);
        return OPRT_COM_ERROR; /* 设备未打开：丢弃（svc 忽略返回值，链路不受影响） */
    }

    if (0 == s_ai_player_consumer.volume) {
        /* 静音：写零样本（保持设备时序与播放时长，而非丢弃数据快进）。
         * 用专用的 zero_buf（静态全零、无人写入），不复用 gain_buf */
        uint32_t remain = len;
        while (remain) {
            uint32_t chunk = (remain > sizeof(s_ai_player_consumer.zero_buf)) ? sizeof(s_ai_player_consumer.zero_buf)
                                                                            : remain;
            rt = __ai_player_consumer_write_all(s_ai_player_consumer.zero_buf, chunk);
            if (OPRT_OK != rt) {
                break;
            }
            remain -= chunk;
        }
    } else if (s_ai_player_consumer.volume < 100) {
        /* 中间音量：分块乘增益后写（不改调用方 const buf） */
        const uint8_t *p      = (const uint8_t *)buf;
        uint32_t       remain = len;

        while (remain) {
            uint32_t chunk = (remain > sizeof(s_ai_player_consumer.gain_buf)) ? sizeof(s_ai_player_consumer.gain_buf)
                                                                             : remain;
            __ai_player_consumer_apply_gain(s_ai_player_consumer.gain_buf, p, chunk, s_ai_player_consumer.volume);
            rt = __ai_player_consumer_write_all(s_ai_player_consumer.gain_buf, chunk);
            if (OPRT_OK != rt) {
                break;
            }
            p += chunk;
            remain -= chunk;
        }
    } else {
        /* 满音量（默认）：直通，零拷贝 */
        rt = __ai_player_consumer_write_all((const uint8_t *)buf, len);
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return rt;
}

static OPERATE_RET __ai_player_consumer_stop(PLAYER_CONSUMER_HANDLE handle)
{
    int err = 0;

    (void)handle;

    pthread_mutex_lock(&s_ai_player_consumer.lock);

    if (s_ai_player_consumer.pcm) {
        /* drain：把设备缓冲里已写入的数据播完再停（TTS 尾音不截断）。
         * 失败（如 xrun 后的状态）忽略——下次 start 的 prepare 会恢复 */
        err = snd_pcm_drain(s_ai_player_consumer.pcm);
        if (err < 0) {
            PR_WARN("ai player consumer: snd_pcm_drain failed: %s", snd_strerror(err));
        }
    }

    pthread_mutex_unlock(&s_ai_player_consumer.lock);
    return OPRT_OK;
}

static OPERATE_RET __ai_player_consumer_set_volume(PLAYER_CONSUMER_HANDLE handle, uint32_t volume)
{
    (void)handle;

    if (volume > 100) {
        volume = 100;
    }

    pthread_mutex_lock(&s_ai_player_consumer.lock);
    s_ai_player_consumer.volume = volume;
    pthread_mutex_unlock(&s_ai_player_consumer.lock);

    PR_DEBUG("ai player consumer: volume set to %u", volume);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * 注入入口
 * --------------------------------------------------------------------------- */

AI_PLAYER_CONSUMER_T g_ai_player_consumer = {
    .open       = __ai_player_consumer_open,
    .close      = __ai_player_consumer_close,
    .start      = __ai_player_consumer_start,
    .write      = __ai_player_consumer_write,
    .stop       = __ai_player_consumer_stop,
    .set_volume = __ai_player_consumer_set_volume,
};

OPERATE_RET port_ai_player_consumer_install(void)
{
    /* 必须在 tuya_ai_player_service_init() 之后调用：service_init 会装载默认
     * 空消费端并覆盖 consumer，此处 set_consumer 才是最终替换点（见 svc_ai_player.c） */
    return tuya_ai_player_set_consumer(&g_ai_player_consumer);
}

#endif /* ENABLE_AI_PLAYER */
