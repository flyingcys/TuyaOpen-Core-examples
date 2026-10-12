/**
 * @file tkl_bluetooth_ops.c
 * @brief TAL BLE 卡（TAL_BLUETOOTH_OPS_T）——本 port 无该能力，空卡
 *
 * core 的 iot-core/ble/tal_bluetooth.c 经卡分发请求平台做 BLE（上游为 TKL
 * BLE 接口直调，2026-10 解耦为经卡分发，契约见 tal_bluetooth_ops.h）。BLE
 * 属平台能力：esp32 本 port 无任何 BLE 实现与头文件（无 tkl_bluetooth.h /
 * NimBLE 接线），成员置 NULL——core 侧分发检测到 NULL 后统一优雅降级
 * （OPRT_NOT_SUPPORTED），与今天"整模块不编译"的能力状态一致（esp32 的
 * sdkconfig 本就 CONFIG_ENABLE_BLUETOOTH 关闭，ble/ 不编入）。
 * 将来接 NimBLE 时，在此把 27 个成员接到平台实现（结构体参数按
 * linux 侧适配层的"镜像布局强转 + 编译期断言"方式接线）。
 *
 * 本文件只依赖 core 头（tal_bluetooth_ops.h），零副作用，放在 src/ 根恒编译。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_bluetooth_ops.h"

TAL_BLUETOOTH_OPS_T tal_bluetooth_ops_platform = {
    /* stack */
    .stack_init               = NULL,
    .stack_deinit             = NULL,
    .stack_gatt_link          = NULL,
    /* 事件注册 */
    .gap_callback_register    = NULL,
    .gatt_callback_register   = NULL,
    /* GAP */
    .gap_addr_set             = NULL,
    .gap_address_get          = NULL,
    .gap_adv_start            = NULL,
    .gap_adv_stop             = NULL,
    .gap_adv_rsp_data_set     = NULL,
    .gap_adv_rsp_data_update  = NULL,
    .gap_scan_start           = NULL,
    .gap_scan_stop            = NULL,
    .gap_connect              = NULL,
    .gap_disconnect           = NULL,
    .gap_conn_param_update    = NULL,
    .gap_rssi_get             = NULL,
    /* GATT server */
    .gatts_service_add        = NULL,
    .gatts_value_set          = NULL,
    .gatts_value_notify       = NULL,
    .gatts_exchange_mtu_reply = NULL,
    /* GATT client */
    .gattc_all_service_discovery = NULL,
    .gattc_all_char_discovery    = NULL,
    .gattc_char_desc_discovery   = NULL,
    .gattc_write_without_rsp     = NULL,
    .gattc_read                  = NULL,
    .gattc_exchange_mtu_request  = NULL,
};
