# tuyaopen-port 包装组件：引入本仓库的 esp32/port（自包含 plain CMake 子目录）。
# REQUIRES tuyaopen-core 保证 core 组件先处理（tuyaopen-headers 就绪后 port 的自注入才生效）。
idf_component_register(SRCS "" INCLUDE_DIRS "" REQUIRES tuyaopen-core)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../port
                 ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-port-build)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-port)
