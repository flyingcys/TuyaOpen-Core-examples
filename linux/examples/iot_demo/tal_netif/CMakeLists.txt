# tal_netif（原 core-communication 模块，随应用层走）
# 依赖映射：tkl_netif.h / tuya_cloud_types.h 均为 port 头，
# 经 tuyaopen-headers（port 注入）可达；符号层面仅调 tkl_netif_*（tuyaopen-port），
# 在 demo 可执行文件的链接组内解析。
if(NOT CONFIG_ENABLE_WIRED)
    return()
endif()

file(GLOB _tal_netif_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_netif STATIC)
target_sources(tal_netif PRIVATE ${_tal_netif_srcs})
target_include_directories(tal_netif PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tal_netif PRIVATE tuyaopen-headers)
unset(_tal_netif_srcs)
