hikari_add_gtest(hikari_api_tests SOURCES json_tests.cpp schema_tests.cpp api_tests.cpp media_file_tests.cpp folder_tests.cpp
    reference_tests.cpp reference_markdown_tests.cpp skill_tests.cpp automation_tests.cpp effects_tests.cpp typeset_tests.cpp
    translation_tests.cpp karaoke_tests.cpp plugin_tests.cpp plugin_feature_tests.cpp vcs_tests.cpp script_client.cpp
    LIBRARIES hikari_api hikari_regex LABELS api)
target_compile_definitions(hikari_api_tests PRIVATE HIKARI_API_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    HIKARI_API_SKILLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/skills"
    HIKARI_API_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/reference-v1.json"
    HIKARI_AI_SIDECAR_API_REFERENCE="${PROJECT_SOURCE_DIR}/src/ai-sidecar/src/tools/api.json"
    HIKARI_KARAOKE_PRESET_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/karaoke-presets"
    HIKARI_API_MARKDOWN_REFERENCE="${PROJECT_SOURCE_DIR}/docs/api-reference.md"
    HIKARI_KARAOKE_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/karaoke")
