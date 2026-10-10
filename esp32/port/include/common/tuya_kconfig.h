/* tuya_kconfig.h —— ESP32（esp-idf / FreeRTOS）配置头（静态，手工可改）。
 *
 * 三部分内容（与 linux/port/include/common/tuya_kconfig.h 同构）：
 *   1. 开关宏：默认全部不定义。三种开启方式（按优先级）：
 *      a) 构建系统注入：esp-idf 下由 sdkconfig（Kconfig.projbuild → CONFIG_*）
 *         经 tuyaopen-core.cmake 的宏注入表转为 ENABLE_*=1，注入
 *         tuyaopen-headers（core 模块）与 tuyaopen-port（port 模块）；
 *      b) 编译命令/IDE 全局宏：-DENABLE_WIRED=1（只传要开启的）；
 *      c) 取消注释下方对应 #define。
 *      注意：开关值必须与实际参与编译的源文件一致，否则链接失败。
 *   2. 板级宏：ESP32（FreeRTOS）平台的默认值
 *   3. 参数默认值：全部 #ifndef 保护，可被 -D 覆盖
 */
#ifndef TUYA_KCONFIG_H
#define TUYA_KCONFIG_H

/* ==================== 1. 开关（默认全关，按需开启） ==================== */
/* 优先用 sdkconfig（menuconfig 的 tuyaopen-core 菜单），一般无需改这里。
 * #ifndef ENABLE_WIRED
 * #define ENABLE_WIRED 1
 * #endif */
/* #ifndef ENABLE_BLUETOOTH
#define ENABLE_BLUETOOTH 1
#endif */
/* #ifndef ENABLE_QRCODE
#define ENABLE_QRCODE 1
#endif */
/* #ifndef ENABLE_TUYA_P2P
#define ENABLE_TUYA_P2P 1
#endif */
/* #ifndef ENABLE_LOCAL_LINK
#define ENABLE_LOCAL_LINK 1
#endif */
/* #ifndef LITTLE_END
#define LITTLE_END 1
#endif */

/* ==================== 2. 板级宏（ESP32 / FreeRTOS） ==================== */
#ifndef OPERATING_SYSTEM
#define OPERATING_SYSTEM 98 /* SYSTEM_FREERTOS */
#endif

#ifndef AUDIO_DRIVER_NAME
#define AUDIO_DRIVER_NAME "audio_driver"
#endif

/* ==================== 3. 参数默认值 ==================== */
#ifndef MAX_SECURITY_LEVEL
#define MAX_SECURITY_LEVEL 1
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
