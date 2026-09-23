set(_ai_srcs)
set(_ai_incs)

list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/port/include)
file(GLOB_RECURSE _ai_port_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/port/src/*.c")
list(APPEND _ai_srcs ${_ai_port_srcs})
unset(_ai_port_srcs)

file(GLOB_RECURSE _ai_agent_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_agent/src/*.c")
list(APPEND _ai_srcs ${_ai_agent_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_agent/include)
unset(_ai_agent_srcs)

file(GLOB_RECURSE _ai_basic_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_basic/src/*.c")
list(APPEND _ai_srcs ${_ai_basic_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_basic/include)
unset(_ai_basic_srcs)

list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/include)
if(CONFIG_ENABLE_TUYA_CODEC)
    list(APPEND _ai_srcs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder.c)
    if(CONFIG_ENABLE_TUYA_CODEC_OPUS)
        list(APPEND _ai_srcs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_opus.c)
    endif()
    if(CONFIG_ENABLE_TUYA_CODEC_OPUS_IPC)
        list(APPEND _ai_srcs
            ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_opus_ipc.c
            ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_opus_ipc_server.c
        )
    endif()
    if(CONFIG_ENABLE_TUYA_CODEC_SPEEX)
        list(APPEND _ai_srcs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_speex.c)
    endif()
endif()

file(GLOB_RECURSE _ai_monitor_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_monitor/src/*.c")
list(APPEND _ai_srcs ${_ai_monitor_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_monitor/include)
unset(_ai_monitor_srcs)

add_library(tuya_ai_service STATIC)
target_sources(tuya_ai_service PRIVATE ${_ai_srcs})
target_include_directories(tuya_ai_service PUBLIC ${_ai_incs})
target_include_directories(tuya_ai_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/include
)
target_link_libraries(tuya_ai_service PRIVATE
    tuyaopen-headers
    libcjson
    libhttp
    libmqtt
    libtls
    common
    tal_system
    tal_network
    tal_kv
    tal_security
    tuya_cloud_service
)
unset(_ai_srcs)
unset(_ai_incs)
