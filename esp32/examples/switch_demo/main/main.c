/* switch_demo —— ESP32 版骨架。
 *
 * 当前 esp32 port 的 TKL 实现尚未补齐（见 ../../port/README.md），
 * 本文件刻意不调用任何 tuya API：核心各静态库会正常编译，但不会
 * 把未实现的 tkl_* 符号拉进链接，因此 idf.py build 在 TKL 补齐前
 * 也能通过，用于验证组件接入结构。
 *
 * TKL 就绪后，参考 linux 版 main.c（tuyaopen-core-examples/linux/
 * examples/switch_demo/main.c）接入 tuya_iot_init / tuya_iot_connect，
 * 并在组件 CMakeLists 里设置 TUYA_PRODUCT_ID / TUYA_OPENSDK_UUID /
 * TUYA_OPENSDK_AUTHKEY 编译定义。
 */
#include <stdio.h>

void app_main(void)
{
    printf("switch_demo (esp32 skeleton): tuyaopen-core component wired, TKL TODO\n");
}
