if(NOT CONFIG_ENABLE_COMP_AI_VIDEO)
    return()
endif()

file(GLOB _ai_video_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_video STATIC)
target_sources(ai_video PRIVATE ${_ai_video_srcs})
target_include_directories(ai_video PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ai_video PRIVATE tuyaopen-headers tal_system tuya_ai_service)
unset(_ai_video_srcs)
