add_library(hikari_application STATIC module.cpp write_coordinator.cpp edit_session.cpp workspace.cpp document_files.cpp
    automation_registry.cpp macro_transaction.cpp template_preview.cpp generated_runs.cpp automation_services.cpp media_association.cpp video_session.cpp seek_throttle.cpp grid_selection.cpp grid_commands.cpp recent_files.cpp grid_clipboard.cpp grid_filtering.cpp grid_groups.cpp recovery_store.cpp automation_hotkeys.cpp legacy_timebase.cpp editor_fields.cpp grid_split.cpp grid_translation.cpp script_properties.cpp shift_times.cpp keyframe_files.cpp select_lines.cpp update_check.cpp resample.cpp script_resolution.cpp style_manager.cpp misspell_replacer.cpp spell_checker.cpp settings.cpp options_dialog.cpp getting_started.cpp audio_display.cpp audio_box.cpp
    dialog_memory.cpp)
target_sources(hikari_application PRIVATE hotkeys.cpp)
target_sources(hikari_application PRIVATE audio_timing.cpp)
target_sources(hikari_application PRIVATE audio_devices.cpp include/hikari/application/audio_devices.h)
target_sources(hikari_application PRIVATE audio_playback.cpp)
target_sources(hikari_application PRIVATE audio_karaoke.cpp)
target_sources(hikari_application PRIVATE karaoke_split.cpp include/hikari/application/karaoke_split.h)
target_sources(hikari_application PRIVATE kana.cpp include/hikari/application/kana.h kanji_timer.cpp
    include/hikari/application/kanji_timer.h)
target_sources(hikari_application PRIVATE karaoke_tap.cpp include/hikari/application/karaoke_tap.h)
target_sources(hikari_application PRIVATE vocal_onsets.cpp include/hikari/application/vocal_onsets.h)
target_sources(hikari_application PRIVATE furigana.cpp include/hikari/application/furigana.h)
target_sources(hikari_application PRIVATE karaoke_presets.cpp include/hikari/application/karaoke_presets.h)
target_sources(hikari_application PRIVATE guide_links.cpp include/hikari/application/guide_links.h)
target_include_directories(hikari_application PUBLIC include)
target_sources(hikari_application PRIVATE playback_pipeline.cpp yuv_bgra.cpp
    include/hikari/application/playback_pipeline.h include/hikari/application/yuv_bgra.h)
target_sources(hikari_application PRIVATE ycbcr_frame.cpp include/hikari/application/ycbcr_frame.h)
find_package(Threads REQUIRED)
target_link_libraries(hikari_application PUBLIC Threads::Threads)
target_sources(hikari_application PRIVATE find_replace.cpp)
# Built without FP contraction so float results match HikariSub legacy bit for bit;
# MSVC /fp:precise stopped contracting in VS 2022 17.0.
target_sources(hikari_application PRIVATE audio_spectrum.cpp)
set_source_files_properties(audio_spectrum.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE legacy_dir.cpp)
if(WIN32)
    target_link_libraries(hikari_application PRIVATE shlwapi)
endif()
target_link_libraries(hikari_application PUBLIC hikari_core)
hikari_module(hikari_application ALLOW hikari_core)
target_sources(hikari_application PRIVATE session_file.cpp)
target_sources(hikari_application PRIVATE document_scripts.cpp)
target_sources(hikari_application PRIVATE macro_validation.cpp)
target_sources(hikari_application PRIVATE subtitle_comparison.cpp)
target_sources(hikari_application PRIVATE reference_navigation.cpp)
target_sources(hikari_application PRIVATE associated_files.cpp legacy_autosaves.cpp)
target_sources(hikari_application PRIVATE font_collector.cpp)
target_sources(hikari_application PRIVATE matroska.cpp)
target_sources(hikari_application PRIVATE visual_view.cpp visual_tools.cpp visual_crosshair.cpp)
target_sources(hikari_application PRIVATE visual_script.cpp visual_position.cpp)
target_sources(hikari_application PRIVATE visual_transform.cpp visual_scale.cpp visual_rotation.cpp)
set_source_files_properties(visual_transform.cpp visual_scale.cpp visual_rotation.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
set_source_files_properties(visual_view.cpp visual_crosshair.cpp visual_script.cpp visual_position.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE settings_import.cpp settings_import_tables.cpp)
target_sources(hikari_application PRIVATE font_catalogs.cpp font_families.cpp)
target_sources(hikari_application PRIVATE visual_vector.cpp visual_clip.cpp)
target_sources(hikari_application PRIVATE visual_perspective.cpp)
set_source_files_properties(visual_vector.cpp visual_clip.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE visual_drawing.cpp shape_presets.cpp)
target_sources(hikari_application PRIVATE visual_shift.cpp visual_all_tags.cpp all_tags.cpp)
set_source_files_properties(visual_shift.cpp visual_all_tags.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
set_source_files_properties(visual_drawing.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE video_sources.cpp)
set_source_files_properties(video_sources.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE video_controls.cpp video_snapshot.cpp video_matrix.cpp subtitle_render.cpp)
set_source_files_properties(video_controls.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE video_fullscreen.cpp)
target_sources(hikari_application PRIVATE video_timing.cpp)
target_sources(hikari_application PRIVATE translation_mode.cpp)
target_sources(hikari_application PRIVATE owned_file.cpp include/hikari/application/owned_file.h)
