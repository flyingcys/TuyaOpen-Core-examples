/**
 * @file skill_cloudevent.c
 * @brief Cloud event skill implementation.
 *
 * This file provides functions for parsing and processing cloud events
 * (playTts / alert action dispatch).
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_api.h"

#include "skill_cloudevent.h"

/***********************************************************
************************macro define************************
***********************************************************/


/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/


/***********************************************************
***********************function define**********************
***********************************************************/
static bool __json_is_string(const cJSON *item)
{
    return item && cJSON_IsString(item) && item->valuestring != NULL;
}

static const char *__json_get_string(const cJSON *item)
{
    return __json_is_string(item) ? item->valuestring : NULL;
}

/**
 * @brief Parse and process cloud event from JSON.
 *
 * @param json JSON object containing cloud event data.
 * @return OPERATE_RET Operation result code.
 */
OPERATE_RET ai_parse_cloud_event(cJSON *json)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(json, OPRT_INVALID_PARM);

    cJSON *action = cJSON_GetObjectItem(json, "action");
    const char *action_str = __json_get_string(action);

    if (action != NULL && action_str == NULL) {
        PR_ERR("cloud event action type invalid");
        return OPRT_CJSON_GET_ERR;
    }

    if (action_str && (strcmp(action_str, "playTts") == 0 ||
                       strcmp(action_str, "alert") == 0)) {
        /* TTS/alert 播放依赖已移除的 ai_audio 组件：保留动作识别，暂不处理 */
        return rt;
    }

    return rt;
}
