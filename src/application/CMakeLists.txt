# Application services: Documents, commands, history, settings and ports.
add_library(hikari_application STATIC module.cpp write_coordinator.cpp edit_session.cpp workspace.cpp document_files.cpp
    automation_registry.cpp macro_transaction.cpp automation_services.cpp media_association.cpp video_session.cpp grid_selection.cpp grid_commands.cpp recent_files.cpp grid_clipboard.cpp grid_filtering.cpp grid_groups.cpp recovery_store.cpp automation_hotkeys.cpp legacy_timebase.cpp grid_split.cpp grid_translation.cpp script_properties.cpp shift_times.cpp keyframe_files.cpp select_lines.cpp update_check.cpp)
target_include_directories(hikari_application PUBLIC include)
target_link_libraries(hikari_application PUBLIC hikari_core)
hikari_module(hikari_application ALLOW hikari_core)
