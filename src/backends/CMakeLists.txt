# The helper side of helper processes (framing and endpoint): plain C++, linked
# into helper executables and, for the framing, into the host adapters.
add_library(hikari_helper_endpoint STATIC helper_protocol.cpp helper_endpoint.cpp)
target_include_directories(hikari_helper_endpoint PUBLIC include)
target_compile_features(hikari_helper_endpoint PUBLIC cxx_std_23)
hikari_module(hikari_helper_endpoint ALLOW)

# Backend adapters implementing application ports (FFMS2/libass, audio, files,
# helper processes).
add_library(hikari_backends STATIC module.cpp libass_renderer.cpp helper_host.cpp
    include/hikari/backends/helper_host.h
    $<$<PLATFORM_ID:Linux>:posix_file_port.cpp> $<$<PLATFORM_ID:Linux>:posix_file_reader.cpp>
    $<$<PLATFORM_ID:Linux>:posix_platform_files.cpp>
    $<$<PLATFORM_ID:Windows>:win_file_port.cpp> $<$<PLATFORM_ID:Windows>:win_file_reader.cpp>
    $<$<PLATFORM_ID:Windows>:win_platform_files.cpp>)
target_include_directories(hikari_backends PUBLIC include)
set_target_properties(hikari_backends PROPERTIES AUTOMOC ON)
target_link_libraries(hikari_backends PUBLIC hikari_application hikari_helper_endpoint Qt6::Core
    PRIVATE Hikari::media_deps)
hikari_module(hikari_backends ALLOW hikari_application hikari_core hikari_helper_endpoint Hikari::media_deps "Qt6::*")
