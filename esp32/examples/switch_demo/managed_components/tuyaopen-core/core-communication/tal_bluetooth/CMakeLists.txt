if(NOT CONFIG_ENABLE_BLUETOOTH)
    return()
endif()

file(GLOB_RECURSE _tal_bluetooth_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set(_tal_bluetooth_public ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(_tal_bluetooth_private)

if(CONFIG_ENABLE_NIMBLE)
    file(GLOB_RECURSE _nimble_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/nimble/*.c"
    )
    list(APPEND _tal_bluetooth_srcs ${_nimble_srcs})
    list(APPEND _tal_bluetooth_public ${CMAKE_CURRENT_SOURCE_DIR}/nimble/include)
    set(_tal_bluetooth_private ${CMAKE_CURRENT_SOURCE_DIR}/nimble/host)
    unset(_nimble_srcs)
endif()

add_library(tal_bluetooth STATIC)
target_sources(tal_bluetooth PRIVATE ${_tal_bluetooth_srcs})
target_include_directories(tal_bluetooth PUBLIC ${_tal_bluetooth_public})
if(_tal_bluetooth_private)
    target_include_directories(tal_bluetooth PRIVATE ${_tal_bluetooth_private})
endif()
target_link_libraries(tal_bluetooth PRIVATE tuyaopen-headers tal_system)
unset(_tal_bluetooth_srcs)
unset(_tal_bluetooth_public)
unset(_tal_bluetooth_private)
