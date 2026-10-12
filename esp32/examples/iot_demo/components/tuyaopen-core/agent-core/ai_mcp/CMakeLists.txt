if(NOT CONFIG_ENABLE_COMP_AI_MCP)
    return()
endif()

file(GLOB _ai_mcp_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_mcp STATIC)
target_sources(ai_mcp PRIVATE ${_ai_mcp_srcs})
target_include_directories(ai_mcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# tuya_cloud_service：tools 里 device_info_get 读 tuya_iot 客户端的真实 devid
# （tuya_iot_client_get / tuya_iot_devid_get，头文件在 tuya_cloud_service 的 PUBLIC 路径）。
# 依赖方向单向（tuya_cloud_service 不依赖 agent-core），无循环。
target_link_libraries(ai_mcp PRIVATE tuyaopen-headers tal_system ai_agent tuya_ai_service tuya_cloud_service)
unset(_ai_mcp_srcs)
