file(GLOB _ai_agent_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_agent STATIC)
target_sources(ai_agent PRIVATE ${_ai_agent_srcs})
target_include_directories(ai_agent PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ai_agent PRIVATE tuyaopen-headers libcjson tal_system utility ai_skills tuya_cloud_service tuya_ai_service)
# 下行 TTS 播放编排（ai_audio_play_tts_stream / ai_audio_player_stop）：
# ENABLE_COMP_AI_AUDIO 打开时才有 ai_audio 目标（默认关，不链接）。
if(TARGET ai_audio)
    target_link_libraries(ai_agent PRIVATE ai_audio)
endif()
unset(_ai_agent_srcs)
