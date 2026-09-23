if(NOT CONFIG_ENABLE_WIRED)
    return()
endif()

file(GLOB _tal_wired_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_wired STATIC)
target_sources(tal_wired PRIVATE ${_tal_wired_srcs})
target_include_directories(tal_wired PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tal_wired PRIVATE tuyaopen-headers)
unset(_tal_wired_srcs)
