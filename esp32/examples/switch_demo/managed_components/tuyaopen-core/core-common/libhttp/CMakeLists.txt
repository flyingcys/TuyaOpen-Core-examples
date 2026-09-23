set(_libhttp_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_client_wrapper.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_download.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/core_http_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/http_parser/http_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_session.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_site.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http_captive.c
)

set(_libhttp_incs
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/include
    ${CMAKE_CURRENT_SOURCE_DIR}/coreHTTP/source/dependency/3rdparty/http_parser
)

add_library(libhttp STATIC)
target_sources(libhttp PRIVATE ${_libhttp_srcs})
target_include_directories(libhttp PUBLIC ${_libhttp_incs})
target_compile_options(libhttp PRIVATE -w)
# core_http_client.h（公共头）引 transport_interface.h（tuya_transport），需 PUBLIC 传播
target_link_libraries(libhttp
    PUBLIC tuya_transport
    PRIVATE tuyaopen-headers tal_system tal_network common libtls
)
unset(_libhttp_srcs)
unset(_libhttp_incs)
