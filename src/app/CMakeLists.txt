# Composition root: constructs services once, starts the engine, shuts down in
# order. The library lets tests start the same composition without shipping a
# test hook in the executable.
add_library(hikari_composition STATIC composition.cpp application.cpp include/hikari/app/application.h
    application_tabs.cpp # P6: tabs and session restore
    application_comparison.cpp # R1: subtitle comparison
    application_video.cpp # V3: video sources, tracks and chapters
    application_reference.cpp # R2: reference navigation and the subtitles preview
    application_tab_menu.cpp # P9: the tab menu, folders, drops and associated files
    application_karaoke.cpp # KA7: the Karaoke check window
    automation_shell.cpp include/hikari/app/automation_shell.h
    automation_hotkeys_controller.cpp include/hikari/app/automation_hotkeys_controller.h
    hotkeys_controller.cpp include/hikari/app/hotkeys_controller.h
    update_checker.cpp include/hikari/app/update_checker.h
    update_installer.cpp include/hikari/app/update_installer.h # ADR 0019: download, verify, hand-off
    update_apply.cpp include/hikari/app/update_apply.h # ADR 0019: the Windows installing side
    whats_new.cpp include/hikari/app/whats_new.h # SH3: What's new from the release notes
    package_kind.cpp include/hikari/app/package_kind.h
    user_folder.cpp include/hikari/app/user_folder.h # UD1: the user folder, seeding, the move in
    style_manager_controller.cpp include/hikari/app/style_manager_controller.h
    kanji_timer_controller.cpp include/hikari/app/kanji_timer_controller.h) # KA3: the Kanji timer window
set_target_properties(hikari_composition PROPERTIES AUTOMOC ON)
target_include_directories(hikari_composition PUBLIC include)
target_link_libraries(hikari_composition PUBLIC hikari_ui hikari_uiplugin hikari_backends hikari_application hikari_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network)
# CR1 (#452): QZipWriter, for the subtitle file an issue report attaches.
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS CorePrivate)
target_link_libraries(hikari_composition PRIVATE Qt6::CorePrivate)
# Running from the build tree, the media helper is not next to the executable.
target_compile_definitions(hikari_composition PRIVATE HIKARI_BUILD_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_BUILD_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>" HIKARI_BUILD_AUTOMATION="${HIKARI_AUTOMATION_ROOT}"
    HIKARI_BUILD_HARPER_LS="${HIKARI_HARPER_LS}" HIKARI_HARPER_VERSION="${HIKARI_HARPER_VERSION}"
    HIKARI_BUILD_UNIDIC="${HIKARI_UNIDIC_DIR}" # KA4: the furigana reading dictionary
    HIKARI_VERSION="${HIKARI_VERSION}")
add_dependencies(hikari_composition hikari_media_helper hikari_lua_helper hikari_automation_stage)
# ADR 0019: test builds read HIKARISUB_UPDATE_FEED (a local release feed) and
# HIKARISUB_PACKAGE_KIND; a release build has neither name in it.
option(HIKARI_UPDATE_TEST_FEED "Read the update feed and package kind from the environment (test builds only)" OFF)
if(HIKARI_UPDATE_TEST_FEED)
    target_compile_definitions(hikari_composition PRIVATE HIKARI_UPDATE_TEST_FEED)
endif()
if(WIN32)
    # ShellExecuteExW and CommandLineToArgvW, and the installing side's
    # WinHTTP, random folder name and folder ACL.
    target_link_libraries(hikari_composition PRIVATE shell32 winhttp bcrypt crypt32 advapi32)
endif()
hikari_module(hikari_composition ALLOW hikari_ui hikari_ui_resources_1 hikari_uiplugin hikari_backends
    hikari_application hikari_core "Qt6::*" "hikari_ui*")

qt_add_executable(hikarisub main.cpp)
target_link_libraries(hikarisub PRIVATE hikari_composition hikari_translations) # O4: QM under :/i18n
hikari_module(hikarisub ALLOW hikari_composition hikari_translations "Qt6::*")
# DX1 (#367): --export-docs, with the commit the build was configured at.
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE HIKARI_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
# #391: in CI's AlmaLinux container git refuses the checkout (dubious
# ownership); the commit Actions checked out is GITHUB_SHA.
if(NOT HIKARI_COMMIT AND DEFINED ENV{GITHUB_SHA})
    set(HIKARI_COMMIT "$ENV{GITHUB_SHA}")
endif()
target_sources(hikari_composition PRIVATE docs_export.cpp include/hikari/app/docs_export.h)
set_source_files_properties(docs_export.cpp PROPERTIES COMPILE_DEFINITIONS "HIKARI_COMMIT=\"${HIKARI_COMMIT}\"")
# AN1 (#404): anonymous usage data (PostHog Cloud EU) and the crash record.
# Only release workflows pass the key; without one nothing is queued or sent.
set(HIKARI_POSTHOG_KEY "" CACHE STRING "PostHog project key for anonymous usage data (release builds only)")
target_sources(hikari_composition PRIVATE analytics.cpp analytics_key.cpp include/hikari/app/analytics.h
    crash_record.cpp include/hikari/app/crash_record.h
    crash_report.cpp include/hikari/app/crash_report.h # CR1 (#452): crash, freeze and issue reports
    crash_reports.cpp include/hikari/app/crash_reports.h
    hang_watchdog.cpp include/hikari/app/hang_watchdog.h)
set_source_files_properties(analytics_key.cpp PROPERTIES COMPILE_DEFINITIONS "HIKARI_POSTHOG_KEY=\"${HIKARI_POSTHOG_KEY}\"")
if(NOT WIN32)
    target_link_libraries(hikari_composition PRIVATE ${CMAKE_DL_LIBS}) # dladdr
endif()
# Y8: the font collector's window state.
target_sources(hikari_composition PRIVATE font_collector_controller.cpp include/hikari/app/font_collector_controller.h)
# O3: the legacy settings importer's generations and window state.
target_sources(hikari_composition PRIVATE settings_import_store.cpp include/hikari/app/settings_import_store.h
    settings_import_controller.cpp include/hikari/app/settings_import_controller.h)
# Y6: Font catalogs, the font picker's lists and the external fonts folder.
target_sources(hikari_composition PRIVATE font_catalogs_controller.cpp include/hikari/app/font_catalogs_controller.h)
# WS5: one instance per user session; later starts hand their files to it.
target_sources(hikari_composition PRIVATE single_instance.cpp include/hikari/app/single_instance.h
    forwarded_launches.cpp include/hikari/app/forwarded_launches.h)
# Y9: GRID_SUBS_FROM_MKV.
target_sources(hikari_composition PRIVATE matroska_controller.cpp include/hikari/app/matroska_controller.h)
# V6: timing and selecting Lines from the video, and the video following the active Line.
target_sources(hikari_composition PRIVATE application_video_timing.cpp)
# E5: translation mode controls (Translator mode, Moving tags, the Grid's
# Original text / Translation columns, the original hidden on video).
target_sources(hikari_composition PRIVATE application_translation.cpp)
# O5: the interface language (catalogs, live switching, aegisub.gettext's lookup).
target_sources(hikari_composition PRIVATE localisation.cpp include/hikari/app/localisation.h application_language.cpp)
# GR1 (#364): grammar check with harper-ls (the Line editor's marks and the panel's walk).
target_sources(hikari_composition PRIVATE application_grammar.cpp)
# AV1: the audio panel's output device menu and the unplugged-device notice.
target_sources(hikari_composition PRIVATE application_audio.cpp)
# W1: the optional DirectShow playback adapter (Windows), chosen in the settings.
target_sources(hikari_composition PRIVATE application_directshow.cpp)
if(WIN32)
    target_link_libraries(hikari_composition PUBLIC hikari_directshow)
    set_property(TARGET hikari_composition APPEND PROPERTY HIKARI_ALLOWED_LINKS hikari_directshow)
endif()
# Installers SPEC 1.4, 1.5 and 6.5: on Windows the program is HikariSub.exe,
# the name legacy's shortcut, App Paths key and file associations point at, a
# GUI program carrying legacy's icon resources with legacy's ids. The
# installer's payload is staged by the hikari_package_windows target.
if(WIN32)
    set(HIKARI_RC_VERSION "${PROJECT_VERSION}")
    if(HIKARI_VERSION_SUFFIX)
        string(APPEND HIKARI_RC_VERSION "-${HIKARI_VERSION_SUFFIX}")
    endif()
    configure_file(windows/hikarisub.rc.in "${CMAKE_CURRENT_BINARY_DIR}/hikarisub.rc" @ONLY)
    target_sources(hikarisub PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/hikarisub.rc")
    set_target_properties(hikarisub PROPERTIES OUTPUT_NAME HikariSub WIN32_EXECUTABLE TRUE)
    include("${PROJECT_SOURCE_DIR}/cmake/HikariPackageWindows.cmake")
endif()
