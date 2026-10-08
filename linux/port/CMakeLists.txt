# tuyaopen-port（LINUX TKL）构建。
# 前置：先 add_subdirectory(tuyaopen-core)（开关与 tuyaopen-headers 就绪）。
# 本目录自包含：收集源码/头、按 core 开关裁剪、注入 tuyaopen-headers。
if(TARGET tuyaopen-port)
    return()
endif()

set(PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# adapter 布局：include/（头）+ src/（TKL 实现，含 tkl_wifi/tkl_bt）；
# 通用工具（tuya_list/queue/mem_heap/ringbuf/slist/tools）已迁至
# tuyaopen-core/iot-core/tuya_transport/src/utilities（core 共用，平台无关）
set(_port_srcs)
file(GLOB_RECURSE _port_srcs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.c"
    "${PORT_ROOT}/src/*.c"
)
if(_port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/CMakeFiles/")
    list(REMOVE_DUPLICATES _port_srcs)
endif()

# littlefs（tal_kv 的存储引擎，tkl_kv.c 直接用）：不在 include/ 或 src/ 下，
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

if(NOT CONFIG_ENABLE_WIFI AND _port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/tkl_wifi/")
endif()
if(NOT CONFIG_ENABLE_BLUETOOTH AND _port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/tkl_bt/")
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
# littlefs 头（lfs.h/lfs_util.h）：tkl_kv.c 直接 include "lfs.h"
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

    if(EXISTS "${PORT_ROOT}/src/tkl_network.c" OR EXISTS "${PORT_ROOT}/src/tkl_runtime.c")
        # tkl_network（POSIX 实现）/tkl_runtime（tal_api ops 卡）：include tal_api.h（tal_system 公共头）与
        # core 的 tal_network.h/tal_api_ops.h（卡与 ops 类型），符号在最终链接组解析
        if(TARGET tal_system)
            target_link_libraries(tuyaopen-port PRIVATE tal_system)
        endif()
        target_include_directories(tuyaopen-port PRIVATE
            "${PORT_ROOT}/../tuyaopen-core/iot-core/tuya_transport/include/transport")
    endif()

    # 模块级隔离（独立于上面的条件块）：仅 p2p 使用的适配由 ENABLE_TUYA_P2P 门控
    # ——tkl_network 的 5 个 p2p 成员 + tkl_fs 整文件。core 的开关宏
    # （TuyaOpenOptions → tuyaopen-headers）只作用于 core 模块，port 目标须注入
    # 同一宏名，否则开了 P2P 也挂不上卡成员/编不出 tkl_fs。
    if(CONFIG_ENABLE_TUYA_P2P)
        target_compile_definitions(tuyaopen-port PRIVATE ENABLE_TUYA_P2P=1)
    endif()

    if(EXISTS "${PORT_ROOT}/src/tkl_kv.c")
        # tkl_kv（littlefs + AES）：include tal_log/tal_memory/tal_mutex（tal_system）
        # 与 tal_security（tuya_transport，随 tal_system 的 PUBLIC 依赖传播）
        if(TARGET tal_system)
            target_link_libraries(tuyaopen-port PRIVATE tal_system)
        endif()
    endif()

    if(EXISTS "${PORT_ROOT}/src/tkl_bt" AND CONFIG_ENABLE_BLUETOOTH)
        # tkl_bt sources include tal_log.h（tal_system 公共头），符号在最终链接组解析
        if(TARGET tal_system)
            target_link_libraries(tuyaopen-port PRIVATE tal_system)
        endif()
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
