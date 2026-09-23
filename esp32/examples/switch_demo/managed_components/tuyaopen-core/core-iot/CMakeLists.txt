# tuya_cloud_service 源码已拍平：原模块子目录（cloud/ netmgr/ netcfg/ ble/ ...）
# 直接位于 core-iot/ 下，因此目标在此定义，而不是在模块子目录里。
file(GLOB_RECURSE _cloud_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lan/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/netmgr/netmgr.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cloud/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/file_storage/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tls/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/weather/*.c"
)

if(NOT CONFIG_ENABLE_DEVICE_TIMER)
    list(REMOVE_ITEM _cloud_srcs "${CMAKE_CURRENT_SOURCE_DIR}/cloud/tuya_device_timer.c")
endif()

set(_cloud_incs
    ${CMAKE_CURRENT_SOURCE_DIR}/cloud
    ${CMAKE_CURRENT_SOURCE_DIR}/file_storage
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/schema
    ${CMAKE_CURRENT_SOURCE_DIR}/lan
    ${CMAKE_CURRENT_SOURCE_DIR}/netmgr
    ${CMAKE_CURRENT_SOURCE_DIR}/weather
)

if(CONFIG_ENABLE_WIFI)
    file(GLOB _wifi_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/netmgr/netconn_wifi.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/netcfg/*.c"
    )
    list(APPEND _cloud_srcs ${_wifi_srcs})
    list(APPEND _cloud_incs ${CMAKE_CURRENT_SOURCE_DIR}/netcfg)
    unset(_wifi_srcs)
endif()

if(CONFIG_ENABLE_WIRED)
    list(APPEND _cloud_srcs ${CMAKE_CURRENT_SOURCE_DIR}/netmgr/netconn_wired.c)
endif()

if(CONFIG_ENABLE_CELLULAR)
    list(APPEND _cloud_srcs ${CMAKE_CURRENT_SOURCE_DIR}/netmgr/netconn_cellular.c)
endif()

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
    libcjson
    libhttp
    libmqtt
    libtls
    common
    tal_system
    tal_network
    tal_kv
    tal_security
)
# 网络模块的公共头泄漏：netconn_*.h 引对应 tal_* 头，开启时需 PUBLIC 传播
if(TARGET tal_wifi)
    list(APPEND _cloud_deps tal_wifi)
    target_link_libraries(tuya_cloud_service PUBLIC tal_wifi)
endif()
if(TARGET tal_wired)
    list(APPEND _cloud_deps tal_wired)
    target_link_libraries(tuya_cloud_service PUBLIC tal_wired)
endif()
if(TARGET tal_bluetooth)
    list(APPEND _cloud_deps tal_bluetooth)
    target_link_libraries(tuya_cloud_service PUBLIC tal_bluetooth)
endif()
if(TARGET tal_cellular)
    list(APPEND _cloud_deps tal_cellular)
    target_link_libraries(tuya_cloud_service PUBLIC tal_cellular)
endif()
target_link_libraries(tuya_cloud_service
    PUBLIC tuya_transport libcjson tal_system libmqtt libhttp libtls common
    PRIVATE ${_cloud_deps}
)
unset(_cloud_srcs)
unset(_cloud_incs)
unset(_cloud_deps)
