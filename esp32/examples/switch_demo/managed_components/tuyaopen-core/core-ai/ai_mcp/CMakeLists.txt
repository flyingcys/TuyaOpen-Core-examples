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
target_link_libraries(ai_mcp PRIVATE tuyaopen-headers tal_system common ai_agent tuya_ai_service)
unset(_ai_mcp_srcs)
