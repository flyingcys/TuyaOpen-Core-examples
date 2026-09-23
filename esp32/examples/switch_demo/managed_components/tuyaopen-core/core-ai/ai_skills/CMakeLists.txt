file(GLOB _ai_skills_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_skills STATIC)
target_sources(ai_skills PRIVATE ${_ai_skills_srcs})
target_include_directories(ai_skills PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ai_skills PRIVATE tuyaopen-headers libcjson tal_system common tuya_ai_service)
unset(_ai_skills_srcs)
