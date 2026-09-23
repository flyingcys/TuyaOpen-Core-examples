# tuyaopen-core 构建入口（include 方式使用）。
#
# 宿主接入（普通 CMake 工程）：
#   include(<tuyaopen-core>/tuyaopen-core.cmake)
#   add_subdirectory(<port>)                # port 自注入头到 tuyaopen-headers
# esp-idf：根目录 CMakeLists.txt 是组件入口（idf_component_register 后同样
# include 本文件）——include 语境不调 project()，无需任何 esp-idf 特判。
#
# 前置：宿主已完成 project()。幂等：重复 include 自动跳过。
if(TARGET tuyaopen-headers)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/TuyaOpenOptions.cmake)

# 环境基底（角色同 esp-idf 的 esp_common）：宿主注入 port 头路径，模块统一
# PRIVATE 依赖它。模块间依赖各自显式声明（esp-idf REQUIRES/PRIV_REQUIRES
# 的纯 CMake 形态）。
# 宿主注入方式：
#   target_include_directories(tuyaopen-headers INTERFACE <port头>)
add_library(tuyaopen-headers INTERFACE)

# 开关 → 编译宏：只对开启的注入（源码守卫是 defined() 风格，OFF = 不定义）。
# 手工编译（不用本 CMake）的用户对应做法：编译命令 -DENABLE_WIFI=1 ...，
# 或在 port 的静态 tuya_kconfig.h 里取消注释对应 #define。
set(_tuyaopen_switch_defs)
foreach(_tuyaopen_opt IN ITEMS
    ENABLE_WIFI
    ENABLE_WIRED
    ENABLE_BLUETOOTH
    ENABLE_NIMBLE
    ENABLE_CELLULAR
    ENABLE_FILE_SYSTEM
    ENABLE_PLATFORM_MBEDTLS
    ENABLE_DEVICE_TIMER
    ENABLE_TUYA_P2P
    ENABLE_LOCAL_STORE
    ENABLE_IPC_RING_BUFFER
    ENABLE_TUYA_CODEC
    ENABLE_TUYA_CODEC_OPUS
    ENABLE_TUYA_CODEC_OPUS_IPC
    ENABLE_TUYA_CODEC_SPEEX
    ENABLE_AI_COMPONENTS
    ENABLE_COMP_AI_AUDIO
    ENABLE_COMP_AI_MCP
    ENABLE_COMP_AI_VIDEO
    ENABLE_WIFI_ULTRA_LOWPOWER
    ENABLE_QRCODE
    LITTLE_END
)
    if(CONFIG_${_tuyaopen_opt})
        list(APPEND _tuyaopen_switch_defs ${_tuyaopen_opt}=1)
    endif()
endforeach()
if(_tuyaopen_switch_defs)
    target_compile_definitions(tuyaopen-headers INTERFACE ${_tuyaopen_switch_defs})
endif()
unset(_tuyaopen_switch_defs)
unset(_tuyaopen_opt)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/core-common ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/core-common)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/core-communication ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/core-communication)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/core-iot ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/core-iot)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/core-ai ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/core-ai)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/core-p2p ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-core/core-p2p)

set(_tuyaopen_core_libs)
foreach(_tuyaopen_mod IN ITEMS
    libcjson
    tuya_transport
    libhttp
    libmqtt
    libtls
    common
    qrcode
    tal_system
    tal_network
    tal_kv
    tal_security
    tal_wifi
    tal_wired
    tal_bluetooth
    tal_cellular
    tal_wifi_ulp
    tuya_cloud_service
    tuya_ai_service
    utility
    ai_agent
    ai_main
    ai_skills
    ai_audio
    ai_mcp
    ai_video
    base_ice
    lib_rtp
    pjproject
    svc_ipc_core
    svc_streaming_p2p
    local_store
    svc_ring_buffer
)
    if(TARGET ${_tuyaopen_mod})
        list(APPEND _tuyaopen_core_libs ${_tuyaopen_mod})
    endif()
endforeach()

# 对外聚合：仅 core 模块。
# - 目标 tuyaopen-core：平铺传递，供无需处理循环引用的宿主（如 esp-idf 组件）使用
# - 变量 TUYAOPEN_CORE_LIBS：GNU/Clang 宿主用它把 core 库与 port 放进同一个
#   --start-group 解析 TAL/云服务与 TKL 的循环引用（CMake 3.16 下 raw flags 与
#   INTERFACE 目标混排会错位，变量列表可保证顺序）：
#   target_link_libraries(app PRIVATE
#       -Wl,--start-group ${TUYAOPEN_CORE_LIBS} $<TARGET_FILE:tuyaopen-port> -Wl,--end-group
#       tuyaopen-port)
add_library(tuyaopen-core INTERFACE)
target_link_libraries(tuyaopen-core INTERFACE ${_tuyaopen_core_libs})
set(TUYAOPEN_CORE_LIBS "${_tuyaopen_core_libs}" CACHE INTERNAL "tuyaopen-core static libs")
unset(_tuyaopen_core_libs)
unset(_tuyaopen_mod)
