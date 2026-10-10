/**
 * @file agent_downlink.c
 * @brief agent_demo 下行处理实现：打印 ASR/NLG 文字，忽略其余事件
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"

#include "ai_user_event.h"

#include "agent_downlink.h"

#include <stdio.h>

static void __agent_downlink_event(AI_NOTIFY_EVENT_T *event)
{
    if (!event) {
        return;
    }

    switch (event->type) {
    case AI_USER_EVT_ASR_OK: {
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data) {
            PR_NOTICE("[AGENT][ASR] %.*s", text->datalen, text->data);
        }
    } break;

    case AI_USER_EVT_ASR_EMPTY:
        PR_WARN("[AGENT][ASR] <empty>");
        break;

    case AI_USER_EVT_TEXT_STREAM_START: {
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        PR_NOTICE("[AGENT][NLG] >> %.*s", text ? text->datalen : 0, (text && text->data) ? text->data : "");
    } break;

    case AI_USER_EVT_TEXT_STREAM_DATA: {
        /* 流式片段：原样续打（不加前缀、不换行） */
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data && text->datalen > 0) {
            printf("%.*s", text->datalen, text->data);
            fflush(stdout);
        }
    } break;

    case AI_USER_EVT_TEXT_STREAM_STOP: {
        /* 多段流的末段（eof=1）经 STOP 携带该包 content（ai_skill.c:173-175，
         * 注释原文 "append final content (may be empty)"）——非空时必须续打，
         * 否则终端缺最后一段文字（单帧场景 STOP 为显式空包，不受影响）。
         * 先续打内容、再打 stream end（内容接在上一行末尾）。 */
        AI_NOTIFY_TEXT_T *text = (AI_NOTIFY_TEXT_T *)event->data;
        if (text && text->data && text->datalen > 0) {
            printf("%.*s", text->datalen, text->data);
            fflush(stdout);
        }
        PR_NOTICE("[AGENT][NLG] << (stream end)");
    } break;

    case AI_USER_EVT_CHAT_BREAK:
        PR_NOTICE("[AGENT] chat break (cloud interrupted)");
        break;

    case AI_USER_EVT_EXIT:
        PR_NOTICE("[AGENT] chat exit");
        break;

    default:
        /* 其余事件（TTS 播放控制、表情、技能等）本 demo 不处理 */
        break;
    }
}

OPERATE_RET agent_downlink_init(void)
{
    ai_user_event_notify_register(__agent_downlink_event);
    PR_NOTICE("[AGENT] downlink handler registered (text only, audio dropped)");

    return OPRT_OK;
}
