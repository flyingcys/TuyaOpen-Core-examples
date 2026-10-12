/**
 * @file tal_audio_enc.c
 * @brief tal_audio_enc 平台分发：单接口透传到 port 组装的 IPC 音频编码卡
 *
 * 原为 AI 服务直调 tkl_audio_enc_request（上游 TKL 平台接口），2026-10
 * 解耦为经卡分发（tal_audio_enc_ops_platform，port 组装）。本文件与
 * tal_kv.c 同构：只做分发，不持有任何编码逻辑。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_audio_enc.h"
#include "tal_audio_enc_ops.h"
#include "tal_log.h"

/* 平台 ops 卡（port 组装；无 IPC 音频编码能力的平台成员为 NULL） */
extern TAL_AUDIO_ENC_OPS_T tal_audio_enc_ops_platform;

TAL_AUDIO_ENC_OPS_T *tal_audio_enc_get_ops(void)
{
    return &tal_audio_enc_ops_platform;
}

OPERATE_RET tal_audio_enc_request(void *enc_para)
{
    if (NULL == enc_para) {
        return OPRT_INVALID_PARM;
    }

    TAL_AUDIO_ENC_OPS_T *ops = tal_audio_enc_get_ops();
    if (NULL != ops && NULL != ops->request) {
        return ops->request(enc_para);
    }

    PR_ERR("TAL audio enc operation %s not available", "request");
    return OPRT_NOT_SUPPORTED;
}
