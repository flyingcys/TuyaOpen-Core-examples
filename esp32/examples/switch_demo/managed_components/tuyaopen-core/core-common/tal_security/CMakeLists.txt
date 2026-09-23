file(GLOB_RECURSE _tal_security_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_security STATIC)
target_sources(tal_security PRIVATE ${_tal_security_srcs})
target_include_directories(tal_security PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# tal_asymmetrical.h 引 tuya_tls.h（libtls 公共头），需 PUBLIC 传播
target_link_libraries(tal_security
    PUBLIC libtls
    PRIVATE tuyaopen-headers tal_system
)
unset(_tal_security_srcs)
