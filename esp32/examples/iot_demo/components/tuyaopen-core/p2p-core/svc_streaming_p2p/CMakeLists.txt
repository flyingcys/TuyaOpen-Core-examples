file(GLOB_RECURSE _svc_streaming_p2p_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(svc_streaming_p2p STATIC)
target_sources(svc_streaming_p2p PRIVATE ${_svc_streaming_p2p_srcs})
target_include_directories(svc_streaming_p2p PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(svc_streaming_p2p PRIVATE ${_p2p_warn_opts})
target_link_libraries(svc_streaming_p2p PRIVATE tuyaopen-headers libcjson tuya_cloud_service lib_rtp base_ice tal_system tuya_transport)
# tuya_ai_service 条件化：本模块源码对它的符号零引用（实测 grep 无命中），而该 target
# 受 CONFIG_ENABLE_AI_COMPONENTS 门控（agent-core/CMakeLists.txt 首行 return）——
# 无条件链会在 AI=OFF 时以 "cannot find -ltuya_ai_service" 链接失败（P2P 与 AI 开关
# 未联动，属既有欠账）。存在则链，供将来真正引用时使用。
if(TARGET tuya_ai_service)
    target_link_libraries(svc_streaming_p2p PRIVATE tuya_ai_service)
endif()
unset(_svc_streaming_p2p_srcs)
