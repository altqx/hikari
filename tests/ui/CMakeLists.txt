hikari_add_qttest(hikari_ui_line_model_tests SOURCES line_model_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_grid_tests SOURCES line_grid_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_line_grid_tests PRIVATE HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
hikari_add_qttest(hikari_ui_line_grid_a11y_tests SOURCES line_grid_a11y_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_shell_tests SOURCES shell_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_log_controller_tests SOURCES log_controller_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_tag_buttons_tests SOURCES tag_buttons_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_measures_tests SOURCES line_measures_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_shell_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_shell_tests hikari_media_fixtures hikari_media_helper)
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
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/presenter"
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_presenter_tests hikari_media_helper hikari_media_fixtures)
# L1: the automation manager tool over the real manager.
hikari_add_qttest(hikari_ui_automation_manager_tests SOURCES automation_manager_ui_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_automation_manager_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include")
add_dependencies(hikari_ui_automation_manager_tests hikari_lua_helper)
# L3: Qt adapters for automation host services.
hikari_add_qttest(hikari_ui_automation_services_tests SOURCES automation_services_ui_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_automation_services_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include")
add_dependencies(hikari_ui_automation_services_tests hikari_lua_helper)
# I1: indexed video in the Classic shell (Spix workflow).
hikari_add_spix_test(hikari_ui_video_workflow SOURCES video_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_video_workflow hikari_media_helper hikari_media_fixtures)
# V1: playback with the I4 handoff (Spix workflow).
hikari_add_spix_test(hikari_ui_playback_workflow SOURCES playback_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_playback_workflow hikari_media_helper hikari_media_fixtures)
# G1: Grid range selection (Spix workflow).
hikari_add_spix_test(hikari_ui_grid_workflow SOURCES grid_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_clipboard_workflow SOURCES clipboard_workflow.cpp LIBRARIES hikari_composition)
# G10: History (Spix workflow).
hikari_add_spix_test(hikari_ui_history_workflow SOURCES history_workflow.cpp LIBRARIES hikari_composition)
# P1: close review (L58-write-close).
hikari_add_qttest(hikari_ui_close_review_tests SOURCES close_review_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_open_reload_tests SOURCES open_reload_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_recovery_tests SOURCES recovery_tests.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_close_workflow SOURCES close_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_open_workflow SOURCES open_workflow.cpp LIBRARIES hikari_composition)
# S1: automation in the shell (Spix workflow, real helpers and video).
hikari_add_spix_test(hikari_ui_automation_workflow SOURCES automation_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
                HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
                HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_workflow hikari_media_helper hikari_media_fixtures hikari_lua_helper
    hikari_automation_stage)
