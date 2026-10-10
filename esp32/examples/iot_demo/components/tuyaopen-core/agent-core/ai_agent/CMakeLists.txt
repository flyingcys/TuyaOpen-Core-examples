file(GLOB _ai_agent_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_agent STATIC)
target_sources(ai_agent PRIVATE ${_ai_agent_srcs})
target_include_directories(ai_agent PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ai_agent PRIVATE tuyaopen-headers libcjson tal_system utility ai_skills tuya_cloud_service tuya_ai_service)
unset(_ai_agent_srcs)
