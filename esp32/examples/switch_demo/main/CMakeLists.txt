idf_component_register(
    SRCS "main.c" "tuya_main.c" "reset_netcfg.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash tuyaopen-core tuyaopen-port
)

# 开关宏与 TKL 头（经 tuyaopen-headers 注入；app 自身源码也需要）
target_link_libraries(${COMPONENT_LIB} PRIVATE tuyaopen-headers)

# CONFIG_MBEDTLS_THREADING_ALT 下 esp-idf 的 mbedtls 组件自身编译也需要
# threading_alt.h——用 esp-idf 自带的 ROM 版（freertos 映射，自包含）。
# tuya 侧（tuya_tls.c）用 port 提供的 tal_mutex 版；mbedtls 内部只经
# set_alt 回调操作 mutex，两个 struct 定义各归各的分配者，互不冲突。
foreach(_mbed_tgt mbedtls mbedcrypto mbedx509)
    if(TARGET ${_mbed_tgt})
        target_include_directories(${_mbed_tgt} PUBLIC
            $ENV{IDF_PATH}/components/mbedtls/port/mbedtls_rom)
    endif()
endforeach()
unset(_mbed_tgt)
