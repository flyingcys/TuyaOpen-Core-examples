# AI 组件层（原 tuya_ai_service + 各 ai_* 组件）。
# 应用主流程（ai_main）/对话模式（ai_mode）/UI（ai_ui）/图片（ai_picture）不在本仓范围，
# 由应用自行组织（见 examples/agent_demo）。
if(NOT CONFIG_ENABLE_AI_COMPONENTS)
    return()
endif()

add_subdirectory(tuya_ai_service)
add_subdirectory(utility)

# 音频播放器：自带 ENABLE_AI_PLAYER 开关（见 audio_player/CMakeLists.txt）。
# 依赖 tuya_ai_service 的 base_event.h，故仍随本组件层门控（ENABLE_AI_COMPONENTS）。
# 置于 ai_audio/ai_agent/ai_skills 之前：后三者按它的 svc_ai_player 接口编排/分发播放。
add_subdirectory(audio_player)

# 音频播放编排（ai_audio_player：TTS 流/音乐播放），自带 ENABLE_COMP_AI_AUDIO
# 开关（默认关）；依赖 audio_player 的 svc_ai_player 接口。
add_subdirectory(ai_audio)

add_subdirectory(ai_agent)
add_subdirectory(ai_skills)
add_subdirectory(ai_mcp)
