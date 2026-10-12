# AI 组件层（原 tuya_ai_service + 各 ai_* 组件）。
# 应用主流程（ai_main）/对话模式（ai_mode）/UI（ai_ui）/图片（ai_picture）不在本仓范围，
# 由应用自行组织（见 examples/agent_demo）。
if(NOT CONFIG_ENABLE_AI_COMPONENTS)
    return()
endif()

add_subdirectory(tuya_ai_service)
add_subdirectory(utility)
add_subdirectory(ai_agent)
add_subdirectory(ai_skills)
add_subdirectory(ai_mcp)
