idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")

# tuyaopen-core（模块库聚合）+ tuyaopen-port（TKL，注入 port 头与静态 tuya_kconfig.h）
target_link_libraries(${COMPONENT_LIB} PRIVATE tuyaopen-core tuyaopen-port)
