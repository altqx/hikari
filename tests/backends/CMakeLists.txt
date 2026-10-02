if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    hikari_add_gtest(hikari_backends_posix_file_tests SOURCES posix_file_port_tests.cpp
        LIBRARIES hikari_backends LABELS backends)
endif()
if(WIN32)
    hikari_add_gtest(hikari_backends_win_file_tests SOURCES win_file_port_tests.cpp
        LIBRARIES hikari_backends LABELS backends)
    target_compile_definitions(hikari_backends_win_file_tests PRIVATE
        HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")
endif()
hikari_add_gtest(hikari_backends_open_save_tests SOURCES open_save_reopen_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
target_compile_definitions(hikari_backends_open_save_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")

hikari_add_gtest(hikari_backends_libass_tests SOURCES libass_renderer_tests.cpp
    LIBRARIES hikari_backends hikari_image_testing Qt6::Gui LABELS backends)
target_compile_definitions(hikari_backends_libass_tests PRIVATE
    HIKARI_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/tests/references"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/libass")

add_executable(hikari_test_helper test_helper.cpp)
target_link_libraries(hikari_test_helper PRIVATE hikari_helper_endpoint)
hikari_add_gtest(hikari_backends_helper_tests SOURCES helper_host_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_helper_tests PRIVATE HIKARI_TEST_HELPER="$<TARGET_FILE:hikari_test_helper>")
add_dependencies(hikari_backends_helper_tests hikari_test_helper)

# N1: fixtures are generated at build time, so they exist before any test
# runs (discovered GoogleTest cases did not honour a CTest setup fixture).
set(HIKARI_MEDIA_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/media-fixtures")
set(_hikari_media_fixtures "")
foreach(kind cfr vfr bframes longgop audio tracks unknown)
    set(out "${HIKARI_MEDIA_FIXTURE_DIR}/${kind}.mkv")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HIKARI_MEDIA_FIXTURE_DIR}"
        COMMAND hikari_media_fixture "${out}" ${kind}
        DEPENDS hikari_media_fixture
        COMMENT "Generating media fixture ${kind}")
    list(APPEND _hikari_media_fixtures "${out}")
endforeach()
add_custom_target(hikari_media_fixtures ALL DEPENDS ${_hikari_media_fixtures})
hikari_add_gtest(hikari_backends_indexed_source_tests SOURCES indexed_source_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_indexed_source_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_indexed_source_tests hikari_media_helper hikari_media_fixtures)

# N6: the real-time engine everywhere; the PortAudio stream wherever an output
# device exists (CI Linux provides a null ALSA PCM, HIKARI_TEST_AUDIO_DEVICE).
hikari_add_gtest(hikari_backends_audio_engine_tests SOURCES audio_engine_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
hikari_add_gtest(hikari_backends_portaudio_tests SOURCES portaudio_output_tests.cpp
    LIBRARIES hikari_backends LABELS backends PROPERTIES RUN_SERIAL TRUE)

# N8: the isolated Lua helper with the unchanged bundled edgeblur script and
# authored fixtures.
hikari_add_gtest(hikari_backends_lua_helper_tests SOURCES lua_helper_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_lua_helper_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include"
    HIKARI_EDGEBLUR_SCRIPT="${PROJECT_SOURCE_DIR}/Automation/automation/Autoload/macro-1-edgeblur.lua")
add_dependencies(hikari_backends_lua_helper_tests hikari_lua_helper)

# N5: general playback through Qt Multimedia, identified by delivered frames,
# and chapters through the media helper.
hikari_add_qttest(hikari_backends_general_player_tests SOURCES general_player_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui Qt6::Multimedia)
target_compile_definitions(hikari_backends_general_player_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_general_player_tests hikari_media_helper hikari_media_fixtures)
