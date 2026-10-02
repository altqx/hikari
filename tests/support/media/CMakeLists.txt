# Test-only synthetic media fixtures (N1/N2): frames carry their index as a
# barcode, audio carries its sample index. Never linked into shipping targets.
add_executable(hikari_media_fixture media_fixture.cpp)
target_link_libraries(hikari_media_fixture PRIVATE Hikari::media_deps)
