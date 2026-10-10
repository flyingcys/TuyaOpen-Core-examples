# 仅作 esp-idf 组件入口：component manager 经 idf_component.yml 的 git 依赖
# 拉取本仓库时，从根目录注册组件。普通 CMake 宿主请 include(tuyaopen-core.cmake)，
# 不要使用本文件（idf_component_register 仅在 esp-idf 构建中可用）。
#
# 【ESP32 平台差异文件】（同步时排除，见 esp32/README.md）：REQUIRES mbedtls
# ——保证 esp-idf 的 mbedtls 组件先于本组件处理，core 模块里
# target_link_libraries(... mbedtls ...) 的裸名在组件处理期即解析到 esp-idf
# 的 mbedtls 库目标（其 MBEDTLS_CONFIG_FILE 指向的 esp_config.h 路径由
# tuyaopen-port 注入 core 环境基底）。core 仓库版本无此行（保持芯片无关），
# 其余内容与上游一致。
idf_component_register(SRCS "" INCLUDE_DIRS "" REQUIRES mbedtls)

include(${CMAKE_CURRENT_LIST_DIR}/tuyaopen-core.cmake)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-core)
