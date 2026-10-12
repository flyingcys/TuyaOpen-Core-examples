/**
 * @file ai_user_event.h
 * @brief AI user event notification module header
 *
 * This header file defines the types and functions for AI user event
 * notifications. It provides an event-driven mechanism for components
 * to communicate AI-related events such as ASR results, TTS playback,
 * emotion detection, and skill execution.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __AI_USER_EVENT_H__
#define __AI_USER_EVENT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
/* 事件编号保持与历史一致：零引用的事件值已删除，保留项全部显式赋值钉住编号，
 * 后续新增事件请追加新值，不要复用已删除项的编号。 */
typedef enum {
    AI_USER_EVT_ASR_EMPTY = 0,
    AI_USER_EVT_ASR_OK = 1,
    AI_USER_EVT_TEXT_STREAM_START = 11,
    AI_USER_EVT_TEXT_STREAM_DATA = 12,
    AI_USER_EVT_TEXT_STREAM_STOP = 13,
    AI_USER_EVT_EMOTION = 15,
    AI_USER_EVT_SKILL = 17,
    AI_USER_EVT_CHAT_BREAK = 18,
    AI_USER_EVT_SERVER_VAD = 19,
    AI_USER_EVT_PLAY_ALERT = 32,
    AI_USER_EVT_EXIT = 38,
} AI_USER_EVT_TYPE_E;

typedef struct {
    AI_USER_EVT_TYPE_E type;
    void              *data;
} AI_NOTIFY_EVENT_T;

typedef void (*AI_USER_EVENT_NOTIFY)(AI_NOTIFY_EVENT_T *event);

typedef struct {
    char    *data;
    uint16_t datalen;
    uint32_t timeindex;
} AI_NOTIFY_TEXT_T;

/* 情绪负载类型。原定义在 ai_skills/skill_emotion.h；本类型随事件负载经
 * ai_user_event 暴露，而 utility 不依赖 ai_skills，故定义上移至此处，
 * skill_emotion.h 反向包含本头文件。 */
typedef struct {
    const char  *emoji;
    const char  *name;
} AI_AGENT_EMO_T;

typedef struct {
    uint8_t *data;
    uint32_t data_len;
} AI_NOTIFY_MIC_DATA_T;

/***********************************************************
********************function declaration********************
***********************************************************/
/**
@brief Register a callback function for AI user event notifications
@param cb Callback function pointer to be called when events occur
@return None
*/
void ai_user_event_notify_register(AI_USER_EVENT_NOTIFY cb);

/**
@brief Notify registered callback about an AI user event
@param type Event type to notify
@param data Pointer to event data (can be NULL)
@return None
*/
void ai_user_event_notify(AI_USER_EVT_TYPE_E type, void *data);

#ifdef __cplusplus
}
#endif

#endif /* __AI_USER_EVENT_H__ */
