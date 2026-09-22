# tuyaopen-port（ESP32 TKL）构建。
# 前置：先 add_subdirectory(tuyaopen-core)（开关与 tuyaopen-headers 就绪）。
# 系统库（pthread 等）由 esp-idf 组件体系提供，此处不链接。
if(TARGET tuyaopen-port)
    return()
endif()

set(PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(PORT_CHIP_ROOT "${PORT_ROOT}")

set(_port_srcs)
file(GLOB_RECURSE _port_srcs CONFIGURE_DEPENDS
    "${PORT_ROOT}/include/*.c"
    "${PORT_CHIP_ROOT}/*.c"
)
if(_port_srcs)
    list(FILTER _port_srcs EXCLUDE REGEX "/CMakeFiles/")
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
    "${PORT_CHIP_ROOT}/*.h"
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
else()
    add_library(tuyaopen-port INTERFACE)
    if(_port_incs)
        target_include_directories(tuyaopen-port INTERFACE ${_port_incs})
    endif()
    message(WARNING "tuyaopen-port has no C sources yet; fill in the ESP32 TKL implementation")
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
unset(PORT_CHIP_ROOT)
