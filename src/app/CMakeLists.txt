# Composition root: constructs services once, starts the engine, shuts down in
# order. The library lets tests start the same composition without shipping a
# test hook in the executable.
add_library(hikari_composition STATIC composition.cpp application.cpp include/hikari/app/application.h
    application_tabs.cpp # P6: tabs and session restore
    application_comparison.cpp # R1: subtitle comparison
    automation_shell.cpp include/hikari/app/automation_shell.h
    automation_hotkeys_controller.cpp include/hikari/app/automation_hotkeys_controller.h
    hotkeys_controller.cpp include/hikari/app/hotkeys_controller.h
    update_checker.cpp include/hikari/app/update_checker.h
    style_manager_controller.cpp include/hikari/app/style_manager_controller.h)
set_target_properties(hikari_composition PROPERTIES AUTOMOC ON)
target_include_directories(hikari_composition PUBLIC include)
target_link_libraries(hikari_composition PUBLIC hikari_ui hikari_uiplugin hikari_backends hikari_application hikari_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network)
# Running from the build tree, the media helper is not next to the executable.
target_compile_definitions(hikari_composition PRIVATE HIKARI_BUILD_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_BUILD_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>" HIKARI_BUILD_AUTOMATION="${HIKARI_AUTOMATION_ROOT}"
    HIKARI_VERSION="${PROJECT_VERSION}")
add_dependencies(hikari_composition hikari_media_helper hikari_lua_helper hikari_automation_stage)
hikari_module(hikari_composition ALLOW hikari_ui hikari_ui_resources_1 hikari_uiplugin hikari_backends
    hikari_application hikari_core "Qt6::*" "hikari_ui*")

qt_add_executable(hikarisub main.cpp)
target_link_libraries(hikarisub PRIVATE hikari_composition hikari_translations) # O4: QM under :/i18n
hikari_module(hikarisub ALLOW hikari_composition hikari_translations "Qt6::*")
# Y8: the font collector's window state.
target_sources(hikari_composition PRIVATE font_collector_controller.cpp include/hikari/app/font_collector_controller.h)
# W2: with HIKARI_WITH_VSFILTER the build ships xy-VSFilter where legacy did,
# Csri/xy-VSFilter_hikarisub.dll beside the program (.github/scripts/package.py:36
# at 20d647c4), and the About notice names it.
if(HIKARI_WITH_VSFILTER)
    target_compile_definitions(hikari_composition PRIVATE HIKARI_WITH_VSFILTER)
    add_custom_command(TARGET hikarisub POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:hikarisub>/Csri"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:HikariXyVSFilter::csri>"
            "$<TARGET_FILE_DIR:hikarisub>/Csri/xy-VSFilter_hikarisub.dll"
        VERBATIM)
endif()
