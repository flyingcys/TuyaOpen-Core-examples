add_subdirectory(tuya_ai_service)

# Component layer on top of tuya_ai_service; the service itself always builds.
if(NOT CONFIG_ENABLE_AI_COMPONENTS)
    return()
endif()

add_subdirectory(utility)
add_subdirectory(ai_agent)
add_subdirectory(ai_main)
add_subdirectory(ai_skills)
add_subdirectory(ai_audio)
add_subdirectory(ai_mcp)
add_subdirectory(ai_video)
