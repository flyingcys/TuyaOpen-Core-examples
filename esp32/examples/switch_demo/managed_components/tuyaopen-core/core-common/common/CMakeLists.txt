file(GLOB _common_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/utilities/*.c"
)
list(APPEND _common_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/backoffAlgorithm/source/backoff_algorithm.c
)

set(_common_incs
    ${CMAKE_CURRENT_SOURCE_DIR}/utilities
    ${CMAKE_CURRENT_SOURCE_DIR}/backoffAlgorithm/source/include
)

add_library(common STATIC)
target_sources(common PRIVATE ${_common_srcs})
target_include_directories(common PUBLIC ${_common_incs})
target_link_libraries(common PRIVATE tuyaopen-headers tal_system)
unset(_common_srcs)
unset(_common_incs)
