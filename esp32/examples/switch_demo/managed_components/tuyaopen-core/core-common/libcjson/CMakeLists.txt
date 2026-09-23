add_library(libcjson STATIC)
target_sources(libcjson PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/cJSON.c)
target_include_directories(libcjson PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cJSON>
)
target_link_libraries(libcjson PRIVATE tuyaopen-headers)
