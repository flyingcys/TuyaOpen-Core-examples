/* tuya_kconfig.h —— ESP32（esp-idf / FreeRTOS）配置头（静态，手工可改）。
 * 说明见 linux/port/include/common/tuya_kconfig.h 头部注释；
 * esp-idf 用户也可在 sdkconfig / 组件编译选项中传入开关宏。 */
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
/* #ifndef LITTLE_END
#define LITTLE_END 1
#endif */

/* ==================== 2. 板级宏（ESP32 / FreeRTOS） ==================== */
#ifndef OPERATING_SYSTEM
#define OPERATING_SYSTEM 98 /* SYSTEM_FREERTOS */
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

#endif /* TUYA_KCONFIG_H */
