/**
 * @file tuya_config.h
 * @brief agent_demo 凭据配置（PID / UUID / AUTHKEY）
 *
 * 默认值取自 iot_demo（PID qhivvyqawogv04e4，已随该 demo 验证可激活连云）。
 * 覆盖优先级：-D 编译宏 > 环境变量 > tuya_config_secrets.h > 本文件默认值。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef TUYA_CONFIG_H_
#define TUYA_CONFIG_H_

#if __has_include("tuya_config_secrets.h")
#include "tuya_config_secrets.h"
#endif

/**
 * @brief configure the product information
 *
 * TUYA_PRODUCT_ID: PID, create on the Tuya IoT platform
 * TUYA_DEVICE_UUID: UUID, create on the Tuya IoT platform
 * TUYA_DEVICE_AUTHKEY: AUTHKEY, create on the Tuya IoT platform
 *
 * warning: please replace these production information with your product id
 * and license, otherwise the demo cannot work.
 */
// clang-format off
#ifndef TUYA_PRODUCT_ID
#define TUYA_PRODUCT_ID      "alon7qgyjj8yus74"                        // 上游 your_chat_bot 同款 AI 产品 PID
#endif

#ifndef TUYA_OPENSDK_UUID
#define TUYA_OPENSDK_UUID      "uuid3dfa17df5ff96fc6"                  // iot_demo 同款授权码
#endif
#ifndef TUYA_OPENSDK_AUTHKEY
#define TUYA_OPENSDK_AUTHKEY   "92J3cLMEYhwTvN3bUWiEqpBfcvvo10V8"      // iot_demo 同款授权码
#endif

// clang-format on

#endif
