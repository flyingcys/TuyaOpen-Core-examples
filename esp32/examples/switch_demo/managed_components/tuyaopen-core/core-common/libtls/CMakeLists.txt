set(_libtls_incs
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port
)

file(GLOB_RECURSE _libtls_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

if(NOT CONFIG_ENABLE_PLATFORM_MBEDTLS)
    file(GLOB_RECURSE _mbedtls_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls-3.1.0/library/*.c"
    )
    list(APPEND _libtls_srcs ${_mbedtls_srcs})
    list(APPEND _libtls_incs ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls-3.1.0/include)
    unset(_mbedtls_srcs)
else()
    message(STATUS "Using mbedtls from platform")
endif()

add_library(libtls STATIC)
target_sources(libtls PRIVATE ${_libtls_srcs})
target_include_directories(libtls PUBLIC ${_libtls_incs})
if(NOT CONFIG_ENABLE_PLATFORM_MBEDTLS)
    target_include_directories(libtls PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls-3.1.0/library
    )
endif()
target_link_libraries(libtls PRIVATE tuyaopen-headers tal_system)
unset(_libtls_srcs)
unset(_libtls_incs)
