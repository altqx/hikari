hikari_add_qttest(hikari_ui_line_model_tests SOURCES line_model_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_grid_tests SOURCES line_grid_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_line_grid_tests PRIVATE HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
hikari_add_qttest(hikari_ui_line_grid_a11y_tests SOURCES line_grid_a11y_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_shell_tests SOURCES shell_tests.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_editor_workflow SOURCES editor_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")
# N8: script dialogs in fixed QML controls, round-tripped through the Lua helper.
hikari_add_qttest(hikari_ui_automation_dialog_tests SOURCES automation_dialog_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_automation_dialog_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include")
add_dependencies(hikari_ui_automation_dialog_tests hikari_lua_helper)
# N7: the scene-graph presenter against the CPU composite (start of M50-present).
hikari_add_qttest(hikari_ui_presenter_tests SOURCES presenter_tests.cpp
    LIBRARIES hikari_ui hikari_backends hikari_image_testing)
target_compile_definitions(hikari_ui_presenter_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/presenter")
