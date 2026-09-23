# tuyaopen-port 包装组件：引入本仓库的 esp32/port（自包含子目录）。
# REQUIRES 保证处理顺序与 TKL 实现所需的 esp-idf 组件。
idf_component_register(
    SRCS "" INCLUDE_DIRS ""
    REQUIRES tuyaopen-core esp_wifi esp_event esp_netif nvs_flash lwip efuse esp_pm
)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../port
                 ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-port-build)

# TKL 源码（plain 库）需要 esp-idf 组件头，经目标链接注入
target_link_libraries(tuyaopen-port PRIVATE
    idf::esp_wifi idf::esp_event idf::esp_netif idf::nvs_flash
    idf::lwip idf::efuse idf::esp_pm idf::fatfs idf::sdmmc idf::app_update)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-port)
