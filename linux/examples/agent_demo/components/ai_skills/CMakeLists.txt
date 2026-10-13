file(GLOB _ai_skills_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_skills STATIC)
target_sources(ai_skills PRIVATE ${_ai_skills_srcs})
target_include_directories(ai_skills PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ai_skills PRIVATE tuyaopen-headers libcjson tal_system tuya_ai_service utility tuya_cloud_service)
# 音乐/故事 skill（skill_music_story.c）调 ai_audio 的播放接口：
# ENABLE_COMP_AI_AUDIO 打开时才有 ai_audio 目标（默认关，不链接）。
if(TARGET ai_audio)
    target_link_libraries(ai_skills PRIVATE ai_audio)
endif()
unset(_ai_skills_srcs)
