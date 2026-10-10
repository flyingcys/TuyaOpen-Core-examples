# tuya_cloud_service 源码已拍平：原模块子目录（cloud/ netmgr/ netcfg/ ble/ ...）
# 直接位于 iot-core/ 下，因此目标在此定义，而不是在模块子目录里。

# 本分组自带的基础库：tuya_utilities（通用工具，零自研依赖）、tal_system（系统抽象）
# 与 tuya_transport（连接服务层，已吸收 tal_network / tal_security / libtls / http&mqtt wrap）。
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utilities)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tal_system)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tuya_transport)
file(GLOB_RECURSE _cloud_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lan/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cloud/*.c"
)

if(NOT CONFIG_ENABLE_DEVICE_TIMER)
    list(REMOVE_ITEM _cloud_srcs "${CMAKE_CURRENT_SOURCE_DIR}/cloud/tuya_device_timer.c")
endif()

# 局域网协议（tuya_lan.c）由 CONFIG_ENABLE_LOCAL_LINK 门控（默认 ON，行为不变）：
# 关闭后不编译，port 的局域网 net ops（accept/listen/send_to/recvfrom/recv_nd_size）
# 也无需适配。lan_sock.c（socket 事件循环）不门控——AI monitor 复用它。
if(NOT CONFIG_ENABLE_LOCAL_LINK)
    list(REMOVE_ITEM _cloud_srcs "${CMAKE_CURRENT_SOURCE_DIR}/lan/tuya_lan.c")
endif()

set(_cloud_incs
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud
    ${CMAKE_CURRENT_SOURCE_DIR}/file_storage
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/schema
    ${CMAKE_CURRENT_SOURCE_DIR}/lan
    # ${CMAKE_CURRENT_SOURCE_DIR}/netmgr
    ${CMAKE_CURRENT_SOURCE_DIR}/weather
)

# 配网：netcfg/ 目录（框架 + AP softAP 前端）已移除。
# 蓝牙配网为自包含模块 ble/ble_netcfg.c，随 BLUETOOTH 编入，
# 由应用层直接启动（tuya_ble_init → ble_netcfg_start(finish_cb)）。

# if(CONFIG_ENABLE_WIRED)
#     list(APPEND _cloud_srcs ${CMAKE_CURRENT_SOURCE_DIR}/netmgr/netconn_wired.c)
# endif()

# if(CONFIG_ENABLE_CELLULAR)
#     list(APPEND _cloud_srcs ${CMAKE_CURRENT_SOURCE_DIR}/netmgr/netconn_cellular.c)
# endif()

if(CONFIG_ENABLE_BLUETOOTH)
    file(GLOB_RECURSE _ble_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ble/*.c"
    )
    list(APPEND _cloud_srcs ${_ble_srcs})
    list(APPEND _cloud_incs ${CMAKE_CURRENT_SOURCE_DIR}/ble)
    unset(_ble_srcs)
endif()

add_library(tuya_cloud_service STATIC)
target_sources(tuya_cloud_service PRIVATE ${_cloud_srcs})
target_include_directories(tuya_cloud_service PUBLIC ${_cloud_incs})
set(_cloud_deps
    tuyaopen-headers
    tuya_transport
    corehttp
    coremqtt
    backoff_algorithm
    mbedtls
    libcjson
    tal_system
)
target_link_libraries(tuya_cloud_service
    PUBLIC tuya_transport libcjson tal_system corehttp coremqtt backoff_algorithm mbedtls
    PRIVATE ${_cloud_deps}
)
unset(_cloud_srcs)
unset(_cloud_incs)
unset(_cloud_deps)
