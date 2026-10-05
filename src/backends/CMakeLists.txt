# The helper side of helper processes (framing and endpoint): plain C++, linked
# into helper executables and, for the framing, into the host adapters.
add_library(hikari_helper_endpoint STATIC helper_protocol.cpp helper_endpoint.cpp)
target_include_directories(hikari_helper_endpoint PUBLIC include)
target_compile_features(hikari_helper_endpoint PUBLIC cxx_std_23)
hikari_module(hikari_helper_endpoint ALLOW)

# The Lua helper protocol: plain C++, shared by the helper and its host adapter.
add_library(hikari_lua_protocol STATIC lua_protocol.cpp)
target_link_libraries(hikari_lua_protocol PUBLIC hikari_helper_endpoint hikari_application)
hikari_module(hikari_lua_protocol ALLOW hikari_helper_endpoint hikari_application hikari_core)

# Backend adapters implementing application ports (FFMS2/libass, audio, files,
# helper processes).
add_library(hikari_backends STATIC module.cpp libass_renderer.cpp helper_host.cpp ffms_indexed_source.cpp
    audio_ring.cpp audio_output_engine.cpp portaudio_output.cpp lua_script_host.cpp qt_general_player.cpp
    libass_font_service.cpp line_audition.cpp include/hikari/backends/line_audition.h
    transport_coordinator.cpp include/hikari/backends/transport_coordinator.h
    automation_manager.cpp include/hikari/backends/automation_manager.h process_metrics.cpp
    legacy_text_file.cpp include/hikari/backends/legacy_text_file.h
    include/hikari/backends/lua_script_host.h include/hikari/backends/qt_general_player.h
    include/hikari/backends/ffms_indexed_source.h
    ffms_matroska.cpp include/hikari/backends/ffms_matroska.h
    include/hikari/backends/helper_host.h
    legacy_spelling.cpp include/hikari/backends/legacy_spelling.h
    $<$<PLATFORM_ID:Linux>:posix_file_port.cpp> $<$<PLATFORM_ID:Linux>:posix_file_reader.cpp>
    $<$<PLATFORM_ID:Linux>:posix_platform_files.cpp>
    $<$<PLATFORM_ID:Windows>:win_file_port.cpp> $<$<PLATFORM_ID:Windows>:win_file_reader.cpp>
    $<$<PLATFORM_ID:Windows>:win_platform_files.cpp>)
target_include_directories(hikari_backends PUBLIC include)
set_target_properties(hikari_backends PROPERTIES AUTOMOC ON)
# A4: the audio box's player through the editor output, and an output without a device.
target_sources(hikari_backends PRIVATE audio_box_player.cpp include/hikari/backends/audio_box_player.h
    simulated_output.cpp include/hikari/backends/simulated_output.h)
target_link_libraries(hikari_backends PUBLIC hikari_application hikari_helper_endpoint hikari_lua_protocol Qt6::Core
    Qt6::Multimedia
    PRIVATE Hikari::media_deps Hikari::portaudio Hikari::uchardet Hikari::hunspell Boost::locale ICU::i18n ICU::uc ICU::data)
# F3 / R2-hunspell: ICU and Boost.Locale link statically (no dllimport, no
# Boost auto-linking).
target_compile_definitions(hikari_backends PRIVATE BOOST_ALL_NO_LIB U_STATIC_IMPLEMENTATION)
hikari_module(hikari_backends ALLOW hikari_application hikari_core hikari_helper_endpoint hikari_lua_protocol
    Hikari::media_deps
    Hikari::portaudio Hikari::uchardet Hikari::hunspell "Qt6::*")
if(WIN32)
    target_link_libraries(hikari_backends PRIVATE dwrite) # FontService system faces
endif()
# P9: legacy SelectInFolder (Explorer's selection on Windows, the desktop
# file manager over D-Bus with a folder fallback on Linux).
target_sources(hikari_backends PRIVATE show_in_folder.cpp include/hikari/backends/show_in_folder.h)
target_link_libraries(hikari_backends PRIVATE Qt6::Gui)
if(WIN32)
    target_link_libraries(hikari_backends PRIVATE shell32 ole32)
else()
    target_link_libraries(hikari_backends PRIVATE Qt6::DBus)
endif()
# Y8: the font collector's folder and zip output.
target_sources(hikari_backends PRIVATE font_collector_output.cpp include/hikari/backends/font_collector_output.h)
