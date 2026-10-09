/**
 * @file tuya_main.c
 * @brief p2p_demo 占位入口：只验证链接链（core + port + P2P 静态库 + qrcode）。
 *
 * 本文件是 Task 2 的骨架占位，Task 3 将整体替换为真实启动链
 * （tal 初始化 → tuya_iot_init（skill_param = gw_active_get_ext_param()）→
 * tuya_iot_start/yield → MQTT 连上后起线程 TUYA_APP_Start + tuya_ipc_demo_start，
 * 以及 TUYA_EVENT_RTC_REQ → gw_p2p_mqtt_data_cb 信令接入）。
 * 现在只做两件事：初始化日志；引用一个 P2P 符号确认静态库真的被链入。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "tal_api_ops.h"
#include "tuya_ipc_demo.h"
#include "tuya_p2p_sdk.h"

/**
 * @brief 日志输出回调：经 port 的 tkl_log_output 落 stdout
 *
 * @param str 日志字符串
 * @return void
 */
static void user_log_output_cb(const char *str)
{
    TAL_API_OPS_T *ops = tal_api_get_ops();
    if (NULL != ops && ops->log_output) {
        ops->log_output(str);
    }
}

/**
 * @brief 占位 main：日志初始化 + P2P 链接自证
 *
 * @return 0
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, user_log_output_cb);

    PR_NOTICE("p2p_demo skeleton, P2P linked: %s", gw_active_get_ext_param());

    return 0;
}
