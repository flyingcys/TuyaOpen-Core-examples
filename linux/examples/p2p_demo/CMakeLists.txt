cmake_minimum_required(VERSION 3.16)

project(p2p_demo C)

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
        elseif(_tuyaopen_line MATCHES "^CONFIG_([A-Z0-9_]+)=(.+)$")
            # int/string 选项（demo 参数、样片路径等）：原样收下，
            # 由下面的分支决定怎么注入编译宏
            if(NOT DEFINED CONFIG_${CMAKE_MATCH_1})
                set(CONFIG_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
            endif()
        endif()
    endforeach()
    unset(_tuyaopen_line)
    unset(_tuyaopen_kconfig_lines)
endif()
unset(TUYAOPEN_DOTCONFIG)

# 内置默认：P2P 链路必需的开关（有线 demo，二维码用于扫码绑定）
if(NOT DEFINED CONFIG_ENABLE_WIRED)
    set(CONFIG_ENABLE_WIRED ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_QRCODE)
    set(CONFIG_ENABLE_QRCODE ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_TUYA_P2P)
    set(CONFIG_ENABLE_TUYA_P2P ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_LOCAL_LINK)
    set(CONFIG_ENABLE_LOCAL_LINK ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_IPC_RING_BUFFER)
    set(CONFIG_ENABLE_IPC_RING_BUFFER ON)
endif()

# demo 参数默认（与 Kconfig 默认一致；样片与 AV info 用）
if(NOT DEFINED CONFIG_P2P_DEMO_WIDTH)
    set(CONFIG_P2P_DEMO_WIDTH 640)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_HEIGHT)
    set(CONFIG_P2P_DEMO_HEIGHT 480)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_FPS)
    set(CONFIG_P2P_DEMO_FPS 20)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_KBPS)
    set(CONFIG_P2P_DEMO_KBPS 400)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_GOP)
    set(CONFIG_P2P_DEMO_GOP 20)
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

# 3) 应用自带组件：qrcode 复用 iot_demo 的副本（不复制文件，随 CONFIG_ENABLE_QRCODE 门控）
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../iot_demo/qrcode ${CMAKE_BINARY_DIR}/qrcode)

# 4) 样片：configure 期复制到构建目录，供 demo_vcam 默认路径 "sample.h264" 使用
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/sample.h264 ${CMAKE_BINARY_DIR}/sample.h264 COPYONLY)

file(GLOB _p2p_demo_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# demo_vcam_selftest.c 自带 main()（独立自测目标），不能进 app 的源列表
list(FILTER _p2p_demo_srcs EXCLUDE REGEX "/demo_vcam_selftest\\.c$")

add_executable(p2p_demo ${_p2p_demo_srcs})

target_include_directories(p2p_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 显式提供的凭据覆盖 tuya_config.h 的 #ifndef 默认值
if(TUYA_PRODUCT_ID)
    target_compile_definitions(p2p_demo PRIVATE TUYA_PRODUCT_ID="${TUYA_PRODUCT_ID}")
endif()
if(TUYA_OPENSDK_UUID)
    target_compile_definitions(p2p_demo PRIVATE TUYA_OPENSDK_UUID="${TUYA_OPENSDK_UUID}")
endif()
if(TUYA_OPENSDK_AUTHKEY)
    target_compile_definitions(p2p_demo PRIVATE TUYA_OPENSDK_AUTHKEY="${TUYA_OPENSDK_AUTHKEY}")
endif()

# demo 参数与开关注入（app 私有；core 的 tuyaopen-headers 只管 ENABLE_* 开关宏）
target_compile_definitions(p2p_demo PRIVATE
    P2P_DEMO_WIDTH=${CONFIG_P2P_DEMO_WIDTH}
    P2P_DEMO_HEIGHT=${CONFIG_P2P_DEMO_HEIGHT}
    P2P_DEMO_FPS=${CONFIG_P2P_DEMO_FPS}
    P2P_DEMO_KBPS=${CONFIG_P2P_DEMO_KBPS}
    P2P_DEMO_GOP=${CONFIG_P2P_DEMO_GOP}
)
# .config 里的 "y"/"n" 在 CMake 里直接可判真假（y=真，n=假）
if(CONFIG_P2P_DEMO_LOCAL_STORE)
    target_compile_definitions(p2p_demo PRIVATE P2P_DEMO_LOCAL_STORE=1)
endif()
# 样片路径覆盖：非空时改用指定文件（默认走构建目录的 sample.h264）
if(CONFIG_P2P_DEMO_H264_FILE)
    target_compile_definitions(p2p_demo PRIVATE P2P_DEMO_H264_FILE="${CONFIG_P2P_DEMO_H264_FILE}")
endif()

# app 链接：一个组同时包 core 库与 port（解析 TAL/云服务与 TKL 的循环引用）；
# 组外的 tuyaopen-port 提供头路径，tuyaopen-headers 提供开关宏注入与 port 头
# （app 自己的源码也要看见 ENABLE_* 与 TKL 头）
set(_p2p_demo_link
    -Wl,--start-group
    ${TUYAOPEN_CORE_LIBS}
    $<TARGET_FILE:tuyaopen-port>
)
list(APPEND _p2p_demo_link -Wl,--end-group tuyaopen-port tuyaopen-headers)
target_link_libraries(p2p_demo PRIVATE ${_p2p_demo_link})

# demo_vcam 自测（独立可执行，链接与 p2p_demo 相同的库组）
# demo_vcam.c 是自测的实现面（app 目标经 GLOB 收它），这里显式带上
add_executable(vcam_selftest
    src/demo_vcam_selftest.c
    src/demo_vcam.c
)
target_include_directories(vcam_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(vcam_selftest PRIVATE ${_p2p_demo_link})
unset(_p2p_demo_link)

# qrcode 无循环依赖，组外链接；开关关闭时目标不注册，跳过
if(TARGET qrcode)
    target_link_libraries(p2p_demo PRIVATE qrcode)
endif()

if(ENABLE_ASAN)
    target_compile_options(p2p_demo PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(p2p_demo PRIVATE -fsanitize=address)
    # 自测也带上：vcam 的缓冲/索引分配是本次新增的内存面，ASAN 下跑一遍更有价值
    target_compile_options(vcam_selftest PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(vcam_selftest PRIVATE -fsanitize=address)
endif()
