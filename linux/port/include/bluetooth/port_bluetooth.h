#ifndef __TKL_BLUETOOTH_H__
#define __TKL_BLUETOOTH_H__

#include "port_bluetooth_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Function for initializing the ble stack
 * @param   role                Indicate the role for ble stack.
 *                              role = 1: ble peripheral    @TKL_BLE_ROLE_SERVER
 *                              role = 2: ble central       @TKL_BLE_ROLE_CLIENT
 * @return  SUCCESS             Initialized successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_stack_init(uint8_t role);

/**
 * @brief   Function for de-initializing the ble stack features
 * @param   role                 Indicate the role for ble stack.
 *                               role = 1: ble peripheral
 *                               role = 2: ble central
 * @return  SUCCESS             Deinitialized successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_stack_deinit(uint8_t role);

/**
 * @brief   Function for getting the GATT Link-Support.
 * @param   p_link              return gatt link                 
 * @return  SUCCESS             Support Gatt Link
 *          ERROR               Only Beacon or Mesh Beacon, Not Support Gatt Link.
 * */
OPERATE_RET port_ble_stack_gatt_link(uint16_t *p_link);

/**
 * @brief   Register GAP Event Callback
 * @param   TKL_BLE_GAP_EVT_FUNC_CB Refer to @TKL_BLE_GAP_EVT_FUNC_CB
 * @return  SUCCESS         Register successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gap_callback_register(const TKL_BLE_GAP_EVT_FUNC_CB gap_evt);

/**
 * @brief   Register GATT Event Callback
 * @param   TKL_BLE_GATT_EVT_FUNC_CB Refer to @TKL_BLE_GATT_EVT_FUNC_CB
 * @return  SUCCESS         Register successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gatt_callback_register(const TKL_BLE_GATT_EVT_FUNC_CB gatt_evt);

/******************************************************************************************************************************/
/** @brief Define All GAP Interface
 */
/**
 * @brief   Set the local Bluetooth identity address.
 *          The local Bluetooth identity address is the address that identifies this device to other peers.
 *          The address type must be either @ref TKL_BLE_GAP_ADDR_TYPE_PUBLIC or @ref TKL_BLE_GAP_ADDR_TYPE_RANDOM.
 * @param   [in] p_peer_addr:   pointer to local address parameters 
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_addr_set(TKL_BLE_GAP_ADDR_T const *p_peer_addr);
 
/**
 * @brief   Get the local Bluetooth identity address.
 * @param   [out] p_peer_addr:  pointer to local address
 * @return  SUCCESS             Set Address successfully.
 *          ERROR
 * */
OPERATE_RET port_ble_gap_address_get(TKL_BLE_GAP_ADDR_T *p_peer_addr);
 
/**
 * @brief   Start advertising
 * @param   [in] p_adv_params : pointer to advertising parameters 
 * @return  SUCCESS
 *  ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_start(TKL_BLE_GAP_ADV_PARAMS_T const *p_adv_params);

/**
 * @brief   Stop advertising
 * @param   void
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_stop(void);

/**
 * @brief   Setting advertising data
 * @param   [in] p_adv:         Data to be used in advertisement packets, and include adv data len
 *          [in] p_scan_rsp:    Data to be used in advertisement respond packets, and include rsp data len
 * @Note    Please Check p_adv and p_scan_rsp, if data->p_data == NULL or data->length == 0, we will not update these values.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_rsp_data_set(TKL_BLE_DATA_T const *p_adv, TKL_BLE_DATA_T const *p_scan_rsp);

/**
 * @brief   Update advertising data
 * @param   [in] p_adv: Data    to be used in advertisement packets, and include adv data len
 *          [in] p_scan_rsp:    Data to be used in advertisement respond packets, and include rsp data len
 * @Note    Please Check p_adv and p_scan_rsp, if data->p_data == NULL or data->length == 0, we will not update these values.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_adv_rsp_data_update(TKL_BLE_DATA_T const *p_adv, TKL_BLE_DATA_T const *p_scan_rsp);

/**
 * @brief   Start scanning
 * @param   [in] scan_param:    scan parameters including interval, windows
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_scan_start(TKL_BLE_GAP_SCAN_PARAMS_T const *p_scan_params);

/**
 * @brief   Stop scanning
 * @param   void
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_scan_stop(void);

/**
 * @brief   Start connecting one peer
 * @param   [in] p_peer_addr:   include address and address type
 *          [in] p_scan_params: scan parameters
 *          [in] p_conn_params: connection  parameters
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_connect(TKL_BLE_GAP_ADDR_T const *p_peer_addr, TKL_BLE_GAP_SCAN_PARAMS_T const *p_scan_params, TKL_BLE_GAP_CONN_PARAMS_T const *p_conn_params);

/**
 * @brief   Disconnect from peer
 * @param   [in] conn_handle:   the connection handle
 *          [in] hci_reason:    terminate reason
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_reason);

/**
 * @brief   Start to update connection parameters
 * @param   [in] conn_handle:   connection handle
 *          [in] p_conn_params: connection  parameters
 * @return  SUCCESS
 *          ERROR
 * */
OPERATE_RET port_ble_gap_conn_param_update(uint16_t conn_handle, TKL_BLE_GAP_CONN_PARAMS_T const *p_conn_params);

/**
 * @brief   Get the received signal strength for the last connection event.
 * @param   [in]conn_handle:    connection handle
 * @return  SUCCESS             Successfully read the RSSI.
 *          ERROR               No sample is available.
 * */
OPERATE_RET port_ble_gap_rssi_get(uint16_t conn_handle);

/**
 * @brief   Add Ble Gatt Service
 * @param   [in] p_service: define the ble service
 *
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_service_add(TKL_BLE_GATTS_PARAMS_T *p_service);

/**
 * @brief   Set the value of a given attribute. After Config Tuya Read-Char, we can update read-value at any time.
 * @param   [in] conn_handle    Connection handle.
 *          [in] char_handle    Attribute handle.
 *          [in,out] p_value    Attribute value information.
 * @return  SUCCESS
 *          ERROR
 *
 * @note Values other than system attributes can be set at any time, regardless of whether any active connections exist. 
 * */ 
OPERATE_RET port_ble_gatts_value_set(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);

/**
 * @brief   Notify an attribute value.
 * @param   [in] conn_handle    Connection handle.
 * @param   [in] char_handle    Attribute handle.
 *          [in] p_data         Notify Values
 *          [in] length         Value Length
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);

/**
 * @brief   Reply to an ATT_MTU exchange request by sending an Exchange MTU Response to the client.
 * @param   [in] conn_handle    Connection handle.
 *          [in] server_rx_mtu  mtu size.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gatts_exchange_mtu_reply(uint16_t conn_handle, uint16_t server_rx_mtu);

/******************************************************************************************************************************/
/** @brief Define All Gatt Client Interface, Refer to current ble gw and ble stack.
 *
 *  Notes: notice the handle will be the one of signed point.
 *  Discovery Operations belongs to GAP Interface, But declear here, because it will be used for the gatt client.
 */
 
/**
 * @brief   [Ble Central] Will Discovery All Service
 * @param   [in] conn_handle    Connection handle.
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gattc_all_service_discovery(uint16_t conn_handle);

/**
 * @brief   [Ble Central] Will Discovery All Characteristic
 * @param   [in] conn_handle    Connection handle.
 *          [in] start_handle   Handle of start
 *          [in] end_handle     Handle of End
 * @return  SUCCESS
 *          ERROR
 * @Note:   For Tuya Service, it may contains more optional service, it is more better to find all Characteristic 
 *          instead of find specific uuid.
 * */  
OPERATE_RET port_ble_gattc_all_char_discovery(uint16_t conn_handle, uint16_t start_handle, uint16_t end_handle);

/**
 * @brief   [Ble Central] Will Discovery All Descriptor of Characteristic
 * @param   [in] conn_handle    Connection handle.
 * @param   [in] conn_handle    Connection handle.
 *          [in] start_handle   Handle of start
 *          [in] end_handle     Handle of End
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gattc_char_desc_discovery(uint16_t conn_handle, uint16_t start_handle, uint16_t end_handle);

/**
 * @brief   [Ble Central] Write Data without Response
 * @param   [in] conn_handle    Connection handle.
 * @param   [in] char_handle    Attribute handle.
 *          [in] p_data         Write Values
 *          [in] length         Value Length
 * @return  SUCCESS
 *          ERROR
 * */ 
OPERATE_RET port_ble_gattc_write_without_rsp(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length);

/**
 * @brief   [Ble Central] Read Data
 * @param   [in] conn_handle    Connection handle.
 * @param   [in] char_handle    Attribute handle.
 * @return  SUCCESS
 *          ERROR
 * */
OPERATE_RET port_ble_gattc_read(uint16_t conn_handle, uint16_t char_handle);

/**
 * @brief   Start an ATT_MTU exchange by sending an Exchange MTU Request to the server.
 * @param   [in] conn_handle    Connection handle.
 *          [in] client_rx_mtu  mtu size.
 * @return  SUCCESS
 *          ERROR
 * */
OPERATE_RET port_ble_gattc_exchange_mtu_request(uint16_t conn_handle, uint16_t client_rx_mtu);

#ifdef __cplusplus
}
#endif

#endif

