# Test-only synthetic media fixtures (N1/N2): frames carry their index as a
# barcode, audio carries its sample index. Never linked into shipping targets.
add_executable(hikari_media_fixture media_fixture.cpp)
target_link_libraries(hikari_media_fixture PRIVATE Hikari::media_deps)
# Y9: Matroska files with subtitle tracks and attachments, written byte by
# byte (mkv_fixture.h describes their content; the tests include it).
add_executable(hikari_mkv_fixture mkv_fixture.cpp)
target_compile_features(hikari_mkv_fixture PRIVATE cxx_std_23)
