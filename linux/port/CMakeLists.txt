# tuyaopen-port（LINUX TKL）构建。
# 前置：先 add_subdirectory(tuyaopen-core)（开关与 tuyaopen-headers 就绪）。
# 本目录自包含：收集源码/头、按 core 开关裁剪、注入 tuyaopen-headers。
if(TARGET tuyaopen-port)
    return()
endif()

set(PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# adapter 布局：include/（头）+ src/（TKL 实现，含 port_bt）；
# 通用工具（tuya_list/queue/mem_heap/ringbuf/slist/tools）已迁至
# tuyaopen-core/iot-core/tuya_transport/src/utilities（core 共用，平台无关）
set(_port_srcs)
file(GLOB_RECURSE _port_srcs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.c"
    "${PORT_ROOT}/src/*.c"
)
if(_port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/CMakeFiles/")
    # bluez_inc 的 examples/ 是第三方示例（central/main.c、peripheral/peripheral.c 各带
    # int main()），BT=ON 时 GLOB_RECURSE 会把它们收进 port 静态库，必须排除
    list(FILTER _port_srcs EXCLUDE REGEX "/bluez_inc/examples/")
    list(REMOVE_DUPLICATES _port_srcs)
endif()

# littlefs（tal_kv 的存储引擎，port_kv.c 直接用）：不在 include/ 或 src/ 下，
# GLOB 收不到，显式收集
if(EXISTS "${PORT_ROOT}/littlefs/lfs.c")
    list(APPEND _port_srcs
        "${PORT_ROOT}/littlefs/lfs_util.c"
        "${PORT_ROOT}/littlefs/lfs.c"
    )
    # lfs_util.h 经 LFS_CONFIG 展开 lfs_config.h（在 include/system/ 下，见下面的
    # _port_incs 注入）；其 lfs_malloc / LFS_ERROR 依赖 tal_malloc / PR_ERR
    set(_port_lfs_defs LFS_CONFIG=lfs_config.h)
endif()

# 路径与 src 下目录名耦合：port 去 tkl 化改名（tkl_bt→port_bt）后须同步，
# 否则 BLE=OFF 时该目录不会被排除而被误编入
if(NOT CONFIG_ENABLE_BLUETOOTH AND _port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/port_bt/")
endif()

set(_port_incs)
file(GLOB_RECURSE _port_hdrs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.h"
    "${PORT_ROOT}/src/*.h"
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
# littlefs 头（lfs.h/lfs_util.h）：port_kv.c 直接 include "lfs.h"
if(IS_DIRECTORY "${PORT_ROOT}/littlefs")
    list(APPEND _port_incs "${PORT_ROOT}/littlefs")
endif()
list(REMOVE_DUPLICATES _port_incs)

if(_port_srcs)
    add_library(tuyaopen-port STATIC)
    target_sources(tuyaopen-port PRIVATE ${_port_srcs})
    target_include_directories(tuyaopen-port PUBLIC ${_port_incs})
    if(_port_lfs_defs)
        target_compile_definitions(tuyaopen-port PRIVATE ${_port_lfs_defs})
    endif()
    if(UNIX)
        target_link_libraries(tuyaopen-port PUBLIC pthread m)
    endif()

    # 路径与 src 下文件名耦合：port 去 tkl 化改名后须同步，否则条件为假会
    # 静默丢掉 tal_system 链接与 core transport 头注入（表面能过增量构建）
    if(EXISTS "${PORT_ROOT}/src/port_network.c" OR EXISTS "${PORT_ROOT}/src/port_runtime.c")
        # port_network（POSIX 实现）/port_runtime（tal_api ops 卡）：include tal_api.h（tal_system 公共头）与
        # core 的 tal_network.h/tal_api_ops.h（卡与 ops 类型），符号在最终链接组解析
        if(TARGET tal_system)
            target_link_libraries(tuyaopen-port PRIVATE tal_system)
        endif()
        target_include_directories(tuyaopen-port PRIVATE
            "${PORT_ROOT}/../tuyaopen-core/iot-core/tuya_transport/include/transport")
    endif()

    # 模块级隔离（独立于上面的条件块）：仅 p2p 使用的适配由 ENABLE_TUYA_P2P 门控
    # ——port_network 的 5 个 p2p 成员 + port_fs 整文件。core 的开关宏
    # （TuyaOpenOptions → tuyaopen-headers）只作用于 core 模块，port 目标须注入
    # 同一宏名，否则开了 P2P 也挂不上卡成员/编不出 port_fs。
    if(CONFIG_ENABLE_TUYA_P2P)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_TUYA_P2P=1)
    endif()

    # 局域网/对端服务 ops（port_network 的 listen/accept/send_to/recvfrom/recv_nd_size）
    # 的挂载条件：listen/accept 局域网协议、p2p、AI monitor 任一开启即需要；
    # send_to/recvfrom 局域网协议或 p2p；recv_nd_size 仅局域网协议。三者全关时
    # port 无需适配这 5 个。同 P2P，逐个注入（core 的宏不传播到 port 目标）。
    if(CONFIG_ENABLE_LOCAL_LINK)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_LOCAL_LINK=1)
    endif()
    if(CONFIG_ENABLE_AI_COMPONENTS)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_AI_COMPONENTS=1)
    endif()

    # AI 播放器消费端（port_ai_player_consumer.c）：把 svc_ai_player 解码后的 PCM
    # 写进 ALSA（16k/16bit/mono）。core 的契约头 svc_ai_player.h 在
    # agent-core/audio_player/include，需补进包含路径；libasound 经 pkg-config
    # 探测（写法同 BT 的 GIO/DBUS）。
    # 同一宏还放行 port_fs.c 的门控（见文件头：P2P || AI_PLAYER）——AI 播放器的
    # 文件源（datasink_file.c → tal_fopen）会把 core 的 tal_fs.o 拉进链接，
    # 没有 P2P 也必须提供 tal_fs_ops 卡。
    if(CONFIG_ENABLE_AI_PLAYER)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_AI_PLAYER=1)
        target_include_directories(tuyaopen-port PRIVATE
            "${PORT_ROOT}/../tuyaopen-core/agent-core/audio_player/include")
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(ALSA alsa QUIET)
        endif()
        if(ALSA_FOUND)
            target_include_directories(tuyaopen-port PRIVATE ${ALSA_INCLUDE_DIRS})
            target_link_libraries(tuyaopen-port PRIVATE ${ALSA_LIBRARIES})
        else()
            # 无 pkg-config 的兜底：直接按库名链接（libasound2-dev 提供）
            target_link_libraries(tuyaopen-port PRIVATE asound)
        endif()
    endif()

    # 路径与 src 下文件名/目录名耦合：port 去 tkl 化改名后须同步
    if(EXISTS "${PORT_ROOT}/src/port_kv.c")
        # port_kv（littlefs + AES）：include tal_log/tal_memory/tal_mutex（tal_system）
        # 与 tal_security（tuya_transport，随 tal_system 的 PUBLIC 依赖传播）
        if(TARGET tal_system)
            target_link_libraries(tuyaopen-port PRIVATE tal_system)
        endif()
    endif()

    if(EXISTS "${PORT_ROOT}/src/port_bt" AND CONFIG_ENABLE_BLUETOOTH)
        # port_bt sources include tal_log.h（tal_system 公共头），符号在最终链接组解析
        if(TARGET tal_system)
            target_link_libraries(tuyaopen-port PRIVATE tal_system)
        endif()
        # tal_bluetooth_ops.c（TAL BLE 卡适配层）include core 的
        # tal_bluetooth_ops.h（tal_system/include），该头内嵌 include
        # "tal_bluetooth_def.h"（在 core 的 ble/ 下），需把 ble/ 补进包含路径
        target_include_directories(tuyaopen-port PRIVATE
            "${PORT_ROOT}/../tuyaopen-core/iot-core/ble")
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GIO gio-2.0 QUIET)
            pkg_check_modules(DBUS dbus-1 QUIET)
        endif()
        if(GIO_FOUND)
            # binc speaks GDBus, which lives in libgio (pulls gobject/glib).
            target_include_directories(tuyaopen-port PRIVATE ${GIO_INCLUDE_DIRS})
            target_link_libraries(tuyaopen-port PRIVATE ${GIO_LIBRARIES})
        else()
            target_include_directories(tuyaopen-port PRIVATE
                /usr/include/glib-2.0
                /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/glib-2.0/include
            )
            target_link_libraries(tuyaopen-port PRIVATE gio-2.0 gobject-2.0 glib-2.0)
        endif()
        if(DBUS_FOUND)
            target_include_directories(tuyaopen-port PRIVATE ${DBUS_INCLUDE_DIRS})
            target_link_libraries(tuyaopen-port PRIVATE ${DBUS_LIBRARIES})
        endif()
        target_link_libraries(tuyaopen-port PRIVATE bluetooth)
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
unset(_port_lfs_defs)
unset(PORT_ROOT)
