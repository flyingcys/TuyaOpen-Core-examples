/* switch_demo —— ESP32 入口（参考 tuya_open_sdk 的壳：NVS 初始化后进入 tuya 流程） */
#include "nvs_flash.h"
#include "esp_log.h"

extern void user_main(void);

void app_main(void)
{
    // tal_kv / tkl_fs 依赖 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    user_main();
}
