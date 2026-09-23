if(NOT CONFIG_ENABLE_WIFI)
    return()
endif()
if(NOT CONFIG_ENABLE_WIFI_ULTRA_LOWPOWER)
    return()
endif()

file(GLOB _tal_wifi_ulp_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_wifi_ulp STATIC)
target_sources(tal_wifi_ulp PRIVATE ${_tal_wifi_ulp_srcs})
target_include_directories(tal_wifi_ulp
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set(_tal_wifi_ulp_deps tuyaopen-headers tal_system common tuya_cloud_service)
if(TARGET tal_wifi)
    list(APPEND _tal_wifi_ulp_deps tal_wifi)
endif()
target_link_libraries(tal_wifi_ulp PRIVATE ${_tal_wifi_ulp_deps})
unset(_tal_wifi_ulp_deps)
unset(_tal_wifi_ulp_srcs)
