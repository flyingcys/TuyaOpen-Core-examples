/* tuya_kconfig.h —— LINUX 平板配置头（静态，手工可改）。
 *
 * 本文件不再由构建系统生成，三部分内容：
 *   1. 开关宏：默认全部不定义。两种开启方式（二选一，-D 优先）：
 *      a) 编译命令/IDE 全局宏：-DENABLE_WIFI=1（只传要开启的）
 *      b) 取消注释下方对应 #define
 *      注意：开关值必须与实际参与编译的源文件一致（开 ENABLE_WIFI 就要
 *      把 tal_wifi/、netcfg/、port 的 tkl_wifi/ 加入编译），否则链接失败。
 *   2. 板级宏：LINUX 平台的默认值
 *   3. 参数默认值：全部 #ifndef 保护，可被 -D 覆盖
 */
#ifndef TUYA_KCONFIG_H
#define TUYA_KCONFIG_H

/* ==================== 1. 开关（默认全关，按需开启） ==================== */
/* #ifndef ENABLE_WIRED
#define ENABLE_WIRED 1
#endif */
/* #ifndef ENABLE_WIFI
#define ENABLE_WIFI 1
#endif */
/* #ifndef ENABLE_BLUETOOTH
#define ENABLE_BLUETOOTH 1
#endif */
/* #ifndef ENABLE_QRCODE
#define ENABLE_QRCODE 1
#endif */
/* #ifndef ENABLE_TUYA_P2P
#define ENABLE_TUYA_P2P 1
#endif */
/* #ifndef LITTLE_END
#define LITTLE_END 1
#endif */

/* ==================== 2. 板级宏（LINUX） ==================== */
#ifndef OPERATING_SYSTEM
#define OPERATING_SYSTEM 100 /* SYSTEM_LINUX */
#endif

#ifndef WLAN_DEV
#define WLAN_DEV "wlan0"
#endif

#ifndef WLAN_AP
#define WLAN_AP "wlan1"
#endif

#ifndef WIFI_DB_PATH
#define WIFI_DB_PATH "./tuya_db_files"
#endif

#ifndef AUDIO_DRIVER_NAME
#define AUDIO_DRIVER_NAME "audio_driver"
#endif

/* ==================== 3. 参数默认值 ==================== */
#ifndef MAX_SECURITY_LEVEL
#define MAX_SECURITY_LEVEL 1
#endif

#ifndef SERIAL_CLI_STACK_SIZE
#define SERIAL_CLI_STACK_SIZE 3072
#endif

#ifndef TUYA_SECURITY_LEVEL
#define TUYA_SECURITY_LEVEL 1
#endif

#ifndef STACK_SIZE_TIMERQ
#define STACK_SIZE_TIMERQ 4096
#endif

#ifndef STACK_SIZE_WORK_QUEUE
#define STACK_SIZE_WORK_QUEUE 5120
#endif

#ifndef MAX_NODE_NUM_WORK_QUEUE
#define MAX_NODE_NUM_WORK_QUEUE 100
#endif

#ifndef STACK_SIZE_MSG_QUEUE
#define STACK_SIZE_MSG_QUEUE 4096
#endif

#ifndef MAX_NODE_NUM_MSG_QUEUE
#define MAX_NODE_NUM_MSG_QUEUE 100
#endif

#ifndef AI_BIZ_MAX_NUM
#define AI_BIZ_MAX_NUM 5
#endif

#ifndef AI_SESSION_MAX_NUM
#define AI_SESSION_MAX_NUM 2
#endif

#ifndef AI_HEARTBEAT_INTERVAL
#define AI_HEARTBEAT_INTERVAL 120
#endif

#ifndef AI_MAX_FRAGMENT_LENGTH
#define AI_MAX_FRAGMENT_LENGTH 8192
#endif

#ifndef AI_CLIENT_STACK_SIZE
#define AI_CLIENT_STACK_SIZE 4096
#endif

#ifndef AI_PACKET_SECURITY_LEVEL
#define AI_PACKET_SECURITY_LEVEL 4
#endif

#ifndef AI_VERSION
#define AI_VERSION 2
#endif

#ifndef AI_SUB_VERSION
#define AI_SUB_VERSION 1
#endif

#endif /* TUYA_KCONFIG_H */
