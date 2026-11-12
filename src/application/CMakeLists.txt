add_library(hikari_application STATIC module.cpp write_coordinator.cpp edit_session.cpp ai_review.cpp word_diff.cpp workspace.cpp document_files.cpp
    automation_registry.cpp macro_transaction.cpp template_preview.cpp generated_runs.cpp automation_services.cpp extents_cache.cpp compile_cache.cpp media_association.cpp video_session.cpp seek_throttle.cpp grid_selection.cpp grid_commands.cpp recent_files.cpp grid_clipboard.cpp lyrics_insert.cpp grid_filtering.cpp grid_groups.cpp recovery_store.cpp automation_hotkeys.cpp legacy_timebase.cpp aegisub_frame_times.cpp editor_fields.cpp grid_split.cpp frame_bake.cpp override_blocks.cpp layer_builders.cpp grid_translation.cpp script_properties.cpp shift_times.cpp keyframe_files.cpp select_lines.cpp update_check.cpp resample.cpp script_resolution.cpp style_manager.cpp tiers.cpp spell_checker.cpp settings.cpp options_dialog.cpp getting_started.cpp audio_display.cpp audio_box.cpp fold_groups.cpp timecodes_file.cpp
    dialog_memory.cpp dependency_control.cpp)
target_sources(hikari_application PRIVATE text_commands.cpp include/hikari/application/text_commands.h)
target_sources(hikari_application PRIVATE hotkeys.cpp)
target_sources(hikari_application PRIVATE ai_inline.cpp include/hikari/application/ai_inline.h)
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
target_sources(hikari_application PRIVATE karaoke_templates.cpp include/hikari/application/karaoke_templates.h)
target_sources(hikari_application PRIVATE karaoke_syllables.cpp include/hikari/application/karaoke_syllables.h)
target_sources(hikari_application PRIVATE node_catalogue.cpp node_graph.cpp node_graph_library.cpp node_validation.cpp karaoke_layout.cpp node_graph_editing.cpp node_graph_presets.cpp
    include/hikari/application/karaoke_layout.h node_effects.cpp include/hikari/application/node_catalogue.h
    include/hikari/application/node_graph.h include/hikari/application/node_graph_library.h
    include/hikari/application/node_effects.h include/hikari/application/node_graph_editing.h)
# Generated Lines must be byte-identical on every platform.
set_source_files_properties(karaoke_layout.cpp node_effects.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE shape_yutils.cpp shape_geometry.cpp include/hikari/application/shape_kernel.h)
target_link_libraries(hikari_application PRIVATE Hikari::clipper2)
set_source_files_properties(shape_yutils.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE guide_links.cpp include/hikari/application/guide_links.h)
target_sources(hikari_application PRIVATE ai_providers.cpp include/hikari/application/ai_providers.h)
target_sources(hikari_application PRIVATE search_suggestions.cpp include/hikari/application/search_suggestions.h)
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
target_sources(hikari_application PRIVATE update_signature.cpp include/hikari/application/update_signature.h)
target_link_libraries(hikari_application PUBLIC hikari_core hikari_json hikari_minisign)
hikari_module(hikari_application ALLOW hikari_core hikari_json hikari_minisign Hikari::clipper2)
target_sources(hikari_application PRIVATE session_file.cpp)
target_sources(hikari_application PRIVATE document_scripts.cpp)
target_sources(hikari_application PRIVATE macro_validation.cpp)
target_sources(hikari_application PRIVATE subtitle_comparison.cpp)
target_sources(hikari_application PRIVATE reference_navigation.cpp)
target_sources(hikari_application PRIVATE associated_files.cpp legacy_autosaves.cpp project.cpp project_automation.cpp translation_project.cpp)
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
target_sources(hikari_application PRIVATE position_commands.cpp)
target_sources(hikari_application PRIVATE visual_vector.cpp visual_clip.cpp)
target_sources(hikari_application PRIVATE visual_perspective.cpp visual_gradient.cpp visual_text_on_path.cpp)
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
target_sources(hikari_application PRIVATE video_controls.cpp video_snapshot.cpp video_matrix.cpp video_details.cpp video_properties.cpp subtitle_render.cpp
    document_overlay.cpp)
set_source_files_properties(video_controls.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
target_sources(hikari_application PRIVATE video_fullscreen.cpp)
target_sources(hikari_application PRIVATE video_timing.cpp line_retime.h timing_helpers.cpp)
target_sources(hikari_application PRIVATE center_times.cpp)
target_sources(hikari_application PRIVATE align_to_video.cpp)
target_sources(hikari_application PRIVATE video_analysis.cpp)
target_sources(hikari_application PRIVATE qc_notes.cpp)
target_sources(hikari_application PRIVATE perspective_tracking.cpp motion_data.cpp tag_removal.cpp motion_tags.cpp motion_fit.cpp)
target_sources(hikari_application PRIVATE tag_lerp.cpp gradient_geometry.cpp gradient.cpp interpolate.cpp text_on_path.cpp)
target_sources(hikari_application PRIVATE translation_mode.cpp)
target_sources(hikari_application PRIVATE owned_file.cpp include/hikari/application/owned_file.h)
