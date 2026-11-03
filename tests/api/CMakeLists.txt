hikari_add_gtest(hikari_api_tests SOURCES json_tests.cpp schema_tests.cpp api_tests.cpp media_file_tests.cpp
    reference_tests.cpp script_client.cpp
    LIBRARIES hikari_api hikari_regex LABELS api)
target_compile_definitions(hikari_api_tests PRIVATE HIKARI_API_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    HIKARI_API_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/reference-v1.json")
