/**
 * @file main.c
 * @brief iot_demo（ESP32 / esp-idf）应用入口
 *
 * app_main 只做 esp-idf 平台初始化（NVS），随后把应用主流程交给
 * tuya_app_main()（tuya_main.c，建独立线程跑 user_main）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "esp_err.h"
#include "nvs_flash.h"

/* Tuya 应用主线程（定义在 tuya_main.c） */
extern void tuya_app_main(void);

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    tuya_app_main();
}
