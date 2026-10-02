# The isolated FFMS2 media helper process (N1; ADR 0016).
add_executable(hikari_media_helper media_helper.cpp)
set_target_properties(hikari_media_helper PROPERTIES OUTPUT_NAME hikari-media-helper)
target_link_libraries(hikari_media_helper PRIVATE hikari_helper_endpoint Hikari::media_deps)
hikari_module(hikari_media_helper ALLOW hikari_helper_endpoint Hikari::media_deps)
