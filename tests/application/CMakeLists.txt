hikari_add_gtest(hikari_application_write_tests SOURCES write_coordinator_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_edit_tests SOURCES edit_session_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_workspace_tests SOURCES workspace_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_document_files_tests SOURCES document_files_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_macro_tests SOURCES macro_transaction_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_automation_services_tests SOURCES automation_services_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_video_session_tests SOURCES video_session_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_selection_tests SOURCES grid_selection_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_commands_tests SOURCES grid_commands_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_clipboard_tests SOURCES grid_clipboard_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_filtering_tests SOURCES grid_filtering_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_groups_tests SOURCES grid_groups_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_recovery_store_tests SOURCES recovery_store_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_automation_hotkeys_tests SOURCES automation_hotkeys_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_split_tests SOURCES grid_split_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_recent_files_tests SOURCES recent_files_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_translation_tests SOURCES grid_translation_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_script_properties_tests SOURCES script_properties_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_shift_times_tests SOURCES shift_times_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_select_lines_tests SOURCES select_lines_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_update_check_tests SOURCES update_check_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_resample_tests SOURCES resample_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_style_manager_tests SOURCES style_manager_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_misspell_replacer_tests SOURCES misspell_replacer_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_spell_checker_tests SOURCES spell_checker_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_include_directories(hikari_application_spell_checker_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_gtest(hikari_application_settings_tests SOURCES settings_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_options_dialog_tests SOURCES options_dialog_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_find_replace_tests SOURCES find_replace_tests.cpp
    LIBRARIES hikari_application LABELS application)
# F1: the legacy Find and replace captures (tools/legacy-capture ui cases)
# replayed through FindReplace. Qt Core only reads the JSON.
hikari_add_gtest(hikari_application_find_replace_capture_tests SOURCES find_replace_capture_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_find_replace_capture_tests PRIVATE
    HIKARI_LEGACY_CAPTURE_PLAN="${PROJECT_SOURCE_DIR}/tools/legacy-capture/plan.json"
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs"
    HIKARI_F1F3_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-37216509697/observations.json"
    HIKARI_CAPTURE_ARTIFACTS="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
hikari_add_gtest(hikari_application_audio_display_tests SOURCES audio_display_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_audio_spectrum_tests SOURCES audio_spectrum_tests.cpp
    LIBRARIES hikari_application LABELS application)
# A4: the audio box's playback modes, Stop and the playback cursor.
hikari_add_gtest(hikari_application_audio_playback_tests SOURCES audio_playback_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_legacy_dir_tests SOURCES legacy_dir_tests.cpp
    LIBRARIES hikari_application LABELS application)
# O2: hotkeys and the shortcut editor, against the legacy sources.
hikari_add_gtest(hikari_application_hotkeys_tests SOURCES hotkeys_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_compile_definitions(hikari_application_hotkeys_tests PRIVATE
    HIKARI_LEGACY_DIR="${PROJECT_SOURCE_DIR}/HikariSub"
    HIKARI_ACTIONS_CSV="${PROJECT_SOURCE_DIR}/docs/qt/proposals/settings-migration-map/actions.csv")
# A3: the audio box's timing.
hikari_add_gtest(hikari_application_audio_timing_tests SOURCES audio_timing_tests.cpp
    LIBRARIES hikari_application LABELS application)
# A5: the audio box's karaoke mode, with the legacy probe's observations.
hikari_add_gtest(hikari_application_audio_karaoke_tests SOURCES audio_karaoke_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_audio_karaoke_tests PRIVATE
    HIKARI_KARAOKE_CASES="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs/karaoke-cases.txt"
    HIKARI_KARAOKE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-a5-karaoke-20261005/observations.jsonl")
# P6: legacy session files against fixtures in the legacy format.
hikari_add_gtest(hikari_application_session_file_tests SOURCES session_file_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_compile_definitions(hikari_application_session_file_tests PRIVATE
    HIKARI_SESSION_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/sessions")
# R1: subtitle comparison criteria, text differences and the menu's state.
hikari_add_gtest(hikari_application_subtitle_comparison_tests SOURCES subtitle_comparison_tests.cpp
    LIBRARIES hikari_application LABELS application)
# Y8: the font collector's scan, review and output rules.
hikari_add_gtest(hikari_application_font_collector_tests SOURCES font_collector_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_include_directories(hikari_application_font_collector_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
# T1: the visual tools' transform, crosshair and gestures, with the legacy
# probe's observations.
hikari_add_gtest(hikari_application_visual_tools_tests SOURCES visual_tools_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_visual_tools_tests PRIVATE
    HIKARI_LEGACY_DIR="${PROJECT_SOURCE_DIR}/HikariSub"
    HIKARI_VISUAL_CASES="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs/visual-cases.txt"
    HIKARI_VISUAL_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t1-visual-20261005/observations.jsonl")
# V6: timing and selecting Lines from the video and the video following the
# active Line, with the legacy probe's observations.
hikari_add_gtest(hikari_application_video_timing_tests SOURCES video_timing_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_video_timing_tests PRIVATE
    HIKARI_VIDEO_TIMING_CASES="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs/video-timing-cases.txt"
    HIKARI_VIDEO_TIMING_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-v6-video-timing-20261005/observations.jsonl")
