set(_tal_kv_srcs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tal_kv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kv_serialize.c
)
set(_tal_kv_public ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(_tal_kv_private ${CMAKE_CURRENT_SOURCE_DIR}/port)

if(NOT CONFIG_ENABLE_FILE_SYSTEM)
    list(APPEND _tal_kv_srcs
        ${CMAKE_CURRENT_SOURCE_DIR}/littlefs/lfs_util.c
        ${CMAKE_CURRENT_SOURCE_DIR}/littlefs/lfs.c
    )
    list(APPEND _tal_kv_public ${CMAKE_CURRENT_SOURCE_DIR}/littlefs)
endif()

add_library(tal_kv STATIC)
target_sources(tal_kv PRIVATE ${_tal_kv_srcs})
target_include_directories(tal_kv
    PUBLIC ${_tal_kv_public}
    PRIVATE ${_tal_kv_private}
)
if(NOT CONFIG_ENABLE_FILE_SYSTEM)
    target_compile_definitions(tal_kv PRIVATE LFS_CONFIG=lfs_config.h)
endif()
target_link_libraries(tal_kv PRIVATE tuyaopen-headers libcjson tal_system tal_security common)
unset(_tal_kv_srcs)
unset(_tal_kv_public)
unset(_tal_kv_private)
