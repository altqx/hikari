add_library(hikari_helper_endpoint STATIC helper_protocol.cpp helper_endpoint.cpp)
target_include_directories(hikari_helper_endpoint PUBLIC include)
target_compile_features(hikari_helper_endpoint PUBLIC cxx_std_23)
hikari_module(hikari_helper_endpoint ALLOW)

add_library(hikari_lua_protocol STATIC lua_protocol.cpp)
target_link_libraries(hikari_lua_protocol PUBLIC hikari_helper_endpoint hikari_application)
hikari_module(hikari_lua_protocol ALLOW hikari_helper_endpoint hikari_application hikari_core)

add_library(hikari_plugin_protocol STATIC plugin_protocol.cpp include/hikari/backends/plugin_protocol.h)
target_link_libraries(hikari_plugin_protocol PUBLIC hikari_helper_endpoint)
hikari_module(hikari_plugin_protocol ALLOW hikari_helper_endpoint)

add_library(hikari_backends STATIC module.cpp libass_renderer.cpp overlay_blend.cpp
    include/hikari/backends/overlay_blend.h helper_host.cpp ffms_indexed_source.cpp
    audio_ring.cpp audio_output_engine.cpp portaudio_output.cpp lua_script_host.cpp qt_general_player.cpp
    video_frame_bgra.cpp include/hikari/backends/video_frame_bgra.h
    libass_font_service.cpp line_audition.cpp include/hikari/backends/line_audition.h
    transport_coordinator.cpp include/hikari/backends/transport_coordinator.h
    automation_manager.cpp include/hikari/backends/automation_manager.h karaoke_preview_runner.cpp include/hikari/backends/karaoke_preview_runner.h process_metrics.cpp
    legacy_text_file.cpp include/hikari/backends/legacy_text_file.h
    include/hikari/backends/lua_script_host.h include/hikari/backends/qt_general_player.h
    include/hikari/backends/ffms_indexed_source.h
    ffms_matroska.cpp include/hikari/backends/ffms_matroska.h
    include/hikari/backends/helper_host.h
    legacy_spelling.cpp include/hikari/backends/legacy_spelling.h
    mecab_readings.cpp include/hikari/backends/mecab_readings.h
    harper_client.cpp include/hikari/backends/harper_client.h
    plugin_process.cpp include/hikari/backends/plugin_process.h
    lua_node_helper.cpp include/hikari/backends/lua_node_helper.h
    $<$<PLATFORM_ID:Linux>:posix_file_port.cpp> $<$<PLATFORM_ID:Linux>:posix_file_reader.cpp>
    $<$<PLATFORM_ID:Linux>:posix_platform_files.cpp>
    $<$<PLATFORM_ID:Windows>:win_file_port.cpp> $<$<PLATFORM_ID:Windows>:win_file_reader.cpp>
    $<$<PLATFORM_ID:Windows>:win_platform_files.cpp>)
target_include_directories(hikari_backends PUBLIC include)
set_target_properties(hikari_backends PROPERTIES AUTOMOC ON)
target_sources(hikari_backends PRIVATE audio_box_player.cpp include/hikari/backends/audio_box_player.h
    simulated_output.cpp include/hikari/backends/simulated_output.h)
target_sources(hikari_backends PRIVATE qt_audio_devices.cpp include/hikari/backends/qt_audio_devices.h)
target_link_libraries(hikari_backends PUBLIC hikari_application hikari_helper_endpoint hikari_lua_protocol
    hikari_plugin_protocol hikari_sandbox Qt6::Core
    Qt6::Multimedia
    PRIVATE Hikari::media_deps Hikari::portaudio Hikari::soundtouch Hikari::uchardet Hikari::hunspell Hikari::mecab Boost::locale ICU::i18n ICU::uc ICU::data)
# ICU and Boost.Locale link statically: no dllimport, no Boost auto-linking.
target_compile_definitions(hikari_backends PRIVATE BOOST_ALL_NO_LIB U_STATIC_IMPLEMENTATION)
hikari_module(hikari_backends ALLOW hikari_application hikari_core hikari_helper_endpoint hikari_lua_protocol
    hikari_plugin_protocol hikari_sandbox
    Hikari::media_deps
    Hikari::portaudio Hikari::soundtouch Hikari::uchardet Hikari::hunspell Hikari::mecab "Qt6::*")
if(WIN32)
    target_link_libraries(hikari_backends PRIVATE dwrite)
endif()
target_sources(hikari_backends PRIVATE show_in_folder.cpp include/hikari/backends/show_in_folder.h)
target_sources(hikari_backends PRIVATE desktop_handoff.cpp include/hikari/backends/desktop_handoff.h)
target_link_libraries(hikari_backends PRIVATE Qt6::Gui)
if(WIN32)
    target_link_libraries(hikari_backends PRIVATE shell32 ole32 gdi32)
else()
    target_link_libraries(hikari_backends PRIVATE Qt6::DBus)
endif()
target_sources(hikari_backends PRIVATE subtitle_renderer_selection.cpp
    include/hikari/backends/subtitle_renderer_selection.h)
if(WIN32)
    target_sources(hikari_backends PRIVATE csri_renderer.cpp csri_loader_win.cpp
        include/hikari/backends/csri_renderer.h)
endif()
target_sources(hikari_backends PRIVATE font_collector_output.cpp include/hikari/backends/font_collector_output.h)
if(WIN32)
    add_subdirectory(directshow)
endif()

if(WIN32)
    add_library(hikari_sandbox STATIC sandbox_windows.cpp include/hikari/backends/sandbox.h)
    target_link_libraries(hikari_sandbox PRIVATE userenv advapi32)
else()
    add_library(hikari_sandbox STATIC sandbox_linux.cpp include/hikari/backends/sandbox.h)
endif()
target_include_directories(hikari_sandbox PUBLIC include)
target_link_libraries(hikari_sandbox PUBLIC Qt6::Core)
hikari_module(hikari_sandbox ALLOW "Qt6::*")

add_library(hikari_ai STATIC ai_config_store.cpp include/hikari/backends/ai_config_store.h ai_requests.cpp
    include/hikari/backends/ai_requests.h ai_signin.cpp include/hikari/backends/ai_signin.h ai_keychain.cpp
    include/hikari/backends/ai_keychain.h ai_sidecar_host.cpp include/hikari/backends/ai_sidecar_host.h ai_channel.cpp
    include/hikari/backends/ai_channel.h ai_sidecar_updater.cpp include/hikari/backends/ai_sidecar_updater.h
    ai_http_relay.cpp include/hikari/backends/ai_http_relay.h ai_agent.cpp include/hikari/backends/ai_agent.h ai_agent_broker.cpp
    include/hikari/backends/ai_agent_broker.h ai_agent_cli.cpp include/hikari/backends/ai_agent_cli.h
    ai_mcp_server.cpp include/hikari/backends/ai_mcp_server.h ai_mcp_bridge.cpp include/hikari/backends/ai_mcp_bridge.h
    ai_inline_run.cpp include/hikari/backends/ai_inline_run.h)
find_package(ZLIB REQUIRED)
target_include_directories(hikari_ai PUBLIC include)
target_link_libraries(hikari_ai PUBLIC hikari_application hikari_api hikari_sandbox Qt6::Core Qt6::Network
    PRIVATE Qt6Keychain::Qt6Keychain ZLIB::ZLIB)
# QZipReader is private Qt API.
find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS CorePrivate)
target_link_libraries(hikari_ai PRIVATE Qt6::CorePrivate)
if(NOT WIN32)
    target_link_libraries(hikari_ai PRIVATE Qt6::DBus)
endif()
hikari_module(hikari_ai ALLOW hikari_application hikari_api hikari_core hikari_sandbox "Qt6::*" ZLIB::ZLIB)
