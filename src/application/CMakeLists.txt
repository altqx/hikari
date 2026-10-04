# Application services: Documents, commands, history, settings and ports.
add_library(hikari_application STATIC module.cpp write_coordinator.cpp edit_session.cpp workspace.cpp document_files.cpp
    automation_registry.cpp macro_transaction.cpp automation_services.cpp media_association.cpp video_session.cpp grid_selection.cpp grid_commands.cpp recent_files.cpp grid_clipboard.cpp grid_filtering.cpp grid_groups.cpp recovery_store.cpp automation_hotkeys.cpp legacy_timebase.cpp grid_split.cpp grid_translation.cpp script_properties.cpp shift_times.cpp keyframe_files.cpp select_lines.cpp update_check.cpp resample.cpp style_manager.cpp misspell_replacer.cpp spell_checker.cpp settings.cpp options_dialog.cpp audio_display.cpp audio_box.cpp)
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
