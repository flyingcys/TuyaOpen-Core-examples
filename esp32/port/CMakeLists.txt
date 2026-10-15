# tuyaopen-port（ESP32 TKL）构建。
# 布局对齐 linux port：include/（域头）+ src/（TKL 实现）+ interface/（契约层）。
# 与 linux port 的差异：系统库/平台头（FreeRTOS/esp_wifi/lwip/nvs 等）由宿主
# 组件 components/tuyaopen-port 声明 REQUIRES 并 target_link_libraries 注入
# （idf::xxx），本文件只处理开关宏注入（core 的宏不传播到 port 目标）与
# port 头对 core 的自注入（tuyaopen-headers）。
if(TARGET tuyaopen-port)
    return()
endif()

set(PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

set(_port_srcs)
file(GLOB_RECURSE _port_srcs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.c"
    "${PORT_ROOT}/src/*.c"
)
if(_port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/CMakeFiles/")
    list(REMOVE_DUPLICATES _port_srcs)
endif()

set(_port_incs)
file(GLOB_RECURSE _port_hdrs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.h"
    "${PORT_ROOT}/src/*.h"
    "${PORT_ROOT}/interface/*.h"
)
if(_port_hdrs)
    list(FILTER _port_hdrs EXCLUDE REGEX "/CMakeFiles/")
    foreach(_port_hdr ${_port_hdrs})
        get_filename_component(_port_hdr_dir "${_port_hdr}" DIRECTORY)
        list(APPEND _port_incs "${_port_hdr_dir}")
    endforeach()
    list(REMOVE_DUPLICATES _port_incs)
endif()
if(IS_DIRECTORY "${PORT_ROOT}")
    list(APPEND _port_incs "${PORT_ROOT}")
endif()
list(REMOVE_DUPLICATES _port_incs)

if(_port_srcs)
    add_library(tuyaopen-port STATIC)
    target_sources(tuyaopen-port PRIVATE ${_port_srcs})
    target_include_directories(tuyaopen-port PUBLIC ${_port_incs})

    # port_api/port_network/port_kv/port_netif/port_bluetooth_ops/port_ai_*：
    # **零 core 依赖**——卡契约取本层 interface/（tal_api_ops.h /
    # tal_network_ops.h / tal_kv_ops.h / tal_netif_ops.h / tal_bluetooth_ops.h /
    # tal_audio_enc_ops.h / tal_vad_ops.h），公共类型取本层 include/common，
    # 日志走本层 port_log.h。刻意不链 tal_system、不注入 core 的 ble/ 头路径：
    # 一旦有人写回对 core 的 include（如 tal_log.h / tal_memory.h /
    # tal_system.h / tal_network.h），编译立即失败（编译期自证的分层门禁）。

    # 模块级隔离（对齐 linux port）：仅 p2p 使用的适配由 ENABLE_TUYA_P2P 门控。
    # core 的开关宏（TuyaOpenOptions → tuyaopen-headers）只作用于 core 模块，
    # port 目标须注入同一宏名，否则开了 P2P 也挂不上卡成员。
    if(CONFIG_ENABLE_TUYA_P2P)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_TUYA_P2P=1)
    endif()

    # 局域网/对端服务 ops（port_network 的 listen/accept/send_to/recvfrom/
    # recv_nd_size）的挂载条件：listen/accept 局域网协议、p2p、AI monitor 任一
    # 开启即需要；send_to/recvfrom 局域网协议或 p2p；recv_nd_size 仅局域网协议。
    # 三者全关时 port 无需适配这 5 个。同 P2P，逐个注入（core 的宏不传播到 port 目标）。
    if(CONFIG_ENABLE_LOCAL_LINK)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_LOCAL_LINK=1)
    endif()
    if(CONFIG_ENABLE_AI_COMPONENTS)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_AI_COMPONENTS=1)
    endif()

    # 外部 RAM（PSRAM）分配卡成员（.ex_ram_malloc/.ex_ram_free）的挂载条件：
    # 与 core 的 ENABLE_EXT_RAM 同名同开关（默认关；见 Kconfig 的 ENABLE_EXT_RAM）。
    if(CONFIG_ENABLE_EXT_RAM)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_EXT_RAM=1)
    endif()
else()
    add_library(tuyaopen-port INTERFACE)
    if(_port_incs)
        target_include_directories(tuyaopen-port INTERFACE ${_port_incs})
    endif()
    message(WARNING "tuyaopen-port has no C sources; no TKL implementation will be linked")
endif()

# port 头（含静态 tuya_kconfig.h）自注入 core 的环境基底
if(TARGET tuyaopen-headers)
    target_include_directories(tuyaopen-headers INTERFACE ${_port_incs})
endif()

unset(_port_srcs)
unset(_port_incs)
unset(_port_hdrs)
unset(_port_hdr)
unset(_port_hdr_dir)
unset(PORT_ROOT)
