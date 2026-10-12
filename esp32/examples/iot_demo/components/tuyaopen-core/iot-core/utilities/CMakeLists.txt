# 通用工具层：平台无关的纯 C 工具（链表/内存堆/slist/tools/crc/random/
# mix_method/dev_evt/queue_template）。**零自研依赖**——只依赖 tuyaopen-headers
# （port 头基底）与 third_party（mbedtls base64）；由 tal_system PUBLIC 依赖，
# 消费者自动可达。
file(GLOB_RECURSE _utilities_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(tuya_utilities STATIC)
target_sources(tuya_utilities PRIVATE ${_utilities_srcs})
target_include_directories(tuya_utilities PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(tuya_utilities PRIVATE tuyaopen-headers)
if(TARGET mbedtls)
    target_link_libraries(tuya_utilities PRIVATE mbedtls)
endif()
unset(_utilities_srcs)
