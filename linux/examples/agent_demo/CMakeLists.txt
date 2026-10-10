cmake_minimum_required(VERSION 3.16)

project(agent_demo C)

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
            # int/string 选项：kconfiglib 的 string 值带引号（空串写作 CONFIG_X=""），
            # 这里剥掉外层引号再收下。注意 string(REGEX REPLACE) 会覆盖 CMAKE_MATCH_*，
            # 所以变量名与值都必须先落到局部变量里再动 REGEX（与 p2p_demo 同坑注释）。
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

# 内置默认：AI 全链路必需的开关（有线 demo，二维码用于扫码绑定）
if(NOT DEFINED CONFIG_ENABLE_AI_COMPONENTS)
    set(CONFIG_ENABLE_AI_COMPONENTS ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_WIRED)
    set(CONFIG_ENABLE_WIRED ON)
endif()
if(NOT DEFINED CONFIG_ENABLE_QRCODE)
    set(CONFIG_ENABLE_QRCODE ON)
endif()
# 编码器注册表（tuya_ai_encoder.c，纯注册无 opus 依赖）：
# svc_ai_agent 无条件引用 tuya_ai_get_encoder，不编会链接失败（本 demo PCM 直传不用编码器）
if(NOT DEFINED CONFIG_ENABLE_TUYA_CODEC)
    set(CONFIG_ENABLE_TUYA_CODEC ON)
endif()

# 素材路径默认（与 Kconfig 默认一致；空值走构建目录的 voice_16k.pcm）
if(NOT DEFINED CONFIG_AGENT_DEMO_AUDIO_FILE)
    set(CONFIG_AGENT_DEMO_AUDIO_FILE "")
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
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../iot_demo/qrcode/CMakeLists.txt")
    message(FATAL_ERROR "agent_demo 需与 iot_demo 同树（复用其 qrcode 组件）：找不到 ${CMAKE_CURRENT_SOURCE_DIR}/../iot_demo/qrcode/CMakeLists.txt")
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../iot_demo/qrcode ${CMAKE_BINARY_DIR}/qrcode)

# 4) 语音素材：configure 期复制到构建目录，供默认路径 "voice_16k.pcm" 使用
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/voice_16k.pcm ${CMAKE_BINARY_DIR}/voice_16k.pcm COPYONLY)

file(GLOB _agent_demo_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# 自测文件自带 main()（独立自测目标），不能进 app 的源列表
list(FILTER _agent_demo_srcs EXCLUDE REGEX "/agent_chain_selftest\\.c$")
list(FILTER _agent_demo_srcs EXCLUDE REGEX "/agent_nlg_selftest\\.c$")
list(FILTER _agent_demo_srcs EXCLUDE REGEX "/agent_http_selftest\\.c$")

add_executable(agent_demo ${_agent_demo_srcs})

target_include_directories(agent_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 显式提供的凭据覆盖 tuya_config.h 的 #ifndef 默认值
if(TUYA_PRODUCT_ID)
    target_compile_definitions(agent_demo PRIVATE TUYA_PRODUCT_ID="${TUYA_PRODUCT_ID}")
endif()
if(TUYA_OPENSDK_UUID)
    target_compile_definitions(agent_demo PRIVATE TUYA_OPENSDK_UUID="${TUYA_OPENSDK_UUID}")
endif()
if(TUYA_OPENSDK_AUTHKEY)
    target_compile_definitions(agent_demo PRIVATE TUYA_OPENSDK_AUTHKEY="${TUYA_OPENSDK_AUTHKEY}")
endif()

# demo 参数注入（app 与链路自测共用）
# 素材路径：非空时改用指定文件（默认走构建目录的 voice_16k.pcm）
set(_agent_demo_defs
    AGENT_DEMO_AUDIO_FILE="${CONFIG_AGENT_DEMO_AUDIO_FILE}"
)
target_compile_definitions(agent_demo PRIVATE ${_agent_demo_defs})

# app 链接：一个组同时包 core 库与 port（解析 TAL/云服务与 TKL 的循环引用）；
# 组外的 tuyaopen-port 提供头路径，tuyaopen-headers 提供开关宏注入与 port 头
# （app 自己的源码也要看见 ENABLE_* 与 TKL 头）
set(_agent_demo_link
    -Wl,--start-group
    ${TUYAOPEN_CORE_LIBS}
    $<TARGET_FILE:tuyaopen-port>
)
list(APPEND _agent_demo_link -Wl,--end-group tuyaopen-port tuyaopen-headers)
target_link_libraries(agent_demo PRIVATE ${_agent_demo_link})

# 链路自测（独立可执行，链接与 agent_demo 相同的库组）：
# 无云地验证 素材加载 → 帧切分 → 边界（不依赖网络与凭据）
# 只测 port 的 tkl_ai_demo_* 契约（上传编排依赖 AI 客户端上下文，属在线实跑范畴）
add_executable(agent_chain_selftest
    src/agent_chain_selftest.c
)
target_include_directories(agent_chain_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(agent_chain_selftest PRIVATE ${_agent_demo_defs})
target_link_libraries(agent_chain_selftest PRIVATE ${_agent_demo_link})

# 下行消费自测（独立可执行）：构造 STOP 事件（含非空末段）验证 agent_downlink
# 的消费策略（STOP 携带的末段必须打印）——防回归，不依赖云端分包行为
add_executable(agent_nlg_selftest
    src/agent_nlg_selftest.c
    src/agent_downlink.c
)
target_include_directories(agent_nlg_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(agent_nlg_selftest PRIVATE ${_agent_demo_link})

# http_manager URL 解析边界自测（独立可执行，不依赖网络）：
# 端口越界必须被 parser 拒绝（uint16 回绕会静默连错端口/落默认端口）
add_executable(agent_http_selftest
    src/agent_http_selftest.c
)
target_include_directories(agent_http_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(agent_http_selftest PRIVATE ${_agent_demo_link})
unset(_agent_demo_link)
unset(_agent_demo_defs)

# qrcode 无循环依赖，组外链接；开关关闭时目标不注册，跳过
if(TARGET qrcode)
    target_link_libraries(agent_demo PRIVATE qrcode)
endif()

if(ENABLE_ASAN)
    target_compile_options(agent_demo PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(agent_demo PRIVATE -fsanitize=address)
    target_compile_options(agent_chain_selftest PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(agent_chain_selftest PRIVATE -fsanitize=address)
endif()
