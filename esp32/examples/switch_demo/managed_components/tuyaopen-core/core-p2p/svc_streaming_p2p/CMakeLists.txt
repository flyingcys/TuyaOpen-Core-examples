file(GLOB_RECURSE _svc_streaming_p2p_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(svc_streaming_p2p STATIC)
target_sources(svc_streaming_p2p PRIVATE ${_svc_streaming_p2p_srcs})
target_include_directories(svc_streaming_p2p PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(svc_streaming_p2p PRIVATE ${_p2p_warn_opts})
target_link_libraries(svc_streaming_p2p PRIVATE tuyaopen-headers libcjson tuya_cloud_service tuya_ai_service lib_rtp base_ice tal_system tal_kv libtls common)
unset(_svc_streaming_p2p_srcs)
