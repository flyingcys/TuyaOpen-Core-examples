/**
 * @file port_bluetooth_ops.c
 * @brief TAL BLE 卡（TAL_BLUETOOTH_OPS_T）的 linux 适配层：卡成员 ↔ port_ble_*
 *
 * 本文件是 core 侧 tal_bluetooth.c（卡分发，契约见 tal_bluetooth_ops.h）与
 * linux TKL 实现（port_bluetooth.c）之间的**纯适配层**，不含任何业务逻辑与
 * 字段转换，只做三件事：
 *
 * 1. 镜像布局强转：卡的成员签名用 core 自持镜像类型（TAL_BLE_*），TKL 侧用
 *    TKL_BLE_*，两套类型**逐字段镜像、布局逐字节一致**（见下方编译期断言），
 *    故带结构体参数的成员直接"指针强转"转发，回调注册直接"函数指针强转"
 *    （回调参数同一内存同一指针，桥接无需 memcpy）；基础类型参数（含出参
 *    指针 p_link、数据指针 p_data）的 16 个成员直接绑定 port_ble_* 函数指针。
 * 2. 编译期断言兜底：镜像类型一旦漂移（改字段/改顺序/改类型）即编译失败，
 *    防止"静默错位"。port 同时看得见两套头（tal_bluetooth_ops.h +
 *    port_bluetooth.h），断言就放在这里。
 * 3. 次序约束转述（由 core 侧 tal_bluetooth.c 保证）：gap/gatt 回调注册必须
 *    先于 stack_init 与 gatts_service_add——linux 的 port_ble_stack_init 会把
 *    已注册的回调交给 bluez 层（bluez_inc_init(__gap_evt_cb, __gatt_evt_cb)，
 *    首次调用即锁存），注册晚于 stack_init 则回调丢失。
 *
 * 本 port 的 port_bt 是**部分实现**：27 个 port_ble_* 中 16 个本就是
 * OPRT_NOT_SUPPORTED 桩（如 stack_gatt_link / gap_addr_set / scan / gattc_*
 * 等），卡照原样绑定，行为与解耦前直调 port_ble_* 完全一致（不变更返回值、
 * 不做能力探测/降级——能力探测由 core 侧分发按卡成员是否为 NULL 处理）。
 *
 * 门控：本文件在 port/src/port_bt/ 下，随 CONFIG_ENABLE_BLUETOOTH 开关整体
 * 编入/排除（见 port/CMakeLists.txt 对 "/port_bt/" 的过滤），无需新增开关。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_bluetooth_ops.h"   /* core：卡契约 + TAL_BLE_* 镜像类型 */
#include "port_bluetooth.h"       /* port：本平台 TKL BLE 接口 + TKL_BLE_* 类型 */

#include <stddef.h>

/* ---------------------------------------------------------------------------
 * 编译期布局断言：镜像类型与 TKL 类型 sizeof 不一致即编译失败
 * （typedef 数组长度为 -1）。漂移时先同步 tal_bluetooth_ops.h 的镜像类型。
 * --------------------------------------------------------------------------- */
#define TAL_BLE_LAYOUT_ASSERT(mirror, tkl) \
    typedef char _tal_ble_assert_##mirror[(sizeof(mirror) == sizeof(tkl)) ? 1 : -1]

TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_ADDR_T, TKL_BLE_GAP_ADDR_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_DATA_T, TKL_BLE_DATA_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_ADV_PARAMS_T, TKL_BLE_GAP_ADV_PARAMS_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_SCAN_PARAMS_T, TKL_BLE_GAP_SCAN_PARAMS_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_UUID_T, TKL_BLE_UUID_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_CHAR_PARAMS_T, TKL_BLE_CHAR_PARAMS_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_SERVICE_PARAMS_T, TKL_BLE_SERVICE_PARAMS_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATTS_PARAMS_T, TKL_BLE_GATTS_PARAMS_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_CONNECT_EVT_T, TKL_BLE_GAP_CONNECT_EVT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_DISCONNECT_EVT_T, TKL_BLE_GAP_DISCONNECT_EVT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_NOTIFY_RESULT_EVT_T, TKL_BLE_NOTIFY_RESULT_EVT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_SVC_HANDLE_T, TKL_BLE_GATT_SVC_HANDLE_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_SVC_DISC_TYPE_T, TKL_BLE_GATT_SVC_DISC_TYPE_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_CHAR_HANDLE_T, TKL_BLE_GATT_CHAR_HANDLE_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_CHAR_DISC_TYPE_T, TKL_BLE_GATT_CHAR_DISC_TYPE_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_DESC_DISC_TYPE_T, TKL_BLE_GATT_DESC_DISC_TYPE_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_DATA_REPORT_T, TKL_BLE_DATA_REPORT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_SUBSCRIBE_EVT_T, TKL_BLE_SUBSCRBE_EVT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_READ_CHAR_EVT_T, TKL_BLE_READ_CHAR_EVT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T);
TAL_BLE_LAYOUT_ASSERT(TAL_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T);

/* 事件结构体再加关键字段偏移断言：联合体偏移与包头字段（conn_handle/result）
 * 一旦错位，回调桥接（同一指针直传）会静默读错字段，sizeof 相同也拦不住 */
#define TAL_BLE_OFFSET_ASSERT(mirror, tkl, field) \
    typedef char _tal_ble_off_assert_##mirror##_##field[(offsetof(mirror, field) == offsetof(tkl, field)) ? 1 : -1]

TAL_BLE_OFFSET_ASSERT(TAL_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, conn_handle);
TAL_BLE_OFFSET_ASSERT(TAL_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, result);
TAL_BLE_OFFSET_ASSERT(TAL_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, gap_event);
TAL_BLE_OFFSET_ASSERT(TAL_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, conn_handle);
TAL_BLE_OFFSET_ASSERT(TAL_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, result);
TAL_BLE_OFFSET_ASSERT(TAL_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, gatt_event);

/* ---------------------------------------------------------------------------
 * 回调注册薄包装：函数指针强转（TAL 回调 ↔ TKL 回调，参数布局已断言）
 * --------------------------------------------------------------------------- */

static OPERATE_RET tal_ble_ops_gap_callback_register(const TAL_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    return port_ble_gap_callback_register((TKL_BLE_GAP_EVT_FUNC_CB)gap_evt);
}

static OPERATE_RET tal_ble_ops_gatt_callback_register(const TAL_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    return port_ble_gatt_callback_register((TKL_BLE_GATT_EVT_FUNC_CB)gatt_evt);
}

/* ---------------------------------------------------------------------------
 * 结构体参数薄包装：镜像布局一致，入参/出参指针强转
 * --------------------------------------------------------------------------- */

static OPERATE_RET tal_ble_ops_gap_addr_set(TAL_BLE_GAP_ADDR_T const *p_peer_addr)
{
    return port_ble_gap_addr_set((TKL_BLE_GAP_ADDR_T const *)p_peer_addr);
}

static OPERATE_RET tal_ble_ops_gap_address_get(TAL_BLE_GAP_ADDR_T *p_peer_addr)
{
    return port_ble_gap_address_get((TKL_BLE_GAP_ADDR_T *)p_peer_addr);
}

static OPERATE_RET tal_ble_ops_gap_adv_start(TAL_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
    return port_ble_gap_adv_start((TKL_BLE_GAP_ADV_PARAMS_T const *)p_adv_params);
}

static OPERATE_RET tal_ble_ops_gap_adv_rsp_data_set(TAL_BLE_DATA_T const *p_adv, TAL_BLE_DATA_T const *p_scan_rsp)
{
    return port_ble_gap_adv_rsp_data_set((TKL_BLE_DATA_T const *)p_adv, (TKL_BLE_DATA_T const *)p_scan_rsp);
}

static OPERATE_RET tal_ble_ops_gap_adv_rsp_data_update(TAL_BLE_DATA_T const *p_adv, TAL_BLE_DATA_T const *p_scan_rsp)
{
    return port_ble_gap_adv_rsp_data_update((TKL_BLE_DATA_T const *)p_adv, (TKL_BLE_DATA_T const *)p_scan_rsp);
}

static OPERATE_RET tal_ble_ops_gap_scan_start(TAL_BLE_GAP_SCAN_PARAMS_T const *p_scan_params)
{
    return port_ble_gap_scan_start((TKL_BLE_GAP_SCAN_PARAMS_T const *)p_scan_params);
}

static OPERATE_RET tal_ble_ops_gap_connect(TAL_BLE_GAP_ADDR_T const *p_peer_addr,
                                           TAL_BLE_GAP_SCAN_PARAMS_T const *p_scan_params,
                                           TAL_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    return port_ble_gap_connect((TKL_BLE_GAP_ADDR_T const *)p_peer_addr,
                               (TKL_BLE_GAP_SCAN_PARAMS_T const *)p_scan_params,
                               (TKL_BLE_GAP_CONN_PARAMS_T const *)p_conn_params);
}

static OPERATE_RET tal_ble_ops_gap_conn_param_update(uint16_t conn_handle, TAL_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    return port_ble_gap_conn_param_update(conn_handle, (TKL_BLE_GAP_CONN_PARAMS_T const *)p_conn_params);
}

/* 非 const：TKL 侧回写 service/char handle，强转不丢 const 限定 */
static OPERATE_RET tal_ble_ops_gatts_service_add(TAL_BLE_GATTS_PARAMS_T *p_service)
{
    return port_ble_gatts_service_add((TKL_BLE_GATTS_PARAMS_T *)p_service);
}

/* ---------------------------------------------------------------------------
 * 平台 ops 卡：core 的 tal_bluetooth.c 经 extern 引用并完成 tal_ble_* 分发。
 * 移植新平台时在各自 port 的对应实现文件中提供同名卡即可。
 * --------------------------------------------------------------------------- */
TAL_BLUETOOTH_OPS_T tal_bluetooth_ops_platform = {
    /* stack */
    .stack_init              = port_ble_stack_init,
    .stack_deinit            = port_ble_stack_deinit,
    .stack_gatt_link         = port_ble_stack_gatt_link,
    /* 事件注册（须先于 stack_init 调用，见文件头说明） */
    .gap_callback_register   = tal_ble_ops_gap_callback_register,
    .gatt_callback_register  = tal_ble_ops_gatt_callback_register,
    /* GAP */
    .gap_addr_set            = tal_ble_ops_gap_addr_set,
    .gap_address_get         = tal_ble_ops_gap_address_get,
    .gap_adv_start           = tal_ble_ops_gap_adv_start,
    .gap_adv_stop            = port_ble_gap_adv_stop,
    .gap_adv_rsp_data_set    = tal_ble_ops_gap_adv_rsp_data_set,
    .gap_adv_rsp_data_update = tal_ble_ops_gap_adv_rsp_data_update,
    .gap_scan_start          = tal_ble_ops_gap_scan_start,
    .gap_scan_stop           = port_ble_gap_scan_stop,
    .gap_connect             = tal_ble_ops_gap_connect,
    .gap_disconnect          = port_ble_gap_disconnect,
    .gap_conn_param_update   = tal_ble_ops_gap_conn_param_update,
    .gap_rssi_get            = port_ble_gap_rssi_get,
    /* GATT server */
    .gatts_service_add        = tal_ble_ops_gatts_service_add,
    .gatts_value_set          = port_ble_gatts_value_set,
    .gatts_value_notify       = port_ble_gatts_value_notify,
    .gatts_exchange_mtu_reply = port_ble_gatts_exchange_mtu_reply,
    /* GATT client */
    .gattc_all_service_discovery = port_ble_gattc_all_service_discovery,
    .gattc_all_char_discovery    = port_ble_gattc_all_char_discovery,
    .gattc_char_desc_discovery   = port_ble_gattc_char_desc_discovery,
    .gattc_write_without_rsp     = port_ble_gattc_write_without_rsp,
    .gattc_read                  = port_ble_gattc_read,
    .gattc_exchange_mtu_request  = port_ble_gattc_exchange_mtu_request,
};
