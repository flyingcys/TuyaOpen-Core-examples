file(GLOB _ai_main_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(ai_main STATIC)
target_sources(ai_main PRIVATE ${_ai_main_srcs})
target_include_directories(ai_main PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ai_main PRIVATE tuyaopen-headers)
unset(_ai_main_srcs)
