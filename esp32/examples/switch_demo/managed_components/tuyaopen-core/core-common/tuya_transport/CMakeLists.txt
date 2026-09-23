# 传输抽象层：tcp / tls / websocket transporter 与 coreMQTT 传输接口，
# libhttp 与 libmqtt 的共同底层，独立成模块（同 esp-idf 的 tcp_transport/esp-tls）。

file(GLOB _tuya_transport_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(tuya_transport STATIC)
target_sources(tuya_transport PRIVATE ${_tuya_transport_srcs})
target_include_directories(tuya_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(tuya_transport PRIVATE
    tuyaopen-headers
    tal_system
    common
    libtls
)
unset(_tuya_transport_srcs)
