hikari_add_gtest(hikari_api_tests SOURCES json_tests.cpp schema_tests.cpp api_tests.cpp script_client.cpp
    LIBRARIES hikari_api LABELS api)
target_compile_definitions(hikari_api_tests PRIVATE HIKARI_API_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
