hikari_add_gtest(hikari_application_write_tests SOURCES write_coordinator_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_edit_tests SOURCES edit_session_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_editor_fields_tests SOURCES editor_fields_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_workspace_tests SOURCES workspace_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_document_files_tests SOURCES document_files_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_macro_tests SOURCES macro_transaction_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_template_preview_tests SOURCES template_preview_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_generated_runs_tests SOURCES generated_runs_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_document_scripts_tests SOURCES document_scripts_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_macro_validation_tests SOURCES macro_validation_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_dialog_memory_tests SOURCES dialog_memory_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_automation_services_tests SOURCES automation_services_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_playback_pipeline_tests SOURCES playback_pipeline_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_video_session_tests SOURCES video_session_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_seek_throttle_tests SOURCES seek_throttle_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_video_sources_tests SOURCES video_sources_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_grid_selection_tests SOURCES grid_selection_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_changed_lines_tests SOURCES changed_lines_tests.cpp
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
hikari_add_gtest(hikari_application_whats_new_tests SOURCES whats_new_tests.cpp
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
hikari_add_gtest(hikari_application_getting_started_tests SOURCES getting_started_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_find_replace_tests SOURCES find_replace_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_find_replace_capture_tests SOURCES find_replace_capture_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_find_replace_capture_tests PRIVATE
    HIKARI_LEGACY_PLAN="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/plan.json"
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs"
    HIKARI_F1F3_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-37216509697/observations.json"
    HIKARI_CAPTURE_ARTIFACTS="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
hikari_add_gtest(hikari_application_audio_display_tests SOURCES audio_display_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_audio_spectrum_tests SOURCES audio_spectrum_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_audio_devices_tests SOURCES audio_devices_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_audio_playback_tests SOURCES audio_playback_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_legacy_dir_tests SOURCES legacy_dir_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_hotkeys_tests SOURCES hotkeys_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_compile_definitions(hikari_application_hotkeys_tests PRIVATE
    HIKARI_ACTIONS_CSV="${PROJECT_SOURCE_DIR}/docs/proposals/settings-migration-map/actions.csv")
hikari_add_gtest(hikari_application_audio_timing_tests SOURCES audio_timing_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_karaoke_tap_tests SOURCES karaoke_tap_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_vocal_onsets_tests SOURCES vocal_onsets_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_furigana_tests SOURCES furigana_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_audio_karaoke_tests SOURCES audio_karaoke_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_audio_karaoke_tests PRIVATE
    HIKARI_KARAOKE_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/karaoke-cases.txt"
    HIKARI_KARAOKE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-a5-karaoke-20261005/observations.jsonl")
hikari_add_gtest(hikari_application_karaoke_split_tests SOURCES karaoke_split_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_compile_definitions(hikari_application_karaoke_split_tests PRIVATE
    HIKARI_HYPHENATION_PATTERNS="${PROJECT_SOURCE_DIR}/src/ui/hyphenation/hyph_en_US.dic")
hikari_add_gtest(hikari_application_kanji_timer_tests SOURCES kanji_timer_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_session_file_tests SOURCES session_file_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_compile_definitions(hikari_application_session_file_tests PRIVATE
    HIKARI_SESSION_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/sessions")
target_include_directories(hikari_application_session_file_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_gtest(hikari_application_subtitle_comparison_tests SOURCES subtitle_comparison_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_subtitle_comparison_tests PRIVATE
    HIKARI_COMPARISON_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/comparison-cases.txt"
    HIKARI_COMPARISON_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-r1-comparison-20261005/observations.jsonl")
hikari_add_gtest(hikari_application_reference_navigation_tests SOURCES reference_navigation_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_font_collector_tests SOURCES font_collector_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_include_directories(hikari_application_font_collector_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
hikari_add_gtest(hikari_application_matroska_tests SOURCES matroska_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_visual_tools_tests SOURCES visual_tools_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_visual_tools_tests PRIVATE
    HIKARI_VISUAL_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/visual-cases.txt"
    HIKARI_VISUAL_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t1-visual-20261005/observations.jsonl")
hikari_add_gtest(hikari_application_settings_import_tests SOURCES settings_import_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_compile_definitions(hikari_application_settings_import_tests PRIVATE
    HIKARI_MIGRATION_MAP="${PROJECT_SOURCE_DIR}/docs/proposals/settings-migration-map")
hikari_add_gtest(hikari_application_font_catalogs_tests SOURCES font_catalogs_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_include_directories(hikari_application_font_catalogs_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_application_font_catalogs_tests PRIVATE
    HIKARI_FONT_CATALOG_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/font-catalogs")
hikari_add_gtest(hikari_application_visual_position_tests SOURCES visual_position_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_visual_position_tests PRIVATE
    HIKARI_POSITION_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/position-cases.txt"
    HIKARI_POSITION_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t2-position-20261005/observations.jsonl")
hikari_add_gtest(hikari_application_visual_transform_tests SOURCES visual_transform_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_visual_transform_tests PRIVATE
    HIKARI_VISUAL_T3_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/visual-t3-cases.txt"
    HIKARI_VISUAL_T3_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t3-visual-20261005/observations.jsonl"
    HIKARI_VISUAL_T3_MEASURE_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/visual-t3-measure-cases.txt"
    HIKARI_VISUAL_T3_MEASURE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t3-measure-20261012/observations.jsonl")
hikari_add_gtest(hikari_application_visual_perspective_tests SOURCES visual_perspective_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
hikari_add_gtest(hikari_application_visual_clip_tests SOURCES visual_clip_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_visual_clip_tests PRIVATE
    HIKARI_CLIP_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/clip-cases.txt"
    HIKARI_CLIP_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t4-clip-20261005/observations.jsonl")
hikari_add_gtest(hikari_application_visual_drawing_tests SOURCES visual_drawing_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_visual_drawing_tests PRIVATE
    HIKARI_DRAWING_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/drawing-cases.txt"
    HIKARI_DRAWING_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t5-drawing-20261005/observations.jsonl")
hikari_add_gtest(hikari_application_visual_shift_tests SOURCES visual_shift_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_visual_shift_tests PRIVATE
    HIKARI_VISUAL_T6_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/visual-t6-cases.txt"
    HIKARI_VISUAL_T6_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-t6-visual-20261006/observations.jsonl")
hikari_add_gtest(hikari_application_video_view_tests SOURCES video_view_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_video_fullscreen_tests SOURCES video_fullscreen_tests.cpp
    LIBRARIES hikari_application LABELS application)
hikari_add_gtest(hikari_application_video_timing_tests SOURCES video_timing_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_video_timing_tests PRIVATE
    HIKARI_VIDEO_TIMING_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/video-timing-cases.txt"
    HIKARI_VIDEO_TIMING_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-v6-video-timing-20261005/observations.jsonl")
hikari_add_gtest(hikari_application_translation_mode_tests SOURCES translation_mode_tests.cpp
    LIBRARIES hikari_application Qt6::Core LABELS application)
target_compile_definitions(hikari_application_translation_mode_tests PRIVATE
    HIKARI_TAG_MOVE_CASES="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs/tag-move-cases.txt"
    HIKARI_TAG_MOVE_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-e5-tag-move-20261012/observations.jsonl")
hikari_add_gtest(hikari_application_associated_files_tests SOURCES associated_files_tests.cpp legacy_autosaves_tests.cpp
    LIBRARIES hikari_application LABELS application)
target_compile_definitions(hikari_application_associated_files_tests PRIVATE
    HIKARI_ASSOCIATION_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/associations")
