# Boolean feature flags. Host may set these before add_subdirectory().
# Values y/Y/yes/1/on and n/N/no/0/off are normalized after option().

option(CONFIG_ENABLE_WIFI "Build tal_wifi and WiFi cloud sources" OFF)
option(CONFIG_ENABLE_WIRED "Build tal_wired and wired netmgr sources" OFF)
option(CONFIG_ENABLE_BLUETOOTH "Build tal_bluetooth and BLE cloud sources" OFF)
option(CONFIG_ENABLE_NIMBLE "Include NimBLE sources in tal_bluetooth" OFF)
option(CONFIG_ENABLE_CELLULAR "Build tal_cellular and cellular netmgr sources" OFF)
option(CONFIG_ENABLE_FILE_SYSTEM "Use platform tkl_fs instead of littlefs in tal_kv" OFF)
option(CONFIG_ENABLE_PLATFORM_MBEDTLS "Use platform mbedTLS instead of the bundled copy" OFF)
option(CONFIG_ENABLE_DEVICE_TIMER "Include tuya_device_timer.c in tuya_cloud_service" OFF)
option(CONFIG_ENABLE_TUYA_P2P "Build tuya_p2p subtree" OFF)
option(CONFIG_ENABLE_LOCAL_STORE "Build tuya_p2p/local_store" OFF)
option(CONFIG_ENABLE_IPC_RING_BUFFER "Build tuya_p2p/svc_ring_buffer" OFF)
option(CONFIG_ENABLE_TUYA_CODEC "Build tuya_ai_service encoders" OFF)
option(CONFIG_ENABLE_TUYA_CODEC_OPUS "Build OPUS encoder" OFF)
option(CONFIG_ENABLE_TUYA_CODEC_OPUS_IPC "Build OPUS IPC encoder" OFF)
option(CONFIG_ENABLE_TUYA_CODEC_SPEEX "Build SPEEX encoder" OFF)
option(CONFIG_ENABLE_AI_COMPONENTS "Build the core-ai component layer on top of tuya_ai_service" OFF)
option(CONFIG_ENABLE_COMP_AI_AUDIO "Build the ai_audio component (needs CONFIG_ENABLE_AI_COMPONENTS)" OFF)
option(CONFIG_ENABLE_COMP_AI_MCP "Build the ai_mcp component (needs CONFIG_ENABLE_AI_COMPONENTS)" OFF)
option(CONFIG_ENABLE_COMP_AI_VIDEO "Build the ai_video component (needs CONFIG_ENABLE_AI_COMPONENTS)" OFF)
option(CONFIG_ENABLE_WIFI_ULTRA_LOWPOWER "Build tal_wifi_ulp (needs CONFIG_ENABLE_WIFI)" OFF)
option(CONFIG_ENABLE_QRCODE "Build the qrcode module (QR output for app binding)" OFF)
option(CONFIG_LITTLE_END "PJ little-endian (ON) vs big-endian (OFF)" ON)

if(NOT DEFINED TUYAOPEN_PORT_DIR)
    set(TUYAOPEN_PORT_DIR "")
endif()

foreach(_tuyaopen_opt
    CONFIG_ENABLE_WIFI
    CONFIG_ENABLE_WIRED
    CONFIG_ENABLE_BLUETOOTH
    CONFIG_ENABLE_NIMBLE
    CONFIG_ENABLE_CELLULAR
    CONFIG_ENABLE_FILE_SYSTEM
    CONFIG_ENABLE_PLATFORM_MBEDTLS
    CONFIG_ENABLE_DEVICE_TIMER
    CONFIG_ENABLE_TUYA_P2P
    CONFIG_ENABLE_LOCAL_STORE
    CONFIG_ENABLE_IPC_RING_BUFFER
    CONFIG_ENABLE_TUYA_CODEC
    CONFIG_ENABLE_TUYA_CODEC_OPUS
    CONFIG_ENABLE_TUYA_CODEC_OPUS_IPC
    CONFIG_ENABLE_TUYA_CODEC_SPEEX
    CONFIG_ENABLE_AI_COMPONENTS
    CONFIG_ENABLE_COMP_AI_AUDIO
    CONFIG_ENABLE_COMP_AI_MCP
    CONFIG_ENABLE_COMP_AI_VIDEO
    CONFIG_ENABLE_WIFI_ULTRA_LOWPOWER
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
