# 连接服务层：对上提供带 TLS 的 HTTP / MQTT / TLS 连接接口，
# 对下只依赖 third_party 官方库与 tal_system。
# 自研代码全部合并为本目标：传输原语（tcp/tls/websocket transporter）、
# http/mqtt wrap、TLS 会话封装（原 libtls）、安全算法（原 tal_security）、
# 网络原语（原 tal_network）。

file(GLOB_RECURSE _tuya_transport_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tuya_transport STATIC)
target_sources(tuya_transport PRIVATE ${_tuya_transport_srcs})
# include/ 与 src/ 同构的四个子目录；消费者仍用 #include "xxx.h" 不带前缀
target_include_directories(tuya_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/transport>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/tls>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/http>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mqtt>
    # 通用工具已迁至 iot-core/utilities（tuya_utilities 目标，见下方依赖）
    # 用户提供型配置头：MBEDTLS_CONFIG_FILE / core_http/mqtt_config.h 展开点
    # 不止官方库（本目标与消费者亦有），须随 INTERFACE 传播
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/config>
)

# 官方库目标（third_party 注册）链接自研侧；CONFIG 门控的 mbedtls 可能不存在。
# PUBLIC：官方库头与符号随本目标传播给所有消费者。
foreach(_tp_official corehttp coremqtt backoff_algorithm libcjson mbedtls)
    if(TARGET ${_tp_official})
        target_link_libraries(tuya_transport PUBLIC ${_tp_official})
    endif()
endforeach()
unset(_tp_official)

# 给官方库编译单元注入自研侧头路径（transport_interface.h、配置头）与
# TAL 头依赖（tuya_cloud_types.h、tal_memory.h 等），third_party 保持
# 纯官方定义，不反向引用自研目录。
#
# mbedtls 只在 core 自建时注入（CONFIG_ENABLE_PLATFORM_MBEDTLS=OFF）：平台
# 提供 mbedtls 时（esp-idf 宿主）该目标名是宿主侧组件/桥接目标，注入 core
# 的 config 目录会让宿主的 mbedtls 消费者（esp_common 等）拿到 core 的
# threading_alt.h（内部 include tal_mutex.h）而编译失败；宿主自带的
# mbedtls 配置体系（esp_config.h + ROM 版 threading_alt.h）由宿主注入。
foreach(_tp_official corehttp coremqtt)
    if(TARGET ${_tp_official})
        target_include_directories(${_tp_official} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        # 配置头（core_http|mqtt_config.h 的展开点）随 INTERFACE 传播：
        # 任何 link 官方库的模块都需要它，并非本目标的私有依赖
        target_include_directories(${_tp_official} INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/config
        )
        target_link_libraries(${_tp_official} PRIVATE tuyaopen-headers tal_system)
    endif()
endforeach()
unset(_tp_official)

if(TARGET mbedtls AND NOT CONFIG_ENABLE_PLATFORM_MBEDTLS)
    target_include_directories(mbedtls PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    # 配置头（MBEDTLS_CONFIG_FILE 展开点）随 INTERFACE 传播：任何 link
    # mbedtls 的模块都需要它（如 tuya_utilities 用 mbedtls/base64.h）
    target_include_directories(mbedtls INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/config
    )
    target_link_libraries(mbedtls PRIVATE tuyaopen-headers tal_system)
endif()

target_link_libraries(tuya_transport PRIVATE
    tuyaopen-headers
    tal_system
)
# 通用工具（crc/random/mix_method/dev_evt 等）随本目标 PUBLIC 传播给消费者
target_link_libraries(tuya_transport PUBLIC tuya_utilities)
unset(_tuya_transport_srcs)
