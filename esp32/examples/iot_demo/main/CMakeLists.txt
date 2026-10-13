# demo 源码（qrcode 与 linux iot_demo 的 qrcode/ 子目录同源，随 QRCODE 开关裁剪）
set(_demo_srcs
    "main.c"
    "tuya_main.c"
    "wifi_sta.c"
    "reset_netcfg.c"
    "ota_app.c"
)
if(CONFIG_ENABLE_QRCODE)
    list(APPEND _demo_srcs
        "qrcode/qrencode_print.c"
        "qrcode/qrcodegen.c"
    )
endif()

idf_component_register(
    SRCS ${_demo_srcs}
    INCLUDE_DIRS "." "qrcode"
    REQUIRES nvs_flash tuyaopen-core tuyaopen-port esp_wifi esp_netif esp_event esp_timer
)
unset(_demo_srcs)

# 开关宏与 TKL 头（经 tuyaopen-headers 注入；app 自身源码也需要）
target_link_libraries(${COMPONENT_LIB} PRIVATE tuyaopen-headers)

# CONFIG_MBEDTLS_THREADING_ALT 下 esp-idf 的 mbedtls 组件自身编译也需要
# threading_alt.h——用 esp-idf 自带的 ROM 版（freertos 映射，自包含）。
# tuya 侧（tuya_tls.c）经 core 的 tuya_transport/config/threading_alt.h 用
# tal_mutex 版（两者内容同构、各归各的分配者，mbedtls 内部只经 set_alt
# 回调操作 mutex，互不冲突）。
foreach(_mbed_tgt mbedtls mbedcrypto mbedx509)
    if(TARGET ${_mbed_tgt})
        target_include_directories(${_mbed_tgt} PUBLIC
            $ENV{IDF_PATH}/components/mbedtls/port/mbedtls_rom)
    endif()
endforeach()
unset(_mbed_tgt)
