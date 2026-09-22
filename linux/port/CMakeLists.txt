# tuyaopen-port（LINUX TKL）构建。
# 前置：先 add_subdirectory(tuyaopen-core)（开关与 tuyaopen-headers 就绪）。
# 本目录自包含：收集源码/头、按 core 开关裁剪、注入 tuyaopen-headers。
if(TARGET tuyaopen-port)
    return()
endif()

set(PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# adapter 原始布局：include/（头 + utilities 实现）+ src/（TKL 实现，含 tkl_wifi/tkl_bt）
set(_port_srcs)
file(GLOB_RECURSE _port_srcs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.c"
    "${PORT_ROOT}/src/*.c"
)
if(_port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/CMakeFiles/")
    # init/src 属 TuyaOpen auto-init 机制，本仓库无调用方，不编（其 tkl_init_system.h 依赖 IN 宏）
    list(FILTER _port_srcs EXCLUDE REGEX "/include/init/src/")
    list(REMOVE_DUPLICATES _port_srcs)
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
list(REMOVE_DUPLICATES _port_incs)

if(_port_srcs)
    add_library(tuyaopen-port STATIC)
    target_sources(tuyaopen-port PRIVATE ${_port_srcs})
    target_include_directories(tuyaopen-port PUBLIC ${_port_incs})
    if(UNIX)
        target_link_libraries(tuyaopen-port PUBLIC pthread m)
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
unset(PORT_ROOT)
