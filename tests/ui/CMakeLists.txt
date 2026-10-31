add_library(hikari_ui_test_environment OBJECT ui_test_environment.cpp)
target_link_libraries(hikari_ui_test_environment PUBLIC hikari_ui Qt6::Core)

hikari_add_qttest(hikari_ui_line_model_tests SOURCES line_model_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_grid_tests SOURCES line_grid_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_line_grid_tests PRIVATE HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")
hikari_add_qttest(hikari_ui_line_grid_a11y_tests SOURCES line_grid_a11y_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_wheel_scroll_tests SOURCES wheel_scroll_tests.cpp LIBRARIES hikari_ui Qt6::QuickControls2)
# MUST KILL: each part runs from its named function up to the next part's, in shell_tests.cpp's order
hikari_add_qttest(hikari_ui_shell_tests SOURCES shell_tests.cpp next_file_folder.cpp LIBRARIES hikari_composition
    PARTS documents
          panels=panelsFloatDockHideAndKeepTheirState
          translation=translationModeStacksOriginalAboveTranslated
          hotkeys=hotkeysPageMapsAndOkInstalls
          visual=visualToolRailCrosshairAndCopyCoordinates
          video=videoRecentListsAndDialogFolders
    PART_TIMEOUT 600)
hikari_add_qttest(hikari_ui_log_controller_tests SOURCES log_controller_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_command_names_tests SOURCES command_names_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_tag_buttons_tests SOURCES tag_buttons_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_measures_tests SOURCES line_measures_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_status_bar_tests SOURCES status_bar_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_shell_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/ui"
    HIKARI_NEXT_FILE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-v3-next-file-20261013"
    HIKARI_ATTACHMENT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/attachments"
    HIKARI_GITHUB_RELEASES="${PROJECT_SOURCE_DIR}/tests/fixtures/github-releases/releases.json"
    HIKARI_FAILING_MEDIA_HELPER="$<TARGET_FILE:hikari_failing_media_helper>"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")
add_dependencies(hikari_ui_shell_tests hikari_media_fixtures hikari_media_helper hikari_failing_media_helper
    hikari_lua_helper hikari_automation_stage)
# MSVC: shell_tests.cpp outgrows an object file's default section count (C1128).
if(MSVC)
    target_compile_options(hikari_ui_shell_tests PRIVATE /bigobj)
endif()
# HIKARI_WITH_VSFILTERMOD, not _WIN32: moc under MSVC sees no compiler-predefined macros.
if(WIN32 AND HIKARI_TEST_VSFILTERMOD_DLL)
    target_compile_definitions(hikari_ui_shell_tests PRIVATE HIKARI_WITH_VSFILTERMOD
        HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    add_custom_command(TARGET hikari_ui_shell_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:hikari_ui_shell_tests>/Csri"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${HIKARI_TEST_VSFILTERMOD_DLL}"
            "$<TARGET_FILE_DIR:hikari_ui_shell_tests>/Csri/VSFiltermod.dll"
        VERBATIM)
endif()
if(NOT WIN32)
    foreach(scale 100 150)
        math(EXPR whole "${scale} / 100")
        math(EXPR part "${scale} % 100")
        add_test(NAME hikari_ui_shell_tests.screen1280scale${scale}
            COMMAND $<TARGET_FILE:hikari_ui_shell_tests> styleEditorFitsTheScreen)
        set_tests_properties(hikari_ui_shell_tests.screen1280scale${scale} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/screen-1280x800.json;QT_SCALE_FACTOR=${whole}.${part}"
            LABELS "qttest;offscreen" TIMEOUT 120)
    endforeach()
endif()
hikari_add_spix_test(hikari_ui_editor_workflow SOURCES editor_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/fixtures/approved-departures")
hikari_add_qttest(hikari_ui_line_editor_a11y_tests SOURCES line_editor_a11y_tests.cpp
    LIBRARIES hikari_composition hikari_a11y_testing)
hikari_add_spix_test(hikari_ui_line_fields_workflow SOURCES line_fields_workflow.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_automation_dialog_tests SOURCES automation_dialog_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_automation_dialog_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include"
    HIKARI_CAPTURE_PROBE="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/automation/capture-probe.lua"
    HIKARI_LEGACY_PLAN="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/plan.json"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
add_dependencies(hikari_ui_automation_dialog_tests hikari_lua_helper)
hikari_add_qttest(hikari_ui_presenter_tests SOURCES presenter_tests.cpp
    LIBRARIES hikari_ui hikari_backends hikari_image_testing)
target_compile_definitions(hikari_ui_presenter_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/presenter"
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_presenter_tests hikari_media_helper hikari_media_fixtures)
target_link_libraries(hikari_ui_presenter_tests PRIVATE Qt6::GuiPrivate)
hikari_add_qttest(hikari_ui_automation_manager_tests SOURCES automation_manager_ui_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_automation_manager_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include")
add_dependencies(hikari_ui_automation_manager_tests hikari_lua_helper)
hikari_add_qttest(hikari_ui_automation_services_tests SOURCES automation_services_ui_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_automation_services_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_LATE_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quick/text/fonts/content/fonts/tarzeau_ocr_a.ttf"
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include"
    HIKARI_VISUAL_T3_MEASURE_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/visual-t3-measure-cases.txt"
    HIKARI_VISUAL_T3_MEASURE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t3-measure-20261012/observations.jsonl")
add_dependencies(hikari_ui_automation_services_tests hikari_lua_helper)
hikari_add_spix_test(hikari_ui_video_workflow SOURCES video_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_video_workflow hikari_media_helper hikari_media_fixtures)
hikari_add_spix_test(hikari_ui_status_workflow SOURCES status_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_status_workflow hikari_media_helper hikari_media_fixtures)
hikari_add_spix_test(hikari_ui_video_fullscreen_workflow SOURCES video_fullscreen_workflow.cpp
    LIBRARIES hikari_composition Qt6::GuiPrivate
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_video_fullscreen_workflow hikari_media_helper hikari_media_fixtures)
if(NOT WIN32)
    add_test(NAME hikari_ui_video_fullscreen_workflow.twoscreens
        COMMAND $<TARGET_FILE:hikari_ui_video_fullscreen_workflow> --monitors 2)
    set_tests_properties(hikari_ui_video_fullscreen_workflow.twoscreens PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/fullscreen-two-screens.json"
        LABELS "ui;offscreen" TIMEOUT 120)
    add_test(NAME hikari_ui_video_fullscreen_workflow.secondprimary
        COMMAND $<TARGET_FILE:hikari_ui_video_fullscreen_workflow> --monitors 2 --primary-second)
    set_tests_properties(hikari_ui_video_fullscreen_workflow.secondprimary PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/fullscreen-two-screens.json"
        LABELS "ui;offscreen" TIMEOUT 120)
endif()
hikari_add_spix_test(hikari_ui_playback_workflow SOURCES playback_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_playback_workflow hikari_media_helper hikari_media_fixtures)
if(WIN32)
    hikari_add_spix_test(hikari_ui_directshow_workflow SOURCES directshow_workflow.cpp LIBRARIES hikari_composition
        DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
    add_dependencies(hikari_ui_directshow_workflow hikari_media_helper hikari_media_fixtures)
    target_include_directories(hikari_ui_directshow_workflow PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
    target_link_libraries(hikari_ui_directshow_workflow PRIVATE strmiids winmm ole32)
    set_tests_properties(hikari_ui_directshow_workflow.offscreen PROPERTIES SKIP_RETURN_CODE 77)
endif()
hikari_add_spix_test(hikari_ui_grid_workflow SOURCES grid_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_clipboard_workflow SOURCES clipboard_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_history_workflow SOURCES history_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_audio_timing_workflow SOURCES audio_timing_workflow.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_close_review_tests SOURCES close_review_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_file_session_popups_tests SOURCES file_session_popups_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_grammar_tests SOURCES grammar_ui_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_grammar_tests PRIVATE HIKARI_HARPER_LS="${HIKARI_HARPER_LS}")
foreach(mode offscreen xvfb native)
    if(TEST hikari_ui_grammar_tests.${mode})
        set_tests_properties(hikari_ui_grammar_tests.${mode} PROPERTIES TIMEOUT 300)
    endif()
endforeach()
hikari_add_qttest(hikari_ui_theme_startup_tests SOURCES theme_startup_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_open_reload_tests SOURCES open_reload_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_open_reload_tests PRIVATE
    HIKARI_LYRICS_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/lyrics")
hikari_add_qttest(hikari_ui_recovery_tests SOURCES recovery_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_automation_hotkeys_tests SOURCES automation_hotkeys_ui_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_hotkeys_tests PRIVATE
    HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_hotkeys_tests hikari_lua_helper hikari_automation_stage)
hikari_add_qttest(hikari_ui_automation_progress_tests SOURCES automation_progress_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_progress_tests PRIVATE
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_progress_tests hikari_lua_helper hikari_automation_stage)
hikari_add_qttest(hikari_ui_automation_runs_tests SOURCES automation_runs_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_automation_document_scripts_tests SOURCES automation_document_scripts_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_document_scripts_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_S4_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-s4-document-scripts-20261013/s4.json"
    HIKARI_S4_WINDOWS_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-s4-document-scripts-20261013/s4-windows.json"
    HIKARI_S4_SCRIPTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/automation/document-scripts")
add_dependencies(hikari_ui_automation_document_scripts_tests hikari_lua_helper hikari_automation_stage
    hikari_media_helper hikari_media_fixtures)
hikari_add_qttest(hikari_ui_automation_repeat_tests SOURCES automation_repeat_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_repeat_tests PRIVATE
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_repeat_tests hikari_lua_helper hikari_automation_stage)
hikari_add_qttest(hikari_ui_automation_submenus_tests SOURCES automation_submenus_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_submenus_tests PRIVATE
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_submenus_tests hikari_lua_helper hikari_automation_stage)
hikari_add_qttest(hikari_ui_template_editor_tests SOURCES template_editor_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin hikari_backends)
target_compile_definitions(hikari_ui_template_editor_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_KARAOKE_TEMPLATER="${HIKARI_AUTOMATION_STAGE}/Autoload/kara-templater.lua"
    HIKARI_KARAOKE_CORPUS="${PROJECT_SOURCE_DIR}/tests/backends/karaoke-corpus"
    HIKARI_KARAOKE_PRESET_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/karaoke-presets")
target_include_directories(hikari_ui_template_editor_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
add_dependencies(hikari_ui_template_editor_tests hikari_lua_helper hikari_automation_stage)
foreach(mode offscreen xvfb native)
    if(TEST hikari_ui_template_editor_tests.${mode})
        set_tests_properties(hikari_ui_template_editor_tests.${mode} PROPERTIES TIMEOUT 300)
    endif()
endforeach()
hikari_add_qttest(hikari_ui_template_editor_shell_tests SOURCES template_editor_shell_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_template_editor_shell_tests PRIVATE
    HIKARI_KARAOKE_TEMPLATER="${HIKARI_AUTOMATION_STAGE}/Autoload/kara-templater.lua"
    HIKARI_KARAOKE_CORPUS="${PROJECT_SOURCE_DIR}/tests/backends/karaoke-corpus")
add_dependencies(hikari_ui_template_editor_shell_tests hikari_lua_helper hikari_automation_stage)
hikari_add_qttest(hikari_ui_karaoke_report_tests SOURCES karaoke_report_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_karaoke_report_tests PRIVATE
    HIKARI_KARAOKE_TEMPLATER="${HIKARI_AUTOMATION_STAGE}/Autoload/kara-templater.lua"
    HIKARI_KARAOKE_CORPUS="${PROJECT_SOURCE_DIR}/tests/backends/karaoke-corpus")
add_dependencies(hikari_ui_karaoke_report_tests hikari_lua_helper hikari_automation_stage)
hikari_add_qttest(hikari_ui_automation_resolution_tests SOURCES automation_resolution_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_resolution_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_resolution_tests hikari_lua_helper hikari_automation_stage
    hikari_media_helper hikari_media_fixtures)
hikari_add_spix_test(hikari_ui_close_workflow SOURCES close_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_open_workflow SOURCES open_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_automation_workflow SOURCES automation_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
                HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
                HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_workflow hikari_media_helper hikari_media_fixtures hikari_lua_helper
    hikari_automation_stage)
hikari_add_qttest(hikari_ui_docking_qualification_tests SOURCES docking_qualification_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin KDAB::kddockwidgets Qt6::Quick Qt6::QuickControls2)
target_link_libraries(hikari_ui_docking_qualification_tests PRIVATE Qt6::GuiPrivate)
foreach(scale 125 150 175 200)
    math(EXPR whole "${scale} / 100")
    math(EXPR part "${scale} % 100")
    add_test(NAME hikari_ui_docking_qualification_tests.scale${scale}
        COMMAND $<TARGET_FILE:hikari_ui_docking_qualification_tests> floatingHeaderDocksByItsTitleAndMovesByTheRest
            floatingTabBarMovesRightOfItsTabs floatingTitleKeepsItsRoom)
    set_tests_properties(hikari_ui_docking_qualification_tests.scale${scale} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${whole}.${part}" LABELS "qttest;offscreen" TIMEOUT 120)
endforeach()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME hikari_native_gate_windows_declared_skips
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/native-gate/windows/check_declared_skips.py"
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(hikari_native_gate_windows_declared_skips PROPERTIES LABELS "native-gate" TIMEOUT 60)
endif()
hikari_add_qttest(hikari_ui_workspace_layout_tests SOURCES workspace_layout_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
hikari_add_qttest(hikari_ui_layout_migration_tests SOURCES layout_migration_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
target_compile_definitions(hikari_ui_layout_migration_tests PRIVATE
    HIKARI_LAYOUT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/layout-fixtures")
hikari_add_qttest(hikari_ui_tab_layouts_tests SOURCES tab_layouts_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
target_compile_definitions(hikari_ui_tab_layouts_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_tab_layouts_tests hikari_media_fixtures hikari_media_helper)
target_include_directories(hikari_ui_tab_layouts_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_qttest(hikari_ui_file_layouts_tests SOURCES file_layouts_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets
    PARTS arrangements refusals=anUnusableFileArrangementFollowsTheNewTabRule
          bounds=aLayoutLargerThanThePanelsIsRefused tree=aLayoutTheEngineCannotReadIsRefused
          positions=aPanelPositionTheEngineCannotReadIsRefused places=aWindowPlaceNoWindowCanHaveIsRefused recovery=aRecoveredCopyKeepsItsArrangement)
target_include_directories(hikari_ui_file_layouts_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_qttest(hikari_ui_translation_preset_tests SOURCES translation_preset_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
hikari_add_qttest(hikari_ui_typesetting_preset_tests SOURCES typesetting_preset_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
hikari_add_qttest(hikari_ui_karaoke_preset_tests SOURCES karaoke_preset_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
hikari_add_qttest(hikari_ui_saved_layouts_tests SOURCES saved_layouts_tests.cpp
    LIBRARIES hikari_composition hikari_translations KDAB::kddockwidgets
    PARTS layouts kept=unusableLayoutsAreKept)
target_include_directories(hikari_ui_saved_layouts_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_ui_saved_layouts_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_saved_layouts_tests hikari_media_helper hikari_media_fixtures)
hikari_add_qttest(hikari_ui_line_editor_options_tests SOURCES line_editor_options_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_update_checker_tests SOURCES update_checker_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_whats_new_tests SOURCES whats_new_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_whats_new_tests PRIVATE
    HIKARI_GITHUB_RELEASES="${PROJECT_SOURCE_DIR}/tests/fixtures/github-releases/releases.json")
add_executable(hikari_crash_probe crash_probe.cpp)
target_link_libraries(hikari_crash_probe PRIVATE hikari_composition)
hikari_add_qttest(hikari_ui_analytics_tests SOURCES analytics_tests.cpp LIBRARIES hikari_composition)
target_include_directories(hikari_ui_analytics_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_qttest(hikari_ui_launcher_tests SOURCES launcher_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_crash_report_tests SOURCES crash_report_tests.cpp LIBRARIES hikari_composition)
target_include_directories(hikari_ui_crash_report_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_ui_crash_report_tests PRIVATE HIKARI_CRASH_PROBE="$<TARGET_FILE:hikari_crash_probe>"
    HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_dependencies(hikari_ui_crash_report_tests hikari_crash_probe)
hikari_add_qttest(hikari_ui_crash_report_session_tests SOURCES crash_report_session_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets Qt6::QuickTest)
target_include_directories(hikari_ui_crash_report_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_qttest(hikari_ui_analytics_session_tests SOURCES analytics_session_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets Qt6::QuickTest)
target_include_directories(hikari_ui_analytics_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_ui_analytics_session_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_analytics_session_tests hikari_media_helper hikari_media_fixtures hikari_lua_helper
    hikari_automation_stage)
hikari_add_qttest(hikari_ui_update_installer_tests SOURCES update_installer_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_update_apply_tests SOURCES update_apply_tests.cpp LIBRARIES hikari_composition)
# For the compiler and moc alike: moc gets no _WIN32 under MSVC.
target_compile_definitions(hikari_ui_update_apply_tests PRIVATE HIKARI_UPDATE_APPLY_WINDOWS_TESTS=$<BOOL:${WIN32}>)
if(WIN32)
    add_executable(hikari_setup_mutex_helper setup_mutex_helper.cpp)
    target_compile_definitions(hikari_ui_update_apply_tests PRIVATE
        HIKARI_SETUP_MUTEX_HELPER="$<TARGET_FILE:hikari_setup_mutex_helper>")
    add_dependencies(hikari_ui_update_apply_tests hikari_setup_mutex_helper)
endif()
hikari_add_qttest(hikari_ui_spelling_tests SOURCES spelling_ui_tests.cpp LIBRARIES hikari_ui)
target_include_directories(hikari_ui_spelling_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_include_directories(hikari_ui_shell_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_qttest(hikari_ui_settings_store_tests SOURCES settings_store_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_list_files_tests SOURCES list_files_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_list_files_tests PRIVATE
    HIKARI_LIST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/list-files"
    HIKARI_FONT_CATALOG_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/font-catalogs")
hikari_add_qttest(hikari_ui_settings_import_tests SOURCES settings_import_ui_tests.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_shortcuts_workflow SOURCES shortcuts_workflow.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_session_tabs_tests SOURCES session_tabs_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_session_tabs_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_SESSION_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/sessions")
add_dependencies(hikari_ui_session_tabs_tests hikari_media_fixtures hikari_media_helper)
target_include_directories(hikari_ui_session_tabs_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_qttest(hikari_ui_playing_frame_tests SOURCES playing_frame_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_playing_frame_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_playing_frame_tests hikari_media_fixtures hikari_media_helper)
hikari_add_spix_test(hikari_ui_tabs_workflow SOURCES tabs_workflow.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_tab_menu_tests SOURCES tab_menu_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_tab_menu_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_tab_menu_tests hikari_media_fixtures hikari_media_helper)
foreach(mode offscreen xvfb native)
    if(TEST hikari_ui_tab_menu_tests.${mode})
        set_tests_properties(hikari_ui_tab_menu_tests.${mode} PROPERTIES TIMEOUT 300)
    endif()
endforeach()
hikari_add_qttest(hikari_ui_single_instance_tests SOURCES single_instance_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_user_folder_tests SOURCES user_folder_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_forwarded_launches_tests SOURCES forwarded_launches_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_forwarded_launches_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_forwarded_launches_tests hikari_media_fixtures hikari_media_helper)
hikari_add_qttest(hikari_ui_desktop_handoff_tests SOURCES desktop_handoff_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_user_guide_tests SOURCES user_guide_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_user_guide_tests PRIVATE HIKARI_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/ui")
hikari_add_qttest(hikari_ui_docs_export_tests SOURCES docs_export_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_docs_export_tests PRIVATE HIKARI_PROGRAM="$<TARGET_FILE:hikarisub>"
    HIKARI_VERSION="${HIKARI_VERSION}")
add_dependencies(hikari_ui_docs_export_tests hikarisub)
hikari_add_qttest(hikari_ui_comparison_tests SOURCES comparison_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_reference_tests SOURCES reference_tests.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_reference_workflow SOURCES reference_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_visual_tools_workflow SOURCES visual_tools_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_visual_tools_workflow hikari_media_helper hikari_media_fixtures)
hikari_add_spix_test(hikari_ui_visual_position_workflow SOURCES visual_position_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_visual_position_workflow hikari_media_helper hikari_media_fixtures)
hikari_add_spix_test(hikari_ui_visual_clips_workflow SOURCES visual_clips_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_visual_clips_workflow hikari_media_helper hikari_media_fixtures)
hikari_add_qttest(hikari_ui_icon_tests SOURCES icon_tests.cpp LIBRARIES hikari_ui hikari_uiplugin Qt6::Svg Qt6::QuickControls2
    OFFSCREEN_ONLY)
target_compile_definitions(hikari_ui_icon_tests PRIVATE
    HIKARI_ICON_DIR="${PROJECT_SOURCE_DIR}/src/ui/icons"
    HIKARI_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/ui")
hikari_add_qttest(hikari_ui_theme_tests SOURCES theme_tests.cpp LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2 OFFSCREEN_ONLY)
if(UNIX AND NOT APPLE)
    target_link_libraries(hikari_ui_theme_tests PRIVATE Qt6::DBus)
    target_compile_definitions(hikari_ui_theme_tests PRIVATE HIKARI_COLOUR_SCHEME_PORTAL)
endif()
hikari_add_qttest(hikari_ui_title_bar_tests SOURCES title_bar_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2)
if(WIN32)
    add_test(NAME hikari_ui_title_bar_tests.windows
        COMMAND $<TARGET_FILE:hikari_ui_title_bar_tests> windowsTitleBarFollowsTheTheme
            -o "${CMAKE_BINARY_DIR}/qttest-hikari_ui_title_bar_tests-windows.txt,txt" -o "-,txt")
    set_tests_properties(hikari_ui_title_bar_tests.windows PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows" LABELS "qttest;windows" TIMEOUT 120)
endif()
hikari_add_qttest(hikari_ui_window_frame_tests SOURCES window_frame_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_dialog_frame_tests SOURCES dialog_frame_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2)
hikari_add_qttest(hikari_ui_popup_kit_tests SOURCES popup_kit_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2 Qt6::QuickTest)
hikari_add_qttest(hikari_ui_qml_engine_tests SOURCES qml_engine_tests.cpp LIBRARIES hikari_composition)
foreach(scale 150 200)
    math(EXPR whole "${scale} / 100")
    math(EXPR part "${scale} % 100")
    add_test(NAME hikari_ui_icon_tests.scale${scale}
        COMMAND $<TARGET_FILE:hikari_ui_icon_tests> contactSheets iconsFollowTheThemeLive mirroredIconsPaintFlipped
            imageSourcedControlsDrawTheSet)
    set_tests_properties(hikari_ui_icon_tests.scale${scale} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${whole}.${part}" LABELS "qttest;offscreen" TIMEOUT 120)
endforeach()
add_test(NAME hikari_ui_icon_tests.pages COMMAND $<TARGET_FILE:hikari_ui_icon_tests> contactSheets)
set_tests_properties(hikari_ui_icon_tests.pages PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HIKARI_ICON_SHEET_PAGE_ROWS=3;HIKARI_ICON_SHEET_DIR=${CMAKE_BINARY_DIR}/icon-sheet-pages"
    LABELS "qttest;offscreen" TIMEOUT 120)
hikari_add_spix_test(hikari_ui_video_timing_workflow SOURCES video_timing_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_video_timing_workflow hikari_media_helper hikari_media_fixtures)
hikari_add_qttest(hikari_ui_colour_picker_tests SOURCES colour_picker_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_font_faces_tests SOURCES font_faces_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_font_faces_tests PRIVATE
    HIKARI_SYMBOL_FONT="${PROJECT_SOURCE_DIR}/tests/fixtures/libass-fork/forktest-symf020.ttf"
    HIKARI_NO_LETTERS_FONT="${PROJECT_SOURCE_DIR}/tests/fixtures/libass-fork/forktest-arabic.ttf")
hikari_add_qttest(hikari_ui_language_tests SOURCES language_tests.cpp LIBRARIES hikari_composition hikari_translations)
target_compile_definitions(hikari_ui_language_tests PRIVATE HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_language_tests hikari_lua_helper hikari_automation_stage)
find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS LinguistTools)
qt_add_lrelease(TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/language-fixtures/hikarisub_pl.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/language-fixtures/qt_pl.ts
    QM_OUTPUT_DIRECTORY language-fixtures LRELEASE_TARGET hikari_ui_language_fixtures NO_GLOBAL_TARGET OPTIONS -silent)
add_dependencies(hikari_ui_language_tests hikari_ui_language_fixtures)
target_compile_definitions(hikari_ui_language_tests PRIVATE
    HIKARI_LANGUAGE_FIXTURES="${CMAKE_CURRENT_BINARY_DIR}/language-fixtures")
foreach(mode offscreen xvfb)
    if(TEST hikari_ui_language_tests.${mode})
        set_tests_properties(hikari_ui_language_tests.${mode} PROPERTIES TIMEOUT 300)
    endif()
endforeach()

get_directory_property(hikari_ui_test_targets BUILDSYSTEM_TARGETS)
foreach(target IN LISTS hikari_ui_test_targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
        target_link_libraries(${target} PRIVATE hikari_ui_test_environment hikari_test_url_recorder)
        get_target_property(libraries ${target} LINK_LIBRARIES)
        if("hikari_composition" IN_LIST libraries OR "hikari_backends" IN_LIST libraries)
            target_link_libraries(${target} PRIVATE hikari_test_desktop_routes)
        endif()
    endif()
endforeach()
