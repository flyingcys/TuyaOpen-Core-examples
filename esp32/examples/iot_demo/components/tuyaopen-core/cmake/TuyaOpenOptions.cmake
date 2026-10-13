# Boolean feature flags. Host may set these before add_subdirectory().
# Values y/Y/yes/1/on and n/N/no/0/off are normalized after option().

option(CONFIG_ENABLE_WIRED "Build wired netmgr sources (tal_netif: local IP / link callback)" OFF)
option(CONFIG_ENABLE_BLUETOOTH "Build tal_bluetooth and BLE cloud sources" OFF)
option(CONFIG_ENABLE_NIMBLE "Include NimBLE sources in tal_bluetooth" OFF)
option(CONFIG_ENABLE_CELLULAR "Build tal_cellular and cellular netmgr sources" OFF)
option(CONFIG_ENABLE_PLATFORM_MBEDTLS "Use platform mbedTLS instead of the bundled copy" OFF)
option(CONFIG_ENABLE_DEVICE_TIMER "Include tuya_device_timer.c in tuya_cloud_service" OFF)
option(CONFIG_ENABLE_TUYA_P2P "Build tuya_p2p subtree" OFF)
# 默认 ON：lan/tuya_lan.c 原先无条件编入，默认保持行为不变；关闭后局域网协议
# 不编译，port 也无需适配局域网相关 net ops（accept/listen/send_to/recvfrom/recv_nd_size）。
# lan/lan_sock.c（socket 事件循环）不随之关闭：AI monitor 复用它。
option(CONFIG_ENABLE_LOCAL_LINK "Build the LAN (local link) service: lan/tuya_lan.c" ON)
option(CONFIG_ENABLE_LOCAL_STORE "Build tuya_p2p/local_store" OFF)
option(CONFIG_ENABLE_IPC_RING_BUFFER "Build tuya_p2p/svc_ring_buffer" OFF)
option(CONFIG_ENABLE_TUYA_CODEC "Build tuya_ai_service encoders" OFF)
option(CONFIG_ENABLE_TUYA_CODEC_OPUS "Build OPUS encoder" OFF)
option(CONFIG_ENABLE_TUYA_CODEC_OPUS_IPC "Build OPUS IPC encoder" OFF)
option(CONFIG_ENABLE_TUYA_CODEC_SPEEX "Build SPEEX encoder" OFF)
option(CONFIG_ENABLE_AI_COMPONENTS "Build the agent-core component layer on top of tuya_ai_service" OFF)
option(CONFIG_ENABLE_COMP_AI_MCP "Build the ai_mcp component (needs CONFIG_ENABLE_AI_COMPONENTS)" OFF)
# AI 音频播放编排（agent-core/ai_audio）：依赖 audio_player 的 svc_ai_player 接口，
# 需 CONFIG_ENABLE_AI_COMPONENTS + CONFIG_ENABLE_AI_PLAYER（Kconfig 侧 select）
option(CONFIG_ENABLE_COMP_AI_AUDIO "Build the ai_audio component (needs CONFIG_ENABLE_AI_COMPONENTS + CONFIG_ENABLE_AI_PLAYER)" OFF)
# 提示音来源（Kconfig choice，默认 LOCAL）：本地素材一期未移植（alert 降级为告警跳过）
option(CONFIG_AI_PLAYER_ALERT_SOURCE_LOCAL "ai_audio alert source: local assets (not ported in phase 1)" OFF)
option(CONFIG_AI_PLAYER_ALERT_SOURCE_CUSTOM "ai_audio alert source: custom callback registered by the app" OFF)
# AI 播放器（agent-core/audio_player）：依赖 base_event.h，随 ENABLE_AI_COMPONENTS 门控；
# OPUS/OGGOPUS 解码器开关为二期范围，打开会在 CMake 阶段报错（见其 CMakeLists.txt）
option(CONFIG_ENABLE_AI_PLAYER "Build the audio_player module (needs CONFIG_ENABLE_AI_COMPONENTS)" OFF)
option(CONFIG_ENABLE_AI_PLAYER_DECODER_OPUS "Enable the OPUS decoder of audio_player (phase 2, not ported)" OFF)
option(CONFIG_ENABLE_AI_PLAYER_DECODER_OGGOPUS "Enable the OGGOPUS decoder of audio_player (phase 2, not ported)" OFF)
option(CONFIG_ENABLE_QRCODE "Build the qrcode module (QR output for app binding)" OFF)
option(CONFIG_LITTLE_END "PJ little-endian (ON) vs big-endian (OFF)" ON)

if(NOT DEFINED TUYAOPEN_PORT_DIR)
    set(TUYAOPEN_PORT_DIR "")
endif()

foreach(_tuyaopen_opt
    CONFIG_ENABLE_WIRED
    CONFIG_ENABLE_BLUETOOTH
    CONFIG_ENABLE_NIMBLE
    CONFIG_ENABLE_CELLULAR
    CONFIG_ENABLE_PLATFORM_MBEDTLS
    CONFIG_ENABLE_DEVICE_TIMER
    CONFIG_ENABLE_TUYA_P2P
    CONFIG_ENABLE_LOCAL_LINK
    CONFIG_ENABLE_LOCAL_STORE
    CONFIG_ENABLE_IPC_RING_BUFFER
    CONFIG_ENABLE_TUYA_CODEC
    CONFIG_ENABLE_TUYA_CODEC_OPUS
    CONFIG_ENABLE_TUYA_CODEC_OPUS_IPC
    CONFIG_ENABLE_TUYA_CODEC_SPEEX
    CONFIG_ENABLE_AI_COMPONENTS
    CONFIG_ENABLE_COMP_AI_MCP
    CONFIG_ENABLE_COMP_AI_AUDIO
    CONFIG_AI_PLAYER_ALERT_SOURCE_LOCAL
    CONFIG_AI_PLAYER_ALERT_SOURCE_CUSTOM
    CONFIG_ENABLE_AI_PLAYER
    CONFIG_ENABLE_AI_PLAYER_DECODER_OPUS
    CONFIG_ENABLE_AI_PLAYER_DECODER_OGGOPUS
    CONFIG_ENABLE_QRCODE
    CONFIG_LITTLE_END
)
    if(DEFINED ${_tuyaopen_opt})
        string(TOLOWER "${${_tuyaopen_opt}}" _tuyaopen_val)
        if(_tuyaopen_val STREQUAL "y" OR _tuyaopen_val STREQUAL "yes" OR
           _tuyaopen_val STREQUAL "1" OR _tuyaopen_val STREQUAL "on")
            set(${_tuyaopen_opt} ON)
        elseif(_tuyaopen_val STREQUAL "n" OR _tuyaopen_val STREQUAL "no" OR
               _tuyaopen_val STREQUAL "0" OR _tuyaopen_val STREQUAL "off")
            set(${_tuyaopen_opt} OFF)
        endif()
    endif()
endforeach()
unset(_tuyaopen_opt)
unset(_tuyaopen_val)

# Cellular needs a modem-capable platform (the TKL has no tkl_init_cellular.h
# and no TUYA_CELLULAR_* declarations); keep it OFF on Linux until the TKL
# grows cellular support.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CONFIG_ENABLE_CELLULAR)
    set(CONFIG_ENABLE_CELLULAR OFF)
    message(WARNING "CONFIG_ENABLE_CELLULAR is forced OFF on Linux: the TKL has no cellular support yet")
endif()
