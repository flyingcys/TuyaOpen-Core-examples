cmake_minimum_required(VERSION 3.16)

project(iot_demo C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ENABLE_ASAN "Enable AddressSanitizer for memory debugging" OFF)

# Kconfig 配置（可选）：menuconfig 生成的 linux/.config 自动导入开关，
# y/n 值由 core 的 TuyaOpenOptions 归一化为 ON/OFF。
# 优先级：-D 命令行 > .config > 下方内置默认
set(TUYAOPEN_DOTCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../../.config")
if(EXISTS "${TUYAOPEN_DOTCONFIG}")
    file(STRINGS "${TUYAOPEN_DOTCONFIG}" _tuyaopen_kconfig_lines)
    foreach(_tuyaopen_line IN LISTS _tuyaopen_kconfig_lines)
        if(_tuyaopen_line MATCHES "^CONFIG_([A-Z0-9_]+)=(y|n)$")
            if(NOT DEFINED CONFIG_${CMAKE_MATCH_1})
                set(CONFIG_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
            endif()
        elseif(_tuyaopen_line MATCHES "^# CONFIG_([A-Z0-9_]+) is not set$")
            # menuconfig 关闭的开关（默认开的开关必须认这种写法，否则关不掉）
            if(NOT DEFINED CONFIG_${CMAKE_MATCH_1})
                set(CONFIG_${CMAKE_MATCH_1} n)
            endif()
        endif()
    endforeach()
    unset(_tuyaopen_line)
    unset(_tuyaopen_kconfig_lines)
endif()
unset(TUYAOPEN_DOTCONFIG)

# 内置默认（有线绑定的 demo 需要二维码输出）
if(NOT DEFINED CONFIG_ENABLE_WIRED)
    set(CONFIG_ENABLE_WIRED ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_QRCODE)
    set(CONFIG_ENABLE_QRCODE ON)
endif()

# 凭据可选：不设置时使用 src/tuya_config.h 的默认值，
# 或在同目录放一份 tuya_config_secrets.h（参考 .example 文件）
if(NOT TUYA_PRODUCT_ID AND DEFINED ENV{TUYA_PRODUCT_ID})
    set(TUYA_PRODUCT_ID "$ENV{TUYA_PRODUCT_ID}")
endif()
if(NOT TUYA_OPENSDK_UUID AND DEFINED ENV{TUYA_OPENSDK_UUID})
    set(TUYA_OPENSDK_UUID "$ENV{TUYA_OPENSDK_UUID}")
endif()
if(NOT TUYA_OPENSDK_AUTHKEY AND DEFINED ENV{TUYA_OPENSDK_AUTHKEY})
    set(TUYA_OPENSDK_AUTHKEY "$ENV{TUYA_OPENSDK_AUTHKEY}")
endif()

# core 默认取本仓库内的 git 子模块（linux/tuyaopen-core），也可用 -D 指向任意副本
set(TUYAOPEN_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tuyaopen-core" CACHE PATH "tuyaopen-core 仓库路径")
set(TUYAOPEN_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../port" CACHE PATH "linux TKL port 目录")
get_filename_component(TUYAOPEN_CORE_DIR "${TUYAOPEN_CORE_DIR}" ABSOLUTE)
get_filename_component(TUYAOPEN_PORT_DIR "${TUYAOPEN_PORT_DIR}" ABSOLUTE)

# 1) core：单一入口 tuyaopen-core.cmake（开关宏注入、五分组、聚合均在此完成）
include(${TUYAOPEN_CORE_DIR}/tuyaopen-core.cmake)

# 2) port：自包含子目录（自建 tuyaopen-port 并把头注入 tuyaopen-headers）
add_subdirectory(${TUYAOPEN_PORT_DIR} ${CMAKE_BINARY_DIR}/tuyaopen-port)

# 3) 公共组件：qrcode（examples/qrcode，iot/agent/p2p 三 demo 共用，CONFIG_ENABLE_QRCODE 门控）
# 树外源目录：必须显式指定 binary dir（与 agent/p2p 的写法一致）
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qrcode ${CMAKE_BINARY_DIR}/qrcode)

# tal_netif / tal_bluetooth 已随 core 自包含回归，不再是应用组件：
#   tal_netif    → tuyaopen-core/iot-core/tal_system（编入 tal_system）
#   tal_bluetooth→ tuyaopen-core/iot-core/ble（编入 tuya_cloud_service，随 BLUETOOTH 门控）
# 头经 tal_system / tuya_cloud_service 的 PUBLIC include 传递，无需额外注入。

file(GLOB _iot_demo_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_executable(iot_demo ${_iot_demo_srcs})

target_include_directories(iot_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 显式提供的凭据覆盖 tuya_config.h 的 #ifndef 默认值
if(TUYA_PRODUCT_ID)
    target_compile_definitions(iot_demo PRIVATE TUYA_PRODUCT_ID="${TUYA_PRODUCT_ID}")
endif()
if(TUYA_OPENSDK_UUID)
    target_compile_definitions(iot_demo PRIVATE TUYA_OPENSDK_UUID="${TUYA_OPENSDK_UUID}")
endif()
if(TUYA_OPENSDK_AUTHKEY)
    target_compile_definitions(iot_demo PRIVATE TUYA_OPENSDK_AUTHKEY="${TUYA_OPENSDK_AUTHKEY}")
endif()

# app 链接：一个组同时包 core 库与 port（解析 TAL/云服务与 TKL 的循环引用）；
# 组外的 tuyaopen-port 提供头路径，tuyaopen-headers 提供开关宏注入与 port 头
# （app 自己的源码也要看见 ENABLE_* 与 TKL 头）
set(_iot_demo_link
    -Wl,--start-group
    ${TUYAOPEN_CORE_LIBS}
    $<TARGET_FILE:tuyaopen-port>
)
# tal_netif（tal_system）/ tal_bluetooth（tuya_cloud_service）已随 TUYAOPEN_CORE_LIBS 在组内，
# 其 tkl_netif_* / tkl_ble_* 符号与本组内 tuyaopen-port 互相解析
list(APPEND _iot_demo_link -Wl,--end-group tuyaopen-port tuyaopen-headers)
target_link_libraries(iot_demo PRIVATE ${_iot_demo_link})

# tal_kv 自测（独立可执行，链接与 iot_demo 相同的库）
add_executable(tal_kv_selftest selftest/tal_kv_selftest.c)
target_link_libraries(tal_kv_selftest PRIVATE ${_iot_demo_link})
unset(_iot_demo_link)

# qrcode 无循环依赖，组外链接；开关关闭时目标不注册，跳过
if(TARGET qrcode)
    target_link_libraries(iot_demo PRIVATE qrcode)
endif()

if(ENABLE_ASAN)
    target_compile_options(iot_demo PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(iot_demo PRIVATE -fsanitize=address)
endif()
