# Application services: Documents, commands, history, settings and ports.
add_library(hikari_application STATIC module.cpp write_coordinator.cpp edit_session.cpp workspace.cpp document_files.cpp
    automation_registry.cpp macro_transaction.cpp automation_services.cpp media_association.cpp video_session.cpp grid_selection.cpp grid_commands.cpp recent_files.cpp grid_clipboard.cpp grid_filtering.cpp grid_groups.cpp recovery_store.cpp automation_hotkeys.cpp legacy_timebase.cpp editor_fields.cpp grid_split.cpp grid_translation.cpp script_properties.cpp shift_times.cpp keyframe_files.cpp select_lines.cpp update_check.cpp resample.cpp style_manager.cpp misspell_replacer.cpp spell_checker.cpp settings.cpp options_dialog.cpp audio_display.cpp audio_box.cpp)
# O2: hotkeys and the shortcut editor.
target_sources(hikari_application PRIVATE hotkeys.cpp)
# A3: the audio box's timing.
target_sources(hikari_application PRIVATE audio_timing.cpp)
# A4: the audio box's playback.
target_sources(hikari_application PRIVATE audio_playback.cpp)
# A5: the audio box's karaoke mode.
target_sources(hikari_application PRIVATE audio_karaoke.cpp)
target_include_directories(hikari_application PUBLIC include)
# F1: find and replace.
target_sources(hikari_application PRIVATE find_replace.cpp)
# A2 and L6: the audio spectrum (the audio box's display and
# aegisub.get_frequency_peaks). Legacy's float FFT must give legacy's bits on
# every compiler, so the file is built without floating-point contraction:
# GCC/Clang -ffp-contract=off, MSVC /fp:precise without /fp:contract (no FMA
# contraction since VS 2022 17.0) and the file's own `#pragma fp_contract`.
target_sources(hikari_application PRIVATE audio_spectrum.cpp)
set_source_files_properties(audio_spectrum.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
# wxDir listing per platform (F1 find in files, F3 dictionaries).
target_sources(hikari_application PRIVATE legacy_dir.cpp)
if(WIN32)
    target_link_libraries(hikari_application PRIVATE shlwapi) # PathMatchSpecW
endif()
target_link_libraries(hikari_application PUBLIC hikari_core)
hikari_module(hikari_application ALLOW hikari_core)
# P6: legacy session files (LastSession.txt, .kls).
target_sources(hikari_application PRIVATE session_file.cpp)
# S4: the scripts a Document names in its Script Info.
target_sources(hikari_application PRIVATE document_scripts.cpp)
# R1: subtitle comparison (legacy SubsComparison and the tab menu's state).
target_sources(hikari_application PRIVATE subtitle_comparison.cpp)
# Y8: the font collector (renderer-verified collection).
target_sources(hikari_application PRIVATE font_collector.cpp)
# Y9: MKV extraction (subtitle tracks and font attachments).
target_sources(hikari_application PRIVATE matroska.cpp)
# T1: the visual tools' shared transform, rail families, gestures, batch
# picker and the crosshair. Legacy's float steps must round as its build did,
# so these files are built without floating-point contraction (as
# audio_spectrum.cpp).
target_sources(hikari_application PRIVATE visual_view.cpp visual_tools.cpp visual_crosshair.cpp)
# T2: Position and Move, and what the families read from the script.
target_sources(hikari_application PRIVATE visual_script.cpp visual_position.cpp)
# T3: Scale and the Z and X/Y rotations, with the tag reading and writing
# they share; legacy's float steps again.
target_sources(hikari_application PRIVATE visual_transform.cpp visual_scale.cpp visual_rotation.cpp)
set_source_files_properties(visual_transform.cpp visual_scale.cpp visual_rotation.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
set_source_files_properties(visual_view.cpp visual_crosshair.cpp visual_script.cpp visual_position.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
# O3: the one-shot legacy settings importer's readers and review plan.
target_sources(hikari_application PRIVATE settings_import.cpp settings_import_tables.cpp)
# Y6: Font catalogs, the picker's family lists and the external fonts folder.
target_sources(hikari_application PRIVATE font_catalogs.cpp font_families.cpp)
# T4: the rectangle and vector clips and the vector point editor T5 shares,
# with legacy's float steps (no contraction either).
target_sources(hikari_application PRIVATE visual_vector.cpp visual_clip.cpp)
set_source_files_properties(visual_vector.cpp visual_clip.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
# V3: the Video panel's sources, tracks and chapters. The dummy video's
# frame times and its checkerboard's HSL step are legacy float arithmetic,
# built without floating-point contraction as above.
target_sources(hikari_application PRIVATE video_sources.cpp)
set_source_files_properties(video_sources.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
# V4: the video view's volume, aspect and wheel zoom, the snapshots and the
# YCbCr matrix. The wheel's zoom is legacy float arithmetic, built as
# visual_view.cpp is.
target_sources(hikari_application PRIVATE video_controls.cpp video_snapshot.cpp video_matrix.cpp)
set_source_files_properties(video_controls.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>;$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>")
# V6: timing and selecting Lines from the video, and the video following the active Line.
target_sources(hikari_application PRIVATE video_timing.cpp)
# E5: translation mode controls (Translator mode, Not confirmed, Moving tags,
# the Grid's Original text / Translation columns).
target_sources(hikari_application PRIVATE translation_mode.cpp)
