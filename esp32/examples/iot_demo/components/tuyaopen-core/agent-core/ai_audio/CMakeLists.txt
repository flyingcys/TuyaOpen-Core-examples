# AI 音频编排（上游 agent-core/ai_audio 的播放侧移植）。
# 仅含 ai_audio_player.c：TTS 流/音乐播放编排，全部落在 svc_ai_player 接口上
# （audio_player 组件）；平台音频输出由 port 经 tuya_ai_player_set_consumer()
# 注入。输入侧 ai_audio_input.c 依赖 tdl_audio，未移植（demo 上行走 port 契约）。
if(NOT CONFIG_ENABLE_COMP_AI_AUDIO)
    return()
endif()

# ai_audio 调 svc_ai_player 的接口，播放器必须同时打开（Kconfig 侧
# ENABLE_COMP_AI_AUDIO select ENABLE_AI_PLAYER；此处兜住 -D 直配路径）。
if(NOT CONFIG_ENABLE_AI_PLAYER)
    message(FATAL_ERROR
        "CONFIG_ENABLE_COMP_AI_AUDIO=ON 但 CONFIG_ENABLE_AI_PLAYER=OFF："
        "ai_audio 依赖 audio_player 的 svc_ai_player 接口，请同时打开 "
        "ENABLE_AI_PLAYER（见 agent-core/audio_player/Kconfig）。")
endif()

file(GLOB _ai_audio_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_audio STATIC)
target_sources(ai_audio PRIVATE ${_ai_audio_srcs})
target_include_directories(ai_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# audio_player 按 PUBLIC 链接：svc_ai_player.h 出现在 ai_audio_player.h 的公共
# 接口里（AI_AUDIO_CODEC_E / AI_PLAYER_* 等类型），消费者（ai_skills/ai_agent）
# 经本目标获得其头路径。tal_api：tal_system；ai_user_event.h：utility。
target_link_libraries(ai_audio PUBLIC audio_player)
target_link_libraries(ai_audio PRIVATE tuyaopen-headers tal_system utility)
unset(_ai_audio_srcs)

# 提示音来源（Kconfig choice，默认 LOCAL）：走 Kconfig/.config 时由
# tuyaopen-core.cmake 的开关映射注入同名宏；-D 直配（两个 CONFIG_ 都未设）
# 时按 Kconfig 默认兜底为 LOCAL，避免 ai_audio_player_alert() 静默空转。
if(NOT CONFIG_AI_PLAYER_ALERT_SOURCE_LOCAL AND NOT CONFIG_AI_PLAYER_ALERT_SOURCE_CUSTOM)
    target_compile_definitions(ai_audio PRIVATE AI_PLAYER_ALERT_SOURCE_LOCAL=1)
endif()
