# tal_wired（原 core-communication 模块，随应用层走）
# 依赖映射：tkl_wired.h / tkl_init_wired.h / tuya_cloud_types.h 均为 port 头，
# 经 tuyaopen-headers（port 注入）可达；符号层面仅调 tkl_wired_*（tuyaopen-port），
# 在 demo 可执行文件的链接组内解析。
if(NOT CONFIG_ENABLE_WIRED)
    return()
endif()

file(GLOB _tal_wired_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_wired STATIC)
target_sources(tal_wired PRIVATE ${_tal_wired_srcs})
target_include_directories(tal_wired PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tal_wired PRIVATE tuyaopen-headers)
unset(_tal_wired_srcs)
