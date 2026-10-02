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
    include/hikari/backends/lua_script_host.h include/hikari/backends/qt_general_player.h
    include/hikari/backends/ffms_indexed_source.h
    include/hikari/backends/helper_host.h
    $<$<PLATFORM_ID:Linux>:posix_file_port.cpp> $<$<PLATFORM_ID:Linux>:posix_file_reader.cpp>
    $<$<PLATFORM_ID:Linux>:posix_platform_files.cpp>
    $<$<PLATFORM_ID:Windows>:win_file_port.cpp> $<$<PLATFORM_ID:Windows>:win_file_reader.cpp>
    $<$<PLATFORM_ID:Windows>:win_platform_files.cpp>)
target_include_directories(hikari_backends PUBLIC include)
set_target_properties(hikari_backends PROPERTIES AUTOMOC ON)
target_link_libraries(hikari_backends PUBLIC hikari_application hikari_helper_endpoint hikari_lua_protocol Qt6::Core
    Qt6::Multimedia
    PRIVATE Hikari::media_deps Hikari::portaudio)
hikari_module(hikari_backends ALLOW hikari_application hikari_core hikari_helper_endpoint hikari_lua_protocol
    Hikari::media_deps
    Hikari::portaudio "Qt6::*")
if(WIN32)
    target_link_libraries(hikari_backends PRIVATE dwrite) # FontService system faces
endif()
