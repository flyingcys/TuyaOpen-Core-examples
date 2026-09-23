# TAL_CELLULAR_BASE_CFG_T reaches for TUYA_CELLULAR_IF_E / TUYA_CELLULAR_PROTOCOL_E,
# which only the platforms carrying a modem declare; gated the same way as
# netconn_cellular.c in tuya_cloud_service - its only consumer.
if(NOT CONFIG_ENABLE_CELLULAR)
    return()
endif()

file(GLOB _tal_cellular_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_cellular STATIC)
target_sources(tal_cellular PRIVATE ${_tal_cellular_srcs})
target_include_directories(tal_cellular PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tal_cellular PRIVATE tuyaopen-headers)
unset(_tal_cellular_srcs)
