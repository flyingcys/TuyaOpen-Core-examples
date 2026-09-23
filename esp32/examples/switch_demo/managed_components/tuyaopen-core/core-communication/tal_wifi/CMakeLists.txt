if(NOT CONFIG_ENABLE_WIFI)
    return()
endif()

file(GLOB _tal_wifi_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_wifi STATIC)
target_sources(tal_wifi PRIVATE ${_tal_wifi_srcs})
target_include_directories(tal_wifi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tal_wifi PRIVATE tuyaopen-headers tal_system)
unset(_tal_wifi_srcs)
