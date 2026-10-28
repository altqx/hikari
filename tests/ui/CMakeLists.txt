# Every executable here starts in the UI tests' environment (the
# application's controls style, the system's fonts offscreen on Windows;
# ui_test_environment.cpp), linked into each at the end of this file.
add_library(hikari_ui_test_environment OBJECT ui_test_environment.cpp)
target_link_libraries(hikari_ui_test_environment PUBLIC hikari_ui Qt6::Core)

hikari_add_qttest(hikari_ui_line_model_tests SOURCES line_model_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_grid_tests SOURCES line_grid_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_line_grid_tests PRIVATE HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")
hikari_add_qttest(hikari_ui_line_grid_a11y_tests SOURCES line_grid_a11y_tests.cpp LIBRARIES hikari_ui)
# The shell tests run in parts, each its own process with its own timeout
# (decision 38f, 2026-10-14: in one process they took about 800-850 s under
# Xvfb on akari-rvm against a 900 s timeout). A part runs from its first
# function up to the next part's, in shell_tests.cpp's order, so a function
# added anywhere joins the part around it; hikari_ui_shell_tests.parts checks
# that every function runs exactly once.
hikari_add_qttest(hikari_ui_shell_tests SOURCES shell_tests.cpp next_file_folder.cpp LIBRARIES hikari_composition
    PARTS documents
          panels=panelsFloatDockHideAndKeepTheirState
          translation=translationModeStacksOriginalAboveTranslated
          hotkeys=hotkeysPageMapsAndOkInstalls
          visual=visualToolRailCrosshairAndCopyCoordinates
          video=videoRecentListsAndDialogFolders
    PART_TIMEOUT 600)
hikari_add_qttest(hikari_ui_log_controller_tests SOURCES log_controller_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_tag_buttons_tests SOURCES tag_buttons_tests.cpp LIBRARIES hikari_ui)
hikari_add_qttest(hikari_ui_line_measures_tests SOURCES line_measures_tests.cpp LIBRARIES hikari_ui)
# P10: the status bar's fields and the first field's progress text.
hikari_add_qttest(hikari_ui_status_bar_tests SOURCES status_bar_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_shell_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/ui"
    HIKARI_NEXT_FILE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-v3-next-file-20261013"
    HIKARI_ATTACHMENT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/attachments"
    HIKARI_GITHUB_RELEASES="${PROJECT_SOURCE_DIR}/tests/fixtures/github-releases/releases.json"
    HIKARI_FAILING_MEDIA_HELPER="$<TARGET_FILE:hikari_failing_media_helper>"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")
add_dependencies(hikari_ui_shell_tests hikari_media_fixtures hikari_media_helper hikari_failing_media_helper
    hikari_lua_helper hikari_automation_stage)
# MSVC: the shell tests outgrew an object file's default section count (C1128).
if(MSVC)
    target_compile_options(hikari_ui_shell_tests PRIVATE /bigobj)
endif()
# W2 (Windows, HIKARI_TEST_VSFILTERMOD): the pinned VSFilterMod in the Csri
# folder beside the tests, as the package ships it, so the Video page offers
# it and the loaded video switches renderers. HIKARI_WITH_VSFILTERMOD is a
# CMake definition so moc sees it too.
if(WIN32 AND HIKARI_TEST_VSFILTERMOD_DLL)
    target_compile_definitions(hikari_ui_shell_tests PRIVATE HIKARI_WITH_VSFILTERMOD
        HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    add_custom_command(TARGET hikari_ui_shell_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:hikari_ui_shell_tests>/Csri"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${HIKARI_TEST_VSFILTERMOD_DLL}"
            "$<TARGET_FILE_DIR:hikari_ui_shell_tests>/Csri/VSFiltermod.dll"
        VERBATIM)
endif()
# The Style manager and editor on a 1280 x 800 screen at 100 % and 150 %
# (the offscreen platform's screen from a file, so Linux only).
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
    HIKARI_CAPTURE_PROBE="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/automation/capture-probe.lua"
    HIKARI_LEGACY_PLAN="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/plan.json"
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
# V1: the largest texture the RHI backend makes (QRhi::resourceLimit).
target_link_libraries(hikari_ui_presenter_tests PRIVATE Qt6::GuiPrivate)
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
    HIKARI_LATE_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quick/text/fonts/content/fonts/tarzeau_ocr_a.ttf"
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include"
    HIKARI_VISUAL_T3_MEASURE_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/visual-t3-measure-cases.txt"
    HIKARI_VISUAL_T3_MEASURE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t3-measure-20261012/observations.jsonl")
add_dependencies(hikari_ui_automation_services_tests hikari_lua_helper)
# I1: indexed video in the Classic shell (Spix workflow).
hikari_add_spix_test(hikari_ui_video_workflow SOURCES video_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_video_workflow hikari_media_helper hikari_media_fixtures)
# P10: the status bar's fields without and with media (Spix workflow).
hikari_add_spix_test(hikari_ui_status_workflow SOURCES status_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_status_workflow hikari_media_helper hikari_media_fixtures)
# V5: video fullscreen, its keys and the monitor choice (Spix workflow); on
# Linux also with two offscreen screens, the second at scale 2.
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
    # V5-monitor-order: the second screen made the primary.
    add_test(NAME hikari_ui_video_fullscreen_workflow.secondprimary
        COMMAND $<TARGET_FILE:hikari_ui_video_fullscreen_workflow> --monitors 2 --primary-second)
    set_tests_properties(hikari_ui_video_fullscreen_workflow.secondprimary PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/fullscreen-two-screens.json"
        LABELS "ui;offscreen" TIMEOUT 120)
endif()
# V1: playback with the I4 handoff (Spix workflow).
hikari_add_spix_test(hikari_ui_playback_workflow SOURCES playback_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_playback_workflow hikari_media_helper hikari_media_fixtures)
# W1: playing and stepping with the optional DirectShow adapter chosen (Windows only).
if(WIN32)
    hikari_add_spix_test(hikari_ui_directshow_workflow SOURCES directshow_workflow.cpp LIBRARIES hikari_composition
        DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
    add_dependencies(hikari_ui_directshow_workflow hikari_media_helper hikari_media_fixtures)
    # A host that cannot build legacy's graph skips (tests/support/directshow/directshow_host.h).
    target_include_directories(hikari_ui_directshow_workflow PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
    target_link_libraries(hikari_ui_directshow_workflow PRIVATE strmiids winmm ole32)
    set_tests_properties(hikari_ui_directshow_workflow.offscreen PROPERTIES SKIP_RETURN_CODE 77)
endif()
# G1: Grid range selection (Spix workflow).
hikari_add_spix_test(hikari_ui_grid_workflow SOURCES grid_workflow.cpp LIBRARIES hikari_composition)
hikari_add_spix_test(hikari_ui_clipboard_workflow SOURCES clipboard_workflow.cpp LIBRARIES hikari_composition)
# G10: History (Spix workflow).
hikari_add_spix_test(hikari_ui_history_workflow SOURCES history_workflow.cpp LIBRARIES hikari_composition)
# A3: three Lines timed by keyboard in the audio box (Spix workflow).
hikari_add_spix_test(hikari_ui_audio_timing_workflow SOURCES audio_timing_workflow.cpp LIBRARIES hikari_composition)
# P1: close review (L58-write-close).
hikari_add_qttest(hikari_ui_close_review_tests SOURCES close_review_tests.cpp LIBRARIES hikari_composition)
# PU1: the changed-on-disk bar, the restore's entry bars and the startup offer.
hikari_add_qttest(hikari_ui_file_session_popups_tests SOURCES file_session_popups_tests.cpp LIBRARIES hikari_composition)
# GR1 (#364): grammar check with the pinned harper-ls in the main window.
hikari_add_qttest(hikari_ui_grammar_tests SOURCES grammar_ui_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_grammar_tests PRIVATE HIKARI_HARPER_LS="${HIKARI_HARPER_LS}")
# Each case starts harper-ls and waits for its checks (the walks most).
foreach(mode offscreen xvfb native)
    if(TEST hikari_ui_grammar_tests.${mode})
        set_tests_properties(hikari_ui_grammar_tests.${mode} PROPERTIES TIMEOUT 300)
    endif()
endforeach()
# K2 / W1: the popups take the theme's palette from the first engine's start.
hikari_add_qttest(hikari_ui_theme_startup_tests SOURCES theme_startup_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_open_reload_tests SOURCES open_reload_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_open_reload_tests PRIVATE
    HIKARI_LYRICS_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/lyrics")
hikari_add_qttest(hikari_ui_recovery_tests SOURCES recovery_tests.cpp LIBRARIES hikari_composition)
# S2: automation hotkeys with real Lua scripts.
hikari_add_qttest(hikari_ui_automation_hotkeys_tests SOURCES automation_hotkeys_ui_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_hotkeys_tests PRIVATE
    HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_hotkeys_tests hikari_lua_helper hikari_automation_stage)
# AD3 (#312): a macro run's progress card and its kept log, with real Lua helpers.
hikari_add_qttest(hikari_ui_automation_progress_tests SOURCES automation_progress_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_progress_tests PRIVATE
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_progress_tests hikari_lua_helper hikari_automation_stage)
hikari_add_qttest(hikari_ui_automation_runs_tests SOURCES automation_runs_tests.cpp LIBRARIES hikari_ui)
# S4: Document scripts and "Run the last loaded script" with real Lua helpers and media.
hikari_add_qttest(hikari_ui_automation_document_scripts_tests SOURCES automation_document_scripts_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_document_scripts_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    # The legacy observations of the Document scripts, and the scripts.
    HIKARI_S4_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-s4-document-scripts-20261013/s4.json"
    HIKARI_S4_WINDOWS_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-s4-document-scripts-20261013/s4-windows.json"
    HIKARI_S4_SCRIPTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/automation/document-scripts")
add_dependencies(hikari_ui_automation_document_scripts_tests hikari_lua_helper hikari_automation_stage
    hikari_media_helper hikari_media_fixtures)
# AD2: Repeat last run and "Fill in my last answers" with the real Lua helper.
hikari_add_qttest(hikari_ui_automation_repeat_tests SOURCES automation_repeat_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_repeat_tests PRIVATE
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_repeat_tests hikari_lua_helper hikari_automation_stage)
# KE1-template-timing: the karaoke templater's report in the status bar and the log window.
# KE2: the Template editor's controller over the bundled karaoke templater, and its panel.
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
# Each test starts the templater's helper; together about a minute.
foreach(mode offscreen xvfb native)
    if(TEST hikari_ui_template_editor_tests.${mode})
        set_tests_properties(hikari_ui_template_editor_tests.${mode} PROPERTIES TIMEOUT 300)
    endif()
endforeach()
# KE2: the Template editor panel in the shell, with the video and the Automation menu's run.
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
# Y4-playres-ask: the script resolution question with real Lua helpers and media.
hikari_add_qttest(hikari_ui_automation_resolution_tests SOURCES automation_resolution_tests.cpp
    LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_automation_resolution_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_automation_resolution_tests hikari_lua_helper hikari_automation_stage
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
    LIBRARIES hikari_ui hikari_uiplugin KDAB::kddockwidgets Qt6::Quick Qt6::QuickControls2)
# The offscreen platform's screen configuration (a live scale change).
target_link_libraries(hikari_ui_docking_qualification_tests PRIVATE Qt6::GuiPrivate)
# D3: the floating header's split at the scales Windows offers.
foreach(scale 125 150 175 200)
    math(EXPR whole "${scale} / 100")
    math(EXPR part "${scale} % 100")
    add_test(NAME hikari_ui_docking_qualification_tests.scale${scale}
        COMMAND $<TARGET_FILE:hikari_ui_docking_qualification_tests> floatingHeaderDocksByItsTitleAndMovesByTheRest
            floatingTabBarMovesRightOfItsTabs floatingTitleKeepsItsRoom)
    set_tests_properties(hikari_ui_docking_qualification_tests.scale${scale} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${whole}.${part}" LABELS "qttest;offscreen" TIMEOUT 120)
endforeach()
# The Windows native gate's declared skips cover every skip of the suites it
# runs whole (tools/native-gate/windows/check_declared_skips.py).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME hikari_native_gate_windows_declared_skips
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/native-gate/windows/check_declared_skips.py"
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(hikari_native_gate_windows_declared_skips PROPERTIES LABELS "native-gate" TIMEOUT 60)
endif()
hikari_add_qttest(hikari_ui_workspace_layout_tests SOURCES workspace_layout_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
# WS1 (ADR 0020): each Document tab's panel arrangement, its Session round
# trip and recovery.
hikari_add_qttest(hikari_ui_tab_layouts_tests SOURCES tab_layouts_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
target_compile_definitions(hikari_ui_tab_layouts_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_tab_layouts_tests hikari_media_fixtures hikari_media_helper)
# WS2: the tab's arrangement in an ASS file's [Hikari] section. Seven parts,
# each its own process under the default timeout, 120 s (60 s in decision 38g): a refusal row
# starts the application as the other functions do, and in one process the
# suite took 55 s here and timed out in CI on both platforms.
hikari_add_qttest(hikari_ui_file_layouts_tests SOURCES file_layouts_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets
    PARTS arrangements refusals=anUnusableFileArrangementFollowsTheNewTabRule
          bounds=aLayoutLargerThanThePanelsIsRefused tree=aLayoutTheEngineCannotReadIsRefused
          positions=aPanelPositionTheEngineCannotReadIsRefused places=aWindowPlaceNoWindowCanHaveIsRefused recovery=aRecoveredCopyKeepsItsArrangement)
target_include_directories(hikari_ui_file_layouts_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
# TM1 (#232): Translation mode through the Translation preset, the Original
# text panel and Subtitles > Translation mode.
hikari_add_qttest(hikari_ui_translation_preset_tests SOURCES translation_preset_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
# TP1 (#274): the Typesetting preset's own arrangement and the Styles panel.
hikari_add_qttest(hikari_ui_typesetting_preset_tests SOURCES typesetting_preset_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
# KP1 (#286): the Karaoke preset's arrangement and mode, and the Grid's
# folded runs of generated Lines.
hikari_add_qttest(hikari_ui_karaoke_preset_tests SOURCES karaoke_preset_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets)
# WS3 (#217): the layout presets and saved layouts (save, apply, rename,
# delete, persistence) and the retired View arrangements and editor switch.
# Every function starts the application and its QML, as D2's suite did, so
# it runs in two parts under the default timeout, 120 s (60 s in decision
# 38g, 2026-10-14).
hikari_add_qttest(hikari_ui_saved_layouts_tests SOURCES saved_layouts_tests.cpp
    LIBRARIES hikari_composition hikari_translations KDAB::kddockwidgets # WS3: the presets' names in every catalog
    PARTS layouts kept=unusableLayoutsAreKept)
target_include_directories(hikari_ui_saved_layouts_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_ui_saved_layouts_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_saved_layouts_tests hikari_media_helper hikari_media_fixtures)
# Wave 6's Line editor cards (LE2-LE6) through the composition, with restarts.
hikari_add_qttest(hikari_ui_line_editor_options_tests SOURCES line_editor_options_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_update_checker_tests SOURCES update_checker_tests.cpp LIBRARIES hikari_composition)
# SH3: What's new against a recorded answer of GitHub's release list.
hikari_add_qttest(hikari_ui_whats_new_tests SOURCES whats_new_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_whats_new_tests PRIVATE
    HIKARI_GITHUB_RELEASES="${PROJECT_SOURCE_DIR}/tests/fixtures/github-releases/releases.json")
# AN1 (#404): anonymous usage data against a fake endpoint on the loopback, and
# a program that crashes with the crash handler installed.
add_executable(hikari_crash_probe crash_probe.cpp)
target_link_libraries(hikari_crash_probe PRIVATE hikari_composition)
hikari_add_qttest(hikari_ui_analytics_tests SOURCES analytics_tests.cpp LIBRARIES hikari_composition)
target_include_directories(hikari_ui_analytics_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
# CR1 (#452): crash, freeze and issue reports: forced crashes and freezes
# (the probe), sending against the fake endpoint, the privacy of what goes,
# and the GitHub links against .github/ISSUE_TEMPLATE.
hikari_add_qttest(hikari_ui_crash_report_tests SOURCES crash_report_tests.cpp LIBRARIES hikari_composition)
target_include_directories(hikari_ui_crash_report_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_ui_crash_report_tests PRIVATE HIKARI_CRASH_PROBE="$<TARGET_FILE:hikari_crash_probe>"
    HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_dependencies(hikari_ui_crash_report_tests hikari_crash_probe)
# CR1: the report windows through the whole window, against a fake endpoint.
hikari_add_qttest(hikari_ui_crash_report_session_tests SOURCES crash_report_session_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets Qt6::QuickTest)
target_include_directories(hikari_ui_crash_report_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
# AN1: a scripted session through the window sends only the table's events.
hikari_add_qttest(hikari_ui_analytics_session_tests SOURCES analytics_session_tests.cpp
    LIBRARIES hikari_composition KDAB::kddockwidgets Qt6::QuickTest)
target_include_directories(hikari_ui_analytics_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_ui_analytics_session_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_analytics_session_tests hikari_media_helper hikari_media_fixtures hikari_lua_helper
    hikari_automation_stage)
# ADR 0019: the download, its checks and the hand-off per package kind.
hikari_add_qttest(hikari_ui_update_installer_tests SOURCES update_installer_tests.cpp LIBRARIES hikari_composition)
# ADR 0019: the Windows installing side checks the installer against the release before it runs.
hikari_add_qttest(hikari_ui_update_apply_tests SOURCES update_apply_tests.cpp LIBRARIES hikari_composition)
# For the compiler and moc alike: moc gets no _WIN32 under MSVC (update_apply_tests.cpp).
target_compile_definitions(hikari_ui_update_apply_tests PRIVATE HIKARI_UPDATE_APPLY_WINDOWS_TESTS=$<BOOL:${WIN32}>)
# The stand-in setup that holds the setup mutex: no Qt, so its start does not
# depend on the runner's load (PowerShell's took over launch's 10 s once).
if(WIN32)
    add_executable(hikari_setup_mutex_helper setup_mutex_helper.cpp)
    target_compile_definitions(hikari_ui_update_apply_tests PRIVATE
        HIKARI_SETUP_MUTEX_HELPER="$<TARGET_FILE:hikari_setup_mutex_helper>")
    add_dependencies(hikari_ui_update_apply_tests hikari_setup_mutex_helper)
endif()
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
# V2 playback follow-ups (user decisions 2026-10-14): the seek bar's drag pace,
# the player's frame for the features while playing, the end of the video.
hikari_add_qttest(hikari_ui_playing_frame_tests SOURCES playing_frame_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_playing_frame_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_playing_frame_tests hikari_media_fixtures hikari_media_helper)
# P6: tab traversal and close review through the real UI (Spix workflow).
hikari_add_spix_test(hikari_ui_tabs_workflow SOURCES tabs_workflow.cpp LIBRARIES hikari_composition)
# P9: the tab menu, tab reordering, folders, drops, associated files and the
# legacy autosaves through the composition and the real QML.
hikari_add_qttest(hikari_ui_tab_menu_tests SOURCES tab_menu_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_tab_menu_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_tab_menu_tests hikari_media_fixtures hikari_media_helper)
foreach(mode offscreen xvfb native)
    if(TEST hikari_ui_tab_menu_tests.${mode})
        set_tests_properties(hikari_ui_tab_menu_tests.${mode} PROPERTIES TIMEOUT 300)
    endif()
endforeach()
# WS5: one instance per user session: the hand-over, a crashed or silent
# holder, the session's own socket; and what is handed over, on the holder.
hikari_add_qttest(hikari_ui_single_instance_tests SOURCES single_instance_tests.cpp LIBRARIES hikari_composition)
# UD1 (#362): the user folder's location, seeding by manifest and the move in.
hikari_add_qttest(hikari_ui_user_folder_tests SOURCES user_folder_tests.cpp LIBRARIES hikari_composition)
hikari_add_qttest(hikari_ui_forwarded_launches_tests SOURCES forwarded_launches_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_forwarded_launches_tests PRIVATE
    HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_forwarded_launches_tests hikari_media_fixtures hikari_media_helper)
# 38e: no test reaches the desktop's programs (tests/support/desktop): the
# recording handlers, the refusal that fails a run, the shell's hand-offs.
hikari_add_qttest(hikari_ui_desktop_handoff_tests SOURCES desktop_handoff_tests.cpp LIBRARIES hikari_composition)
# DX1 (#367): User guide (F1, the DG1 windows' "?") and the docs export.
hikari_add_qttest(hikari_ui_user_guide_tests SOURCES user_guide_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_user_guide_tests PRIVATE HIKARI_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/ui")
hikari_add_qttest(hikari_ui_docs_export_tests SOURCES docs_export_tests.cpp LIBRARIES hikari_composition)
target_compile_definitions(hikari_ui_docs_export_tests PRIVATE HIKARI_PROGRAM="$<TARGET_FILE:hikarisub>"
    HIKARI_VERSION="${HIKARI_VERSION}")
add_dependencies(hikari_ui_docs_export_tests hikarisub)
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
# K2 on Linux: Follow system theme through the XDG settings portal, on a
# private bus with a fake portal (portal_colour_scheme.h).
if(UNIX AND NOT APPLE)
    target_link_libraries(hikari_ui_theme_tests PRIVATE Qt6::DBus)
    target_compile_definitions(hikari_ui_theme_tests PRIVATE HIKARI_COLOUR_SCHEME_PORTAL)
endif()
# K2 on Windows: the window system's title bar of every framed window follows
# the theme (title_bars.h); on Windows also read back from the window system
# in real windows (the .windows entry, the windows platform).
hikari_add_qttest(hikari_ui_title_bar_tests SOURCES title_bar_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2)
if(WIN32)
    add_test(NAME hikari_ui_title_bar_tests.windows
        COMMAND $<TARGET_FILE:hikari_ui_title_bar_tests> windowsTitleBarFollowsTheTheme
            -o "${CMAKE_BINARY_DIR}/qttest-hikari_ui_title_bar_tests-windows.txt,txt" -o "-,txt")
    set_tests_properties(hikari_ui_title_bar_tests.windows PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows" LABELS "qttest;windows" TIMEOUT 120)
endif()
# SH1: the main window's frame: the title bar's default by desktop, the
# frameless window's hints, its parts as the window system is told of them
# and Alt alone (window_frame.h).
hikari_add_qttest(hikari_ui_window_frame_tests SOURCES window_frame_tests.cpp LIBRARIES hikari_ui)
# DG1: the dialog frame (DialogWindow.qml, dialog_placement.h): the frame's
# buttons, the first and later places, Esc, Enter, the keyboard's return,
# modality and closing with the opener.
hikari_add_qttest(hikari_ui_dialog_frame_tests SOURCES dialog_frame_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2)
# PK1: the popup kit (MessageBox, Popover, ToastStack, InlineBar and the
# forms on them: clash review, Choose styles, hotkey capture, list chooser,
# date picker).
hikari_add_qttest(hikari_ui_popup_kit_tests SOURCES popup_kit_tests.cpp
    LIBRARIES hikari_ui hikari_uiplugin Qt6::QuickControls2 Qt6::QuickTest)
foreach(scale 150 200)
    math(EXPR whole "${scale} / 100")
    math(EXPR part "${scale} % 100")
    add_test(NAME hikari_ui_icon_tests.scale${scale}
        COMMAND $<TARGET_FILE:hikari_ui_icon_tests> contactSheets iconsFollowTheThemeLive mirroredIconsPaintFlipped
            imageSourcedControlsDrawTheSet)
    set_tests_properties(hikari_ui_icon_tests.scale${scale} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${whole}.${part}" LABELS "qttest;offscreen" TIMEOUT 120)
endforeach()
# The contact sheets on pages of three rows, as a small desktop pages them:
# each page drawn whole and written as -pageN.
add_test(NAME hikari_ui_icon_tests.pages COMMAND $<TARGET_FILE:hikari_ui_icon_tests> contactSheets)
set_tests_properties(hikari_ui_icon_tests.pages PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HIKARI_ICON_SHEET_PAGE_ROWS=3;HIKARI_ICON_SHEET_DIR=${CMAKE_BINARY_DIR}/icon-sheet-pages"
    LABELS "qttest;offscreen" TIMEOUT 120)
# V6: Lines timed from the video (Spix workflow).
hikari_add_spix_test(hikari_ui_video_timing_workflow SOURCES video_timing_workflow.cpp LIBRARIES hikari_composition
    DEFINITIONS HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
                HIKARI_MEDIA_FIXTURES="${CMAKE_BINARY_DIR}/tests/backends/media-fixtures")
add_dependencies(hikari_ui_video_timing_workflow hikari_media_helper hikari_media_fixtures)
# Y7: the colour picker's option, simple-picker recent colour and screen sampler.
hikari_add_qttest(hikari_ui_colour_picker_tests SOURCES colour_picker_tests.cpp LIBRARIES hikari_ui)
# SY3: font lists drawing each family in its own face.
hikari_add_qttest(hikari_ui_font_faces_tests SOURCES font_faces_tests.cpp LIBRARIES hikari_ui)
target_compile_definitions(hikari_ui_font_faces_tests PRIVATE
    HIKARI_SYMBOL_FONT="${PROJECT_SOURCE_DIR}/tests/fixtures/libass-fork/forktest-symf020.ttf"
    HIKARI_NO_LETTERS_FONT="${PROJECT_SOURCE_DIR}/tests/fixtures/libass-fork/forktest-arabic.ttf")
# O5: the interface language over the embedded catalogs (live switching,
# aegisub.gettext through the application, pseudolocalized and right-to-left
# layouts, the program font).
hikari_add_qttest(hikari_ui_language_tests SOURCES language_tests.cpp LIBRARIES hikari_composition hikari_translations)
target_compile_definitions(hikari_ui_language_tests PRIVATE HIKARI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    HIKARI_LUA_FIXTURES="${PROJECT_SOURCE_DIR}/tests/backends/lua-fixtures")
add_dependencies(hikari_ui_language_tests hikari_lua_helper hikari_automation_stage)
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS LinguistTools)
# O5-qt-catalog: a HikariSub and a Qt catalog that answer the same string,
# compiled beside the test, for the catalogs' precedence.
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

# The UI tests' environment in every test and workflow above, and in any
# added later (ui_test_environment.cpp says why), and the recorded desktop
# (38e, tests/support/desktop/desktop_guard.h): the recording URL handlers in
# each, the application's desktop routes in each that links the application.
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
