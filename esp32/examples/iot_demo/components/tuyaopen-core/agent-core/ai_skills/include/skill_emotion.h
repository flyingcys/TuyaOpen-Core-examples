/**
 * @file skill_emotion.h
 * @brief Emotion skill module header
 *
 * This header file defines the types and functions for parsing and playing
 * emotion expressions from AI skill responses.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __SKILL_EMOTION_H__
#define __SKILL_EMOTION_H__

#include "tuya_cloud_types.h"
#include "cJSON.h"
#include "ai_user_event.h" /* AI_AGENT_EMO_T */

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define EMOJI_NEUTRAL      "NEUTRAL"
#define EMOJI_HAPPY        "HAPPY"
#define EMOJI_LAUGHING     "LAUGHING"
#define EMOJI_FUNNY        "FUNNY"
#define EMOJI_SAD          "SAD"
#define EMOJI_ANGRY        "ANGRY"
#define EMOJI_FEARFUL      "FEARFUL"
#define EMOJI_LOVING       "LOVING"
#define EMOJI_EMBARRASSED  "EMBARRASSED"
#define EMOJI_SURPRISE     "SURPRISE"
#define EMOJI_SHOCKED      "SHOCKED"
#define EMOJI_THINKING     "THINKING"
#define EMOJI_WINK         "WINK"
#define EMOJI_COOL         "COOL"
#define EMOJI_RELAXED      "RELAXED"
#define EMOJI_DELICIOUS    "DELICIOUS"
#define EMOJI_KISSY        "KISSY"
#define EMOJI_CONFIDENT    "CONFIDENT"
#define EMOJI_SLEEP        "SLEEP"
#define EMOJI_SILLY        "SILLY"
#define EMOJI_CONFUSED     "CONFUSED"


/***********************************************************
***********************typedef define***********************
***********************************************************/
/* AI_AGENT_EMO_T 定义已上移至 utility/ai_user_event.h（事件负载类型，避免
 * utility 反向依赖 ai_skills）。本头文件经 ai_user_event.h 传递该类型。 */


/***********************************************************
********************function declaration********************
***********************************************************/
/**
 * @brief Process emotion skill data from JSON.
 *
 * @param json JSON object containing emotion skill data.
 * @return OPERATE_RET Operation result code.
 */
OPERATE_RET ai_skill_emo_process(cJSON *json);

const char* ai_agent_emoji_get_name(const char* emoji);

/**
 * @brief Play emotion expression.
 *
 * @param emo Pointer to emotion structure.
 * @return OPERATE_RET Operation result code.
 */
OPERATE_RET ai_agent_play_emo(AI_AGENT_EMO_T *emo);


#ifdef __cplusplus
}
#endif

#endif /* __SKILL_EMOTION_H__ */
