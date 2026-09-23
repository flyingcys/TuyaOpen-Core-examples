file(GLOB _tal_network_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_network STATIC)
target_sources(tal_network PRIVATE ${_tal_network_srcs})
target_include_directories(tal_network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tal_network PRIVATE tuyaopen-headers common tal_system)
unset(_tal_network_srcs)
