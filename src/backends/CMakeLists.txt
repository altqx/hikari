# Backend adapters implementing application ports (FFMS2/libass, audio, files).
add_library(hikari_backends STATIC module.cpp
    $<$<PLATFORM_ID:Linux>:posix_file_port.cpp> $<$<PLATFORM_ID:Linux>:posix_file_reader.cpp>
    $<$<PLATFORM_ID:Linux>:posix_platform_files.cpp>
    $<$<PLATFORM_ID:Windows>:win_file_port.cpp> $<$<PLATFORM_ID:Windows>:win_file_reader.cpp>
    $<$<PLATFORM_ID:Windows>:win_platform_files.cpp>)
target_include_directories(hikari_backends PUBLIC include)
target_link_libraries(hikari_backends PUBLIC hikari_application PRIVATE Hikari::media_deps)
hikari_module(hikari_backends ALLOW hikari_application hikari_core Hikari::media_deps)
