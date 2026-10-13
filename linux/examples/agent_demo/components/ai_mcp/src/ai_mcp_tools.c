/**
 * @file ai_mcp_tools.c
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "tal_api.h"

#include "tuya_ai_agent.h"
#include "tuya_iot.h"

#include "ai_mcp_server.h"

#include "ai_mcp.h"
/***********************************************************
************************macro define************************
***********************************************************/
/* 构建系统注入的工程信息兜底（与 examples/agent_demo/src/tuya_main.c 同风格）：
 * PROJECT_NAME/PROJECT_VERSION 由宿主构建注入（esp-idf 注入 PROJECT_NAME），
 * 普通 CMake 宿主不注入时用下面的默认值。 */
#ifndef PROJECT_NAME
#define PROJECT_NAME "agent_demo"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/


/***********************************************************
***********************function define**********************
***********************************************************/
static OPERATE_RET __get_device_info(const MCP_PROPERTY_LIST_T *properties, MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    cJSON             *json   = NULL;
    tuya_iot_client_t *client = NULL;
    const char        *devid  = NULL;

    json = cJSON_CreateObject();
    if (!json) {
        PR_ERR("Create JSON object failed");
        return OPRT_MALLOC_FAILED;
    }

    /* serialNumber 取 IoT 客户端的真实设备 ID（激活后由云端下发并落 KV，重启后回填）；
     * 本工具只能在 MQTT 连上后（MCP cb 已注册）被调用，故正常路径 devid 非空。
     * 未激活/未初始化（client 为空）时退化为 "unknown"。 */
    client = tuya_iot_client_get();
    if (client) {
        devid = tuya_iot_devid_get(client);
    }

    // Add device information
    cJSON_AddStringToObject(json, "model", PROJECT_NAME);
    cJSON_AddStringToObject(json, "serialNumber", (devid && devid[0] != '\0') ? devid : "unknown");
    cJSON_AddStringToObject(json, "firmwareVersion", PROJECT_VERSION);

    // Set return value
    ai_mcp_return_value_set_json(ret_val, json);

    return OPRT_OK;
}

static OPERATE_RET __ai_mcp_tools_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    // device info get tool
    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "device_info_get",
        "Get device information such as model, serial number, and firmware version.",
        __get_device_info,
        NULL
    ), err);

    /* 已移除的工具（保证云端工具列表与实际能力一致，不再暴露"调用必失败"的占位工具）：
     * - device_camera_take_photo：依赖 ai_video（未移植到本仓）
     * - device_audio_volume_set：依赖 ai_audio（未移植到本仓）
     * - device_audio_mode_set  ：依赖 ai_mode（未移植到本仓）
     * 上游带宏门控的完整实现保留在 esp32 快照
     * （esp32/examples/iot_demo/components/tuyaopen-core/agent-core/ai_mcp/src/ai_mcp_tools.c），
     * 将来移植对应组件后，从那里取回实现并在此恢复注册即可。 */

    return OPRT_OK;

err:
    // destroy MCP server on failure
    ai_mcp_server_destroy();

    return rt;
}

static OPERATE_RET __ai_mcp_init(void *data)
{
    OPERATE_RET rt = OPRT_OK;

    // FIXME: Set actual MCP server name and mcp version
    TUYA_CALL_ERR_RETURN(ai_mcp_server_init("Tuya MCP Server", "1.0"));

    TUYA_CALL_ERR_RETURN(__ai_mcp_tools_register());

    PR_DEBUG("MCP Server initialized successfully");

    return rt;
}

OPERATE_RET ai_mcp_init(void)
{
    return tal_event_subscribe(EVENT_MQTT_CONNECTED, "ai_mcp_init", __ai_mcp_init, SUBSCRIBE_TYPE_ONETIME);
}

OPERATE_RET ai_mcp_deinit(void)
{
    ai_mcp_server_destroy();

    PR_DEBUG("MCP Server deinitialized successfully");

    return OPRT_OK;
}