# 仅作 esp-idf 组件入口：component manager 经 idf_component.yml 的 git 依赖
# 拉取本仓库时，从根目录注册组件。普通 CMake 宿主请 include(tuyaopen-core.cmake)，
# 不要使用本文件（idf_component_register 仅在 esp-idf 构建中可用）。
# mbedtls：tal_security 等公共头 include mbedtls/*.h（esp-idf 的 mbedtls 组件头路径）
idf_component_register(SRCS "" INCLUDE_DIRS "" REQUIRES mbedtls)

include(${CMAKE_CURRENT_LIST_DIR}/tuyaopen-core.cmake)

# esp-idf 组件头直达 core 内部的 plain 库目标（tal_security 等公共头 include
# mbedtls/*.h）——经环境基底 tuyaopen-headers 分发（core 模块统一 PRIVATE 它）
target_link_libraries(tuyaopen-headers INTERFACE idf::mbedtls)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-core)
