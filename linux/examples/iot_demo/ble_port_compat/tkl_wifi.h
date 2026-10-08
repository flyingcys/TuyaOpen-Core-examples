/* tkl_wifi.h 的常量兼容替身（linux port 已删除 WIFI TKL 层）。
 *
 * 缺口：iot-core/ble/ble_netcfg.h 无条件 #include "tkl_wifi.h"，但仅使用
 * WIFI_SSID_LEN / WIFI_PASSWD_LEN 两个定长常量（BLE 配网下发 ssid/passwd
 * 的缓冲区定义）。linux 去 netmgr/WIFI 改造删除了 port 的 tkl_wifi.h，
 * ENABLE_BLUETOOTH=ON 且 ENABLE_WIFI=OFF 时该 include 失联。
 *
 * 取值来源：port 仓库 HEAD 的 port/include/wifi/tkl_wifi.h
 *   #define WIFI_SSID_LEN  32
 *   #define WIFI_PASSWD_LEN 64
 *
 * 范围控制：本目录经 tuyaopen-headers 注入（iot_demo/CMakeLists.txt，
 * 仅 TARGET tal_bluetooth 存在即 BT=ON 时生效），只 shadow include 查找；
 * 现存编译面中 include "tkl_wifi.h" 的仅 ble_netcfg.h 一处。本替身只提供
 * 常量，不伪造任何 tkl_wifi_* 函数原型（WIFI TKL 符号层面已不存在）。
 */
#ifndef _TKL_WIFI_COMPAT_H_
#define _TKL_WIFI_COMPAT_H_

/* tuya sdk definition of wifi ap info */
#define WIFI_SSID_LEN 32    /* tuya sdk definition WIFI SSID MAX LEN */
#define WIFI_PASSWD_LEN 64  /* tuya sdk definition WIFI PASSWD MAX LEN */

#endif /* _TKL_WIFI_COMPAT_H_ */
