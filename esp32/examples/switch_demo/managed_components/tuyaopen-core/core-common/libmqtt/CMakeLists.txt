file(GLOB _libmqtt_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/coreMQTT/source/*.c"
)
list(APPEND _libmqtt_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/mqtt_client_wrapper.c)

add_library(libmqtt STATIC)
target_sources(libmqtt PRIVATE ${_libmqtt_srcs})
target_include_directories(libmqtt
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/coreMQTT/source/include
)
# 公共头 transport_interface.h/tuya_transporter.h 已归 tuya_transport
target_link_libraries(libmqtt
    PUBLIC tuya_transport libtls
    PRIVATE tuyaopen-headers tal_system
)
unset(_libmqtt_srcs)
