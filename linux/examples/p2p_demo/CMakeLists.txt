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
            # int/string 选项（demo 参数、样片路径等）：kconfiglib 的 string 值带
            # 引号（空串写作 CONFIG_X=""），这里剥掉外层引号再收下——否则空值会
            # 变成两个引号字符、"非空"判断失真，注入出 -DXXX="""" 这种空串宏。
            # 注意：string(REGEX REPLACE) 会覆盖 CMAKE_MATCH_*，所以变量名与值都必须
            # 先落到局部变量里再动 REGEX，否则 set 会写到被覆盖后的组名上
            # （曾把 "live.h264" 写成 set(CONFIG_live.h264 ...)）。
            if(NOT DEFINED CONFIG_${CMAKE_MATCH_1})
                set(_tuyaopen_name "${CMAKE_MATCH_1}")
                set(_tuyaopen_val "${CMAKE_MATCH_2}")
                string(REGEX REPLACE "^\"(.*)\"$" "\\1" _tuyaopen_val "${_tuyaopen_val}")
                set(CONFIG_${_tuyaopen_name} "${_tuyaopen_val}")
            endif()
        endif()
    endforeach()
    unset(_tuyaopen_line)
    unset(_tuyaopen_name)
    unset(_tuyaopen_val)
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
    set(CONFIG_P2P_DEMO_WIDTH 2304)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_HEIGHT)
    set(CONFIG_P2P_DEMO_HEIGHT 1296)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_FPS)
    set(CONFIG_P2P_DEMO_FPS 15)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_KBPS)
    set(CONFIG_P2P_DEMO_KBPS 960)
endif()
if(NOT DEFINED CONFIG_P2P_DEMO_GOP)
    set(CONFIG_P2P_DEMO_GOP 60)
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

# 3) 公共组件：qrcode（examples/qrcode，iot/agent/p2p 三 demo 共用一份，随 CONFIG_ENABLE_QRCODE 门控）
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../qrcode/CMakeLists.txt")
    message(FATAL_ERROR "p2p_demo 需在 examples 树内构建（复用公共 qrcode 组件）：找不到 ${CMAKE_CURRENT_SOURCE_DIR}/../qrcode/CMakeLists.txt")
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qrcode ${CMAKE_BINARY_DIR}/qrcode)

# 4) 视频源：configure 期复制到构建目录，供 demo_vcam 默认路径 "demo_video.264" 使用
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/demo_video.264 ${CMAKE_BINARY_DIR}/demo_video.264 COPYONLY)

file(GLOB _p2p_demo_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# 自测（selftest/ 目录，独立目标自带 main()）不在此 GLOB 范围内，无需过滤

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

# demo 参数与开关注入（app 与媒体链路自测共用；core 的 tuyaopen-headers 只管 ENABLE_* 开关宏）
set(_p2p_demo_media_defs
    P2P_DEMO_WIDTH=${CONFIG_P2P_DEMO_WIDTH}
    P2P_DEMO_HEIGHT=${CONFIG_P2P_DEMO_HEIGHT}
    P2P_DEMO_FPS=${CONFIG_P2P_DEMO_FPS}
    P2P_DEMO_KBPS=${CONFIG_P2P_DEMO_KBPS}
    P2P_DEMO_GOP=${CONFIG_P2P_DEMO_GOP}
)
# .config 里的 "y"/"n" 在 CMake 里直接可判真假（y=真，n=假）
# local_store 回放：demo 开关与 core 开关同时打开才注入 P2P_DEMO_LOCAL_STORE=1
# （代码里唯一开关，见 tuya_ipc_demo.h 的契约注释）。只开 demo 开关时告警且不注入
# ——回放不编译，live 链路照常（menuconfig 路径由 Kconfig select 保证一致）。
if(CONFIG_P2P_DEMO_LOCAL_STORE)
    if(CONFIG_ENABLE_LOCAL_STORE)
        list(APPEND _p2p_demo_media_defs P2P_DEMO_LOCAL_STORE=1)
    else()
        message(WARNING "CONFIG_P2P_DEMO_LOCAL_STORE 打开但 CONFIG_ENABLE_LOCAL_STORE 未开："
                        "回放不会编译（menuconfig 路径由 Kconfig select 保证一致；"
                        "用 -D 时两个都要给）")
    endif()
endif()
if(CONFIG_P2P_DEMO_LIVE_RECORD)
    list(APPEND _p2p_demo_media_defs P2P_DEMO_LIVE_RECORD=1)
endif()
# 分段时长：Kconfig 只在 LIVE_RECORD 打开时写出，未定义/空值时兜底 120
if(NOT DEFINED CONFIG_P2P_DEMO_RECORD_MAX_SEC OR CONFIG_P2P_DEMO_RECORD_MAX_SEC STREQUAL "")
    set(CONFIG_P2P_DEMO_RECORD_MAX_SEC 120)
endif()
list(APPEND _p2p_demo_media_defs P2P_DEMO_RECORD_MAX_SEC=${CONFIG_P2P_DEMO_RECORD_MAX_SEC})
if(CONFIG_P2P_DEMO_PB_SEED)
    list(APPEND _p2p_demo_media_defs P2P_DEMO_PB_SEED=1)
endif()
# 样片路径覆盖：非空时改用指定文件（默认走构建目录的 demo_video.264）
if(CONFIG_P2P_DEMO_H264_FILE)
    list(APPEND _p2p_demo_media_defs P2P_DEMO_H264_FILE="${CONFIG_P2P_DEMO_H264_FILE}")
endif()
target_compile_definitions(p2p_demo PRIVATE ${_p2p_demo_media_defs})

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
    selftest/demo_vcam_selftest.c
    src/demo_vcam.c
)
target_include_directories(vcam_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 与 media_chain_selftest 一致：让 -DCONFIG_P2P_DEMO_H264_FILE 等覆盖也作用于
# vcam 自测，否则用户换视频源后自测会静默测内置视频
target_compile_definitions(vcam_selftest PRIVATE ${_p2p_demo_media_defs})
target_link_libraries(vcam_selftest PRIVATE ${_p2p_demo_link})

# 媒体链路自测（独立可执行，链接与 p2p_demo 相同的库组）：
# 无云无 App 地跑通 vcam → 帧校验/队列/ring buffer → 取帧回调。
# 不带 tuya_main.c/reset_netcfg.c（自测不连云），媒体实现面显式列出
add_executable(media_chain_selftest
    selftest/media_chain_selftest.c
    src/tuya_ipc_demo.c
    src/demo_vcam.c
    src/demo_media_event.c
)
target_include_directories(media_chain_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(media_chain_selftest PRIVATE ${_p2p_demo_media_defs})
target_link_libraries(media_chain_selftest PRIVATE ${_p2p_demo_link})
unset(_p2p_demo_link)
unset(_p2p_demo_media_defs)

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
    # 媒体链路自测同样带上（帧池/ring buffer 拷贝是本次新增的内存面）
    target_compile_options(media_chain_selftest PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(media_chain_selftest PRIVATE -fsanitize=address)
endif()
