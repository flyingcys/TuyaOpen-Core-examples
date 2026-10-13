/**
 * @file consumer_default.c
 * @brief audio_player 默认消费端（AI_PLAYER_CONSUMER_T）：core 侧空实现
 *
 * 上游的默认消费端是 consumer_speaker.c（依赖平台音频服务层，本仓不存在，
 * 且属平台相关实现），本仓不移植。core 侧改为**空实现占位**：
 *
 * - 行为等价于上游在"平台无音频编解码服务"时的分支（ENABLE_AUDIO_CODECS
 *   未开时 consumer_speaker 的六个回调同样全部返回 OPRT_OK 且不发声）；
 * - 平台侧（port）必须实现自己的 AI_PLAYER_CONSUMER_T（把 PCM 写进平台
 *   音频输出）并在初始化后经 tuya_ai_player_set_consumer() 注入；
 * - tuya_ai_player_service_init() 会先装载本默认实例，故未注入时播放器
 *   可正常走完 解包→解码→混音/重采样 全链路，只是最终 PCM 被丢弃——
 *   便于无音频硬件时自测链路，也避免 core 依赖平台音频服务。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_log.h"
#include "svc_ai_player.h"

static OPERATE_RET consumer_default_open(PLAYER_CONSUMER_HANDLE *handle)
{
    if (handle) {
        *handle = NULL;
    }

    PR_DEBUG("ai player default consumer: no audio output, "
             "inject a platform consumer via tuya_ai_player_set_consumer()");

    return OPRT_OK;
}

static OPERATE_RET consumer_default_close(PLAYER_CONSUMER_HANDLE handle)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_start(PLAYER_CONSUMER_HANDLE handle)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_write(PLAYER_CONSUMER_HANDLE handle, const void *buf, uint32_t len)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_stop(PLAYER_CONSUMER_HANDLE handle)
{
    return OPRT_OK;
}

static OPERATE_RET consumer_default_set_volume(PLAYER_CONSUMER_HANDLE handle, uint32_t volume)
{
    return OPRT_OK;
}

AI_PLAYER_CONSUMER_T g_consumer_default = {
    .open = consumer_default_open,
    .close = consumer_default_close,
    .start = consumer_default_start,
    .write = consumer_default_write,
    .stop = consumer_default_stop,
    .set_volume = consumer_default_set_volume
};
