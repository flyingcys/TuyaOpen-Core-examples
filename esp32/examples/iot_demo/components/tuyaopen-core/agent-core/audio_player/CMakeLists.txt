# AI 播放器（上游 src/audio_player 的平台无关核心移植）。
# 平台音频输出经 AI_PLAYER_CONSUMER_T 由 port 注入（core 内默认空实现），
# 故本模块零 tkl/tdl/tdd 依赖；URL 源用 tuya_transport 的 HTTP 会话，
# 文件源用 tal_fs（平台需提供 tal_fs_ops 卡，见 datasink_file.c 注释）。
if(NOT CONFIG_ENABLE_AI_PLAYER)
    return()
endif()

# 一期解码范围：MP3（minimp3）+ WAV。OPUS / OGGOPUS 为二期（本仓暂无
# libopus / libogg 与对应解码实现）——开关打开即报错，避免"能配不能用"
# 的静默半成品（Kconfig 里两个开关的默认值均为 n）。
if(CONFIG_ENABLE_AI_PLAYER_DECODER_OPUS OR CONFIG_ENABLE_AI_PLAYER_DECODER_OGGOPUS)
    message(FATAL_ERROR
        "CONFIG_ENABLE_AI_PLAYER_DECODER_OPUS / OGGOPUS 已开，但 OPUS/OGGOPUS "
        "解码实现属二期范围（未移植）：请关闭这两个开关 "
        "（见 agent-core/audio_player/Kconfig 的 Decoder Options）。")
endif()

# 文件源（datasink_file.c）经 tal_fs 读文件，需要平台提供 tal_fs_ops 卡。
# linux port 的文件系统适配文件（port/src/ 下）当前由 CONFIG_ENABLE_TUYA_P2P
# 门控：P2P 关闭时本模块仍会引用 tal_fopen，链接期缺 tal_fs_ops_platform
# （port 侧待放宽门控；不用 AI_PLAYER_SRC_FILE 的工程可忽略此告警）。
if(NOT CONFIG_ENABLE_TUYA_P2P)
    message(WARNING "audio_player: CONFIG_ENABLE_TUYA_P2P=OFF，平台未编文件系统适配（tal_fs_ops 卡）——文件源会在链接期缺 tal_fs_ops_platform")
endif()

file(GLOB _audio_player_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/consumer/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/datasink/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/decoder/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mixer/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resample/*.c"
)

add_library(audio_player STATIC)
target_sources(audio_player PRIVATE ${_audio_player_srcs})
target_include_directories(audio_player PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# tuya_ai_service：base_event.h（ty_publish_event 发布播放状态事件）
# tuya_transport：http_session.h（URL 源）+ tal_api.h 转链的 tal_security.h
target_link_libraries(audio_player PRIVATE
    tuyaopen-headers
    tal_system
    tuya_ai_service
    tuya_transport
)

# 资源参数：.config（或宿主 -D）的值注入为编译宏；svc_ai_player.h 内有同名
# #ifndef 默认值兜底（默认与 Kconfig 一致），故未配置时行为不变。
foreach(_ap_param IN ITEMS
    AI_PLAYER_STACK_SIZE
    AI_PLAYER_FRAMEBUF_SIZE
    AI_PLAYER_DECODEBUF_SIZE
    AI_PLAYER_RINGBUF_SIZE
    AI_PLAYER_HTTP_YIELD_MS
    AI_PLAYER_HTTP_TIMEOUT_S
)
    if(DEFINED CONFIG_${_ap_param} AND NOT CONFIG_${_ap_param} STREQUAL "")
        target_compile_definitions(audio_player PRIVATE ${_ap_param}=${CONFIG_${_ap_param}})
    endif()
endforeach()
unset(_ap_param)

unset(_audio_player_srcs)
