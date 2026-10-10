# 官方库目录：全部为 git submodule / 官方源码（官方文件零修改）。
# 每个库注册为独立目标；自研连接层（iot-core/tuya_transport）链接它们，
# 并向官方库编译单元注入 transport_interface.h 与用户提供型配置头路径
# （core_http_config.h / core_mqtt_config.h / tuya_tls_config.h，
#  均位于 iot-core/tuya_transport/config/，不放官方目录内）。

# （TUYAOPEN_THIRD_PARTY_DIR 已由 tuyaopen-core.cmake 在顶层定义）

# 自研 KV 存储（历史遗留仍居于此，非官方库）

# cJSON（submodule，v1.7.18）：单文件库，目标名沿用 libcjson
add_library(libcjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/cJSON.c)
target_include_directories(libcjson PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cJSON>
)
target_link_libraries(libcjson PRIVATE tuyaopen-headers)

# coreHTTP（submodule，v3.1.3）：核心客户端 + 官方解析器 llhttp（嵌套 submodule）
add_library(corehttp STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/core_http_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/src/api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/src/llhttp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/src/http.c
)
target_include_directories(corehttp
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/llhttp/include
)

# coreMQTT（submodule，v5.0.2）
file(GLOB _coremqtt_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/coreMQTT/source/*.c"
)
add_library(coremqtt STATIC ${_coremqtt_srcs})
target_include_directories(coremqtt
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/coreMQTT/source/include>
)
unset(_coremqtt_srcs)

# backoffAlgorithm（AWS 官方 v1.3.0）：云连接重试退避
add_library(backoff_algorithm STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backoffAlgorithm/source/backoff_algorithm.c
)
target_include_directories(backoff_algorithm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backoffAlgorithm/source/include>
)

# mbedtls（submodule，v3.1.0）：整库编入（library/*.c）。
# 配置走官方覆盖机制 MBEDTLS_CONFIG_FILE -> tuya_transport/config/tuya_tls_config.h；
# CONFIG_ENABLE_PLATFORM_MBEDTLS=ON 时由平台提供、不注册本目标。
if(NOT CONFIG_ENABLE_PLATFORM_MBEDTLS)
    file(GLOB _mbedtls_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/*.c"
    )
    add_library(mbedtls STATIC ${_mbedtls_srcs})
    target_include_directories(mbedtls
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library
    )
    target_compile_definitions(mbedtls PUBLIC MBEDTLS_CONFIG_FILE=\"tuya_tls_config.h\")
    # threading_alt.h（裁剪配置的 THREADING_ALT 实现）引 tal_mutex 等 TAL 头
    target_link_libraries(mbedtls PRIVATE tuyaopen-headers tal_system)
    unset(_mbedtls_srcs)
else()
    message(STATUS "Using mbedtls from platform")
endif()
