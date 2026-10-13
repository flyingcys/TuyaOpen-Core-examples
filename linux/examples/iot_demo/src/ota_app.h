/**
 * @file ota_app.h
 * @brief 应用层 OTA 实现（linux 写文件版）注册接口
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __OTA_APP_H__
#define __OTA_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 OTA 回调到 core（tal_ota_register_cbs）
 *
 * 需要 OTA 的应用在初始化时调用（云连接建立前）；不调用即不支持 OTA
 * （tal_ota_* 返回 OPRT_NOT_SUPPORTED）。
 *
 * @return none
 */
void ota_app_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_APP_H__ */
