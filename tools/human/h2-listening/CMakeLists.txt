# H2 listening driver (#125): the PortAudio editor output and Line audition
# under a person's control. A human tool, not a test: excluded from `all`;
# tools/human/h2-listening/h2-session.sh builds it on request
# (cmake --build <build dir> --target hikari_h2_audition).
add_executable(hikari_h2_audition EXCLUDE_FROM_ALL h2_audition.cpp)
target_link_libraries(hikari_h2_audition PRIVATE hikari_backends Qt6::Core)
target_compile_definitions(hikari_h2_audition PRIVATE HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>")
add_dependencies(hikari_h2_audition hikari_media_helper)
