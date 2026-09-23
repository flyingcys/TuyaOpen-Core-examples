file(GLOB _tal_system_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_system STATIC)
target_sources(tal_system PRIVATE ${_tal_system_srcs})
target_include_directories(tal_system PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# tal_api.h 伞形头引用兄弟模块头（esp-idf 的 REQUIRES 语义，需 PUBLIC 传播）
target_link_libraries(tal_system
    PUBLIC tal_network tal_kv tal_security
    PRIVATE tuyaopen-headers
)
unset(_tal_system_srcs)
