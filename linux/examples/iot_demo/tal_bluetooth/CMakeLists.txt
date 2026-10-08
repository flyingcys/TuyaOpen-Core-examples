# tal_bluetooth（原 core-communication 模块，随应用层走；nimble 子树不搬，linux 后端走 port 的 tkl_bluetooth/BlueZ）
# 依赖映射：tal_api.h（tal_system）、tuya_cloud_types.h 为 core 头，tkl_bluetooth*.h 为 port 头，
# 均经 tuyaopen-headers（port 注入）可达；符号层面仅调 tkl_ble_*（tuyaopen-port）与
# tal_malloc/tal_free（core），在 demo 可执行文件的链接组内解析。
if(NOT CONFIG_ENABLE_BLUETOOTH)
    return()
endif()

file(GLOB _tal_bluetooth_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_bluetooth STATIC)
target_sources(tal_bluetooth PRIVATE ${_tal_bluetooth_srcs})
target_include_directories(tal_bluetooth PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# tal_api.h 聚合头在模块外不可达（依赖 tal_kv/tuya_transport/tls 的 target 内部
# 传播），src_incs 提供精简替身（见 src_incs/tal_api.h 头注）；tal_memory.h 本体
# 取自 core 的 tal_system/include。二者均为 PRIVATE，仅作用于本 target 编译。
target_include_directories(tal_bluetooth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src_incs
    ${TUYAOPEN_CORE_DIR}/iot-core/tal_system/include
)
target_link_libraries(tal_bluetooth PRIVATE tuyaopen-headers)
unset(_tal_bluetooth_srcs)
