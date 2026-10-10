# tuyaopen-port 包装组件：引入本仓库的 esp32/port（自包含子目录）。
# REQUIRES 保证处理顺序（tuyaopen-core 先于本组件）与 TKL 实现所需的
# esp-idf 组件可用；idf::xxx 链接把 esp-idf 组件头注入 plain CMake 的
# tuyaopen-port 目标（普通 add_subdirectory 目标不自动获得 idf 组件头）。
idf_component_register(
    SRCS "" INCLUDE_DIRS ""
    REQUIRES tuyaopen-core esp_wifi esp_event esp_netif nvs_flash lwip esp_timer app_update
)

# ---- esp-idf 宿主下 core 的 mbedtls 接入（平台定制只进 port，core 零改动）----
#
# 1) esp_config.h：core 模块里 target_link_libraries(... mbedtls ...) 的裸名
#    在 esp-idf 构建中解析到 esp-idf 的 mbedtls 库目标（mbedtls 组件内
#    add_subdirectory(mbedtls) 创建，同名上游目标）——它带 MBEDTLS_CONFIG_FILE
#    定义，但缺 esp_config.h 所在目录（该目录挂在组件目标 idf::mbedtls 上）。
#    给 core 的环境基底（tuyaopen-headers，core 模块统一 PRIVATE 依赖）补该
#    include，覆盖 core 全部编译单元。
# 2) threading_alt.h：core 的 tuya_tls.c 用 tal_mutex 实现 mbedtls 的
#    THREADING_ALT 回调，需要 tal_mutex 版结构（port/include/common 下，
#    随 port 头注入 core 环境基底）；esp-idf 的 mbedtls 库/组件自身用 ROM 版
#    （由 main 组件注入 mbedtls 目标）——两套结构各归各的分配者，互不冲突。
#    （core 的 config/threading_alt.h 在 esp32 侧同步排除，避免经 mbedtls
#    库目标的 INTERFACE 注入覆盖 ROM 版。）
# 3) -Wno-error=format：core 代码有 %u 配 uint32_t 的写法，esp32 上 uint32_t
#    为 long unsigned（esp-idf 默认 -Werror=format 报错）；32 位平台同宽无
#    实际危害，放行警告（建议 core 侧后续改用 PRIu32）。
if(TARGET tuyaopen-headers AND TARGET idf::mbedtls)
    target_include_directories(tuyaopen-headers INTERFACE
        $ENV{IDF_PATH}/components/mbedtls/port/include)
    target_compile_options(tuyaopen-headers INTERFACE -Wno-error=format)
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../port
                 ${CMAKE_CURRENT_BINARY_DIR}/tuyaopen-port-build)

# TKL 源码（plain 库）需要 esp-idf 组件头，经目标链接注入
target_link_libraries(tuyaopen-port PRIVATE
    idf::esp_wifi idf::esp_event idf::esp_netif idf::nvs_flash
    idf::lwip idf::esp_timer idf::app_update idf::esp_partition)

# core 与 port 存在符号级循环引用（core 直调 tkl_* / 卡的 ops 符号；port 用
# tal_*/卡契约）：esp-idf 的组件库按依赖拓扑排序、无 --start-group，单遍链接
# 解析不了环。这里把 core 全部库与 port 放进同一个 GNU ld 链接组重扫
# （与 linux 侧 CMakeLists 的 --start-group 同款机制）。TUYAOPEN_CORE_LIBS
# 由 core 的 tuyaopen-core.cmake 导出（core 模块目标名列表）。
if(DEFINED TUYAOPEN_CORE_LIBS)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--start-group" ${TUYAOPEN_CORE_LIBS} tuyaopen-port "-Wl,--end-group")
else()
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--start-group" tuyaopen-port "-Wl,--end-group")
endif()

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-port)
