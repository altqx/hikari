hikari_add_qttest(hikari_ui_line_model_tests SOURCES line_model_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_grid_tests SOURCES line_grid_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_line_grid_tests PRIVATE HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")
hikari_add_qttest(hikari_ui_line_grid_a11y_tests SOURCES line_grid_a11y_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_shell_tests SOURCES shell_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_log_controller_tests SOURCES log_controller_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_tag_buttons_tests SOURCES tag_buttons_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_measures_tests SOURCES line_measures_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_shell_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/ui"
    HIKARI_FAILING_MEDIA_HELPER="$<TARGET_FILE:hikari_failing_media_helper>")
add_dependencies(hikari_ui_shell_tests hikari_media_fixtures hikari_media_helper hikari_failing_media_helper
    hikari_lua_helper hikari_automation_stage)
# The whole shell suite runs in one process (about a minute here, more on CI
# runners), so it gets more than the default 60 s.
foreach(mode offscreen xvfb)
    if(TEST hikari_ui_shell_tests.${mode})
        set_tests_properties(hikari_ui_shell_tests.${mode} PROPERTIES TIMEOUT 900)
    endif()
endforeach()
hikari_add_spix_test(hikari_ui_editor_workflow SOURCES editor_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")
# E4: the Line editor's metadata fields in the accessibility tree (E2-a11y)
# and through the real UI (Spix).
hikari_add_qttest(hikari_ui_line_editor_a11y_tests SOURCES line_editor_a11y_tests.cpp
    LIBRARIES hikari_composition hikari_a11y_testing)
hikari_add_spix_test(hikari_ui_line_fields_workflow SOURCES line_fields_workflow.cpp LIBRARIES hikari_composition)
# N8: script dialogs in fixed QML controls, round-tripped through the Lua helper.
hikari_add_qttest(hikari_ui_automation_dialog_tests SOURCES automation_dialog_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_automation_dialog_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include"
    HIKARI_CAPTURE_PROBE="${PROJECT_SOURCE_DIR}/tools/legacy-capture/automation/capture-probe.lua"
    HIKARI_LEGACY_CAPTURE_PLAN="${PROJECT_SOURCE_DIR}/tools/legacy-capture/plan.json"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
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
# A3: three Lines timed by keyboard in the audio box (Spix workflow).
hikari_add_spix_test(hikari_ui_audio_timing_workflow SOURCES audio_timing_workflow.cpp LIBRARIES hikari_composition)
# P1: close review (L58-write-close).
hikari_add_qttest(hikari_ui_close_review_tests SOURCES close_review_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_open_reload_tests SOURCES open_reload_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_recovery_tests SOURCES recovery_tests.cpp LIBRARIES hikari_composition)
# S2: automation hotkeys with real Lua scripts.
hikari_add_qttest(hikari_ui_automation_hotkeys_tests SOURCES automation_hotkeys_ui_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_hotkeys_tests PRIVATE
    HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_hotkeys_tests hikari_lua_helper hikari_automation_stage)
# S4: Document scripts and "Run the last loaded script" with real Lua helpers and media.
hikari_add_qttest(hikari_ui_automation_document_scripts_tests SOURCES automation_document_scripts_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_document_scripts_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_document_scripts_tests hikari_lua_helper hikari_automation_stage
    hikari_media_helper hikari_media_fixtures)
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
# D1: the pinned docking engine (KDDockWidgets, QtQuick frontend) qualified with the provisioned Qt.
hikari_add_qttest(hikari_ui_docking_qualification_tests SOURCES docking_qualification_tests.cpp
    LIBRARIES KDAB::kddockwidgets Qt6::Quick Qt6::QuickControls2)
hikari_add_qttest(hikari_ui_workspace_layout_tests SOURCES workspace_layout_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_update_checker_tests SOURCES update_checker_tests.cpp LIBRARIES hikari_composition)
# F3: the spell checker's Qt adapters and the Grid's marks; the shell tests use the fake backend.
hikari_add_qttest(hikari_ui_spelling_tests SOURCES spelling_ui_tests.cpp LIBRARIES hikari_ui)
target_include_directories(hikari_ui_spelling_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_include_directories(hikari_ui_shell_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
# O1: the settings registry over the INI file and the interim keys' move.
hikari_add_qttest(hikari_ui_settings_store_tests SOURCES settings_store_tests.cpp LIBRARIES hikari_ui)
# O3: the legacy settings importer's snapshot, generations and rollback.
hikari_add_qttest(hikari_ui_settings_import_tests SOURCES settings_import_ui_tests.cpp LIBRARIES hikari_composition)
# O2: remapping a command in the shortcut editor and using it (Spix workflow).
hikari_add_spix_test(hikari_ui_shortcuts_workflow SOURCES shortcuts_workflow.cpp LIBRARIES hikari_composition)
# P6: tabs and session restore through the composition.
hikari_add_qttest(hikari_ui_session_tabs_tests SOURCES session_tabs_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_session_tabs_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_SESSION_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/sessions")
add_dependencies(hikari_ui_session_tabs_tests hikari_media_fixtures hikari_media_helper)
# P6: tab traversal and close review through the real UI (Spix workflow).
hikari_add_spix_test(hikari_ui_tabs_workflow SOURCES tabs_workflow.cpp LIBRARIES hikari_composition)
# R1: subtitle comparison through the composition and the tab menu.
hikari_add_qttest(hikari_ui_comparison_tests SOURCES comparison_tests.cpp LIBRARIES hikari_composition)
# R2: reference navigation, linked matching and the subtitles preview.
hikari_add_qttest(hikari_ui_reference_tests SOURCES reference_tests.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_reference_workflow SOURCES reference_workflow.cpp LIBRARIES hikari_composition)
# T1: the crosshair and VIDEO_COPY_COORDS through the real UI (Spix workflow).
hikari_add_spix_test(hikari_ui_visual_tools_workflow SOURCES visual_tools_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_visual_tools_workflow hikari_media_helper hikari_media_fixtures)
# T2: Position and Move's numeric and keyboard alternatives through the real UI (Spix workflow).
hikari_add_spix_test(hikari_ui_visual_position_workflow SOURCES visual_position_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_visual_position_workflow hikari_media_helper hikari_media_fixtures)
# T4: the vector and rectangle clips through the real UI (Spix workflow).
hikari_add_spix_test(hikari_ui_visual_clips_workflow SOURCES visual_clips_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_visual_clips_workflow hikari_media_helper hikari_media_fixtures)
# K1: the vector icon set: the icon manifest test, the tint, the theme
# colours' contrast, live colours, and the rendering fixtures (contact sheets)
# at 100% here and at 150% and 200% in the .scale150 / .scale200 runs.
hikari_add_qttest(hikari_ui_icon_tests SOURCES icon_tests.cpp LIBRARIES hikari_ui hikari_uiplugin Qt6::Svg Qt6::QuickControls2
    OFFSCREEN_ONLY)
target_compile_definitions(hikari_ui_icon_tests PRIVATE
    HIKARI_ICON_DIR="${PROJECT_SOURCE_DIR}/src/ui/icons"
    HIKARI_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/ui"
    HIKARI_LEGACY_BITMAPS="${PROJECT_SOURCE_DIR}/HikariSub/Bitmaps")
# K2: the theme layer: role tables, accent presets and their contrast,
# following the system, the high-contrast pickers, content colours, the live
# preview and the Theme singleton; the swatch sheet with HIKARI_THEME_SHEET_DIR.
hikari_add_qttest(hikari_ui_theme_tests SOURCES theme_tests.cpp LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2 OFFSCREEN_ONLY)
foreach(scale 150 200)
    math(EXPR whole "${scale} / 100")
    math(EXPR part "${scale} % 100")
    add_test(NAME hikari_ui_icon_tests.scale${scale}
        COMMAND $<TARGET_FILE:hikari_ui_icon_tests> contactSheets iconsFollowTheThemeLive mirroredIconsPaintFlipped
            imageSourcedControlsDrawTheSet)
    set_tests_properties(hikari_ui_icon_tests.scale${scale} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${whole}.${part}" LABELS "qttest;offscreen" TIMEOUT 120)
endforeach()
# V6: Lines timed from the video (Spix workflow).
hikari_add_spix_test(hikari_ui_video_timing_workflow SOURCES video_timing_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_video_timing_workflow hikari_media_helper hikari_media_fixtures)
# Y7: the colour picker's option, simple-picker recent colour and screen sampler.
hikari_add_qttest(hikari_ui_colour_picker_tests SOURCES colour_picker_tests.cpp LIBRARIES hikari_ui)
