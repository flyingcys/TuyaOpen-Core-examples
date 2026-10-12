/**
 * @file tal_vad.c
 * @brief tal_vad 平台分发：四接口透传到 port 组装的 VAD 卡
 *
 * 原为 AI 音频输入直调 tkl_vad_*（上游 TKL 平台接口），2026-10 解耦为经卡
 * 分发（tal_vad_ops_platform，port 组装）。本文件与 tal_kv.c / tal_audio_enc.c
 * 同构：只做分发，不持有任何 VAD 逻辑。
 *
 * 无 VAD 能力的平台（port 卡成员为 NULL）按"优雅降级"处理：init/start/stop
 * 返回 OPRT_NOT_SUPPORTED（调用方 TUYA_CALL_ERR_* 即按不支持处理），
 * get_status 返回中性值"无语音"——调用方据此保持非语音态，不会误报唤醒。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_vad.h"
#include "tal_vad_ops.h"
#include "tal_log.h"

/* 平台 ops 卡（port 组装；无 VAD 能力的平台成员为 NULL） */
extern TAL_VAD_OPS_T tal_vad_ops_platform;

TAL_VAD_OPS_T *tal_vad_get_ops(void)
{
    return &tal_vad_ops_platform;
}

#define TAL_VAD_EXEC_OP(op_name, ...)                                                                                  \
    do {                                                                                                               \
        TAL_VAD_OPS_T *ops = tal_vad_get_ops();                                                                        \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("TAL vad operation %s not available", #op_name);                                                        \
        return OPRT_NOT_SUPPORTED;                                                                                     \
    } while (0)

OPERATE_RET tal_vad_init(const TAL_VAD_CONFIG_T *cfg)
{
    if (NULL == cfg) {
        return OPRT_INVALID_PARM;
    }

    TAL_VAD_EXEC_OP(init, cfg);
}

OPERATE_RET tal_vad_start(void)
{
    TAL_VAD_EXEC_OP(start);
}

OPERATE_RET tal_vad_stop(void)
{
    TAL_VAD_EXEC_OP(stop);
}

TAL_VAD_STATUS_E tal_vad_get_status(void)
{
    TAL_VAD_OPS_T *ops = tal_vad_get_ops();
    if (NULL != ops && NULL != ops->get_status) {
        return ops->get_status();
    }

    /* 卡不可用：返回中性值"无语音"（空闲），调用方（ai_audio_input 的记录任务）
     * 据此保持非语音态，不会误触发唤醒；本接口被周期调用，故不打日志 */
    return TAL_VAD_STATUS_NONE;
}
