add_library(hikari_composition STATIC composition.cpp application.cpp include/hikari/app/application.h
    application_tabs.cpp
    session_json.cpp include/hikari/app/session_json.h
    application_comparison.cpp
    application_video.cpp
    application_reference.cpp
    application_tab_menu.cpp
    application_karaoke.cpp
    automation_shell.cpp include/hikari/app/automation_shell.h
    automation_hotkeys_controller.cpp include/hikari/app/automation_hotkeys_controller.h
    hotkeys_controller.cpp include/hikari/app/hotkeys_controller.h
    update_checker.cpp include/hikari/app/update_checker.h
    update_installer.cpp include/hikari/app/update_installer.h
    update_apply.cpp include/hikari/app/update_apply.h
    whats_new.cpp include/hikari/app/whats_new.h
    package_kind.cpp include/hikari/app/package_kind.h
    user_folder.cpp include/hikari/app/user_folder.h
    style_manager_controller.cpp include/hikari/app/style_manager_controller.h
    kanji_timer_controller.cpp include/hikari/app/kanji_timer_controller.h
    local_only_network.cpp include/hikari/app/local_only_network.h)
set_target_properties(hikari_composition PROPERTIES AUTOMOC ON)
target_include_directories(hikari_composition PUBLIC include)
target_link_libraries(hikari_composition PUBLIC hikari_ui hikari_uiplugin hikari_backends hikari_application hikari_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network)
# QZipWriter is private Qt API.
find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS CorePrivate)
target_link_libraries(hikari_composition PRIVATE Qt6::CorePrivate)
target_compile_definitions(hikari_composition PRIVATE HIKARI_BUILD_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_BUILD_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>" HIKARI_BUILD_AUTOMATION="${HIKARI_AUTOMATION_ROOT}"
    HIKARI_BUILD_HARPER_LS="${HIKARI_HARPER_LS}" HIKARI_HARPER_VERSION="${HIKARI_HARPER_VERSION}"
    HIKARI_BUILD_UNIDIC="${HIKARI_UNIDIC_DIR}"
    HIKARI_VERSION="${HIKARI_VERSION}")
add_dependencies(hikari_composition hikari_media_helper hikari_lua_helper hikari_automation_stage)
option(HIKARI_UPDATE_TEST_FEED "Read the update feed and package kind from the environment (test builds only)" OFF)
if(HIKARI_UPDATE_TEST_FEED)
    target_compile_definitions(hikari_composition PRIVATE HIKARI_UPDATE_TEST_FEED)
endif()
if(WIN32)
    target_link_libraries(hikari_composition PRIVATE shell32 winhttp bcrypt crypt32 advapi32)
endif()
hikari_module(hikari_composition ALLOW hikari_ui hikari_ui_resources_1 hikari_uiplugin hikari_backends
    hikari_application hikari_core "Qt6::*" "hikari_ui*")

target_sources(hikari_composition PRIVATE startup_look.cpp include/hikari/app/startup_look.h
    include/hikari/app/launcher.h)
if(WIN32)
    add_library(hikarisub_app SHARED windows/app_library.cpp)
    target_link_libraries(hikarisub_app PRIVATE hikari_composition hikari_translations)
    set_target_properties(hikarisub_app PROPERTIES OUTPUT_NAME HikariSubApp)
    hikari_module(hikarisub_app ALLOW hikari_composition hikari_translations "Qt6::*")
    add_executable(hikarisub WIN32 windows/launcher.cpp)
    target_include_directories(hikarisub PRIVATE include ../application/include windows)
    target_compile_definitions(hikarisub PRIVATE "HIKARI_VERSION_W=L\"${HIKARI_VERSION}\"")
    target_link_libraries(hikarisub PRIVATE dwmapi windowscodecs ole32)
    add_dependencies(hikarisub hikarisub_app)
else()
    qt_add_executable(hikarisub main.cpp)
    target_link_libraries(hikarisub PRIVATE hikari_composition hikari_translations)
    hikari_module(hikarisub ALLOW hikari_composition hikari_translations "Qt6::*")
endif()
option(HIKARI_QML_HOT_RELOAD "Development only: QML debugging and the hikarisub_qmlpreview target" OFF)
if(HIKARI_QML_HOT_RELOAD)
    target_compile_definitions(hikari_composition PRIVATE QT_QML_DEBUG)
    find_program(HIKARI_QMLPREVIEW qmlpreview HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" NO_DEFAULT_PATH REQUIRED)
    # Qt makes <exe>_qmlpreview only for the executable that owns the QML module; ours live in libraries.
    set(hikari_qrc_args)
    foreach(module hikari_ui hikari_ui_style)
        list(APPEND hikari_qrc_args "$<LIST:TRANSFORM,$<TARGET_PROPERTY:${module},_qt_generated_qrc_files>,PREPEND,--resource=>")
    endforeach()
    add_custom_target(hikarisub_qmlpreview
        COMMAND "${HIKARI_QMLPREVIEW}" --interactive ${hikari_qrc_args} "$<TARGET_FILE:hikarisub>"
        COMMAND_EXPAND_LISTS
        DEPENDS hikarisub
        USES_TERMINAL)
endif()
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE HIKARI_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
# In CI's container git refuses the checkout (dubious ownership); Actions sets GITHUB_SHA.
if(NOT HIKARI_COMMIT AND DEFINED ENV{GITHUB_SHA})
    set(HIKARI_COMMIT "$ENV{GITHUB_SHA}")
endif()
target_sources(hikari_composition PRIVATE docs_export.cpp include/hikari/app/docs_export.h)
set_source_files_properties(docs_export.cpp PROPERTIES COMPILE_DEFINITIONS "HIKARI_COMMIT=\"${HIKARI_COMMIT}\"")
set(HIKARI_POSTHOG_KEY "" CACHE STRING "PostHog project key for anonymous usage data (release builds only)")
target_sources(hikari_composition PRIVATE analytics.cpp analytics_key.cpp include/hikari/app/analytics.h
    crash_record.cpp include/hikari/app/crash_record.h
    crash_report.cpp include/hikari/app/crash_report.h
    crash_reports.cpp include/hikari/app/crash_reports.h
    hang_watchdog.cpp include/hikari/app/hang_watchdog.h)
set_source_files_properties(analytics_key.cpp PROPERTIES COMPILE_DEFINITIONS "HIKARI_POSTHOG_KEY=\"${HIKARI_POSTHOG_KEY}\"")
if(NOT WIN32)
    target_link_libraries(hikari_composition PRIVATE ${CMAKE_DL_LIBS}) # dladdr
endif()
target_sources(hikari_composition PRIVATE font_collector_controller.cpp include/hikari/app/font_collector_controller.h)
target_sources(hikari_composition PRIVATE settings_import_store.cpp include/hikari/app/settings_import_store.h
    settings_import_controller.cpp include/hikari/app/settings_import_controller.h)
target_sources(hikari_composition PRIVATE font_catalogs_controller.cpp include/hikari/app/font_catalogs_controller.h)
target_sources(hikari_composition PRIVATE single_instance.cpp include/hikari/app/single_instance.h
    forwarded_launches.cpp include/hikari/app/forwarded_launches.h)
target_sources(hikari_composition PRIVATE matroska_controller.cpp include/hikari/app/matroska_controller.h)
target_sources(hikari_composition PRIVATE application_video_timing.cpp)
target_sources(hikari_composition PRIVATE application_translation.cpp)
target_sources(hikari_composition PRIVATE localisation.cpp include/hikari/app/localisation.h application_language.cpp)
target_sources(hikari_composition PRIVATE application_grammar.cpp)
target_sources(hikari_composition PRIVATE application_audio.cpp)
target_sources(hikari_composition PRIVATE application_directshow.cpp)
if(WIN32)
    target_link_libraries(hikari_composition PUBLIC hikari_directshow)
    set_property(TARGET hikari_composition APPEND PROPERTY HIKARI_ALLOWED_LINKS hikari_directshow)
endif()
# HikariSub legacy's shortcut, App Paths key and file associations name HikariSub.exe and its
# icon resource ids, so neither may change.
if(WIN32)
    set(HIKARI_RC_VERSION "${PROJECT_VERSION}")
    if(HIKARI_VERSION_SUFFIX)
        string(APPEND HIKARI_RC_VERSION "-${HIKARI_VERSION_SUFFIX}")
    endif()
    set(HIKARI_SPLASH_RC "")
    set(id 201)
    foreach(theme dark light)
        string(TOUPPER "${theme}" upper)
        target_compile_definitions(hikarisub PRIVATE HIKARI_SPLASH_${upper}_FIRST=${id})
        foreach(scale 100 150 200 300)
            set(art "${CMAKE_CURRENT_SOURCE_DIR}/windows/splash/splash-${theme}-${scale}.png")
            if(EXISTS "${art}")
                string(APPEND HIKARI_SPLASH_RC "${id} RCDATA \"${art}\"\n")
            endif()
            math(EXPR id "${id} + 1")
        endforeach()
    endforeach()
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/windows/splash")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS windows/splash)
    endif()
    configure_file(windows/hikarisub.rc.in "${CMAKE_CURRENT_BINARY_DIR}/hikarisub.rc" @ONLY)
    target_sources(hikarisub PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/hikarisub.rc")
    set_target_properties(hikarisub PROPERTIES OUTPUT_NAME HikariSub WIN32_EXECUTABLE TRUE)
    include("${PROJECT_SOURCE_DIR}/cmake/HikariPackageWindows.cmake")
endif()
