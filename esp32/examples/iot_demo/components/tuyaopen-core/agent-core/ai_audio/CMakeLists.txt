if(NOT CONFIG_ENABLE_COMP_AI_AUDIO)
    return()
endif()

file(GLOB _ai_audio_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_audio STATIC)
target_sources(ai_audio PRIVATE ${_ai_audio_srcs})
target_include_directories(ai_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ai_audio PRIVATE tuyaopen-headers tal_system utility ai_agent tuya_ai_service)
unset(_ai_audio_srcs)
