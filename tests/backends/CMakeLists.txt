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
foreach(kind cfr vfr bframes longgop audio tracks unknown color601 color709 color709full)
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
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_EDGEBLUR_SCRIPT="${PROJECT_SOURCE_DIR}/Automation/automation/Autoload/macro-1-edgeblur.lua"
    HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
add_dependencies(hikari_backends_lua_helper_tests hikari_lua_helper hikari_automation_stage)

# N5: general playback through Qt Multimedia, identified by delivered frames,
# and chapters through the media helper.
hikari_add_qttest(hikari_backends_general_player_tests SOURCES general_player_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui Qt6::Multimedia)
target_compile_definitions(hikari_backends_general_player_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_general_player_tests hikari_media_helper hikari_media_fixtures)

# N4: font identity through the patched libass. Original CC0 fixtures are
# generated at build time; on Linux a private fontconfig configuration lists
# only them and the locked Qt install's OFL fonts, so selections are
# deterministic.
set(HIKARI_FONT_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/font-fixtures")
add_custom_command(OUTPUT "${HIKARI_FONT_FIXTURE_DIR}/base.ttf"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${HIKARI_FONT_FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/font-refresh-source"
    COMMAND hikari_font_fixture "${HIKARI_FONT_FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/font-refresh-source"
    DEPENDS hikari_font_fixture
    COMMENT "Generating font fixtures")
add_custom_target(hikari_font_fixtures ALL DEPENDS "${HIKARI_FONT_FIXTURE_DIR}/base.ttf")
set(HIKARI_QT_EXAMPLE_FONTS "${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2")
cmake_path(NORMAL_PATH HIKARI_QT_EXAMPLE_FONTS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/font-fixtures.conf"
"<?xml version=\"1.0\"?>
<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">
<fontconfig>
  <dir>${HIKARI_FONT_FIXTURE_DIR}</dir>
  <dir>${HIKARI_QT_EXAMPLE_FONTS}/quick/advancedtext/fonts</dir>
  <dir>${HIKARI_QT_EXAMPLE_FONTS}/quickcontrols/wearable/WearableStyle/fonts</dir>
  <dir>${CMAKE_CURRENT_BINARY_DIR}/font-refresh</dir>
  <cachedir>${CMAKE_CURRENT_BINARY_DIR}/font-fixtures-cache</cachedir>
</fontconfig>
")
hikari_add_gtest(hikari_backends_font_service_tests SOURCES font_service_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends
    PROPERTIES ENVIRONMENT "FONTCONFIG_FILE=${CMAKE_CURRENT_BINARY_DIR}/font-fixtures.conf")
target_compile_definitions(hikari_backends_font_service_tests PRIVATE
    HIKARI_FONT_FIXTURES="${HIKARI_FONT_FIXTURE_DIR}"
    HIKARI_FONTCONFIG_FILE="${CMAKE_CURRENT_BINARY_DIR}/font-fixtures.conf"
    HIKARI_QT_FONTS="${HIKARI_QT_EXAMPLE_FONTS}")
add_dependencies(hikari_backends_font_service_tests hikari_font_fixtures)

# I6 (M50-helper): the media helper's package manifest — its static vcpkg
# closure with reviewed licences and copyright files, and OS-only dynamic
# dependencies. Written into provision-evidence, which CI keeps.
add_test(NAME media-helper-manifest COMMAND "${CMAKE_COMMAND}"
    "-DHELPER=$<TARGET_FILE:hikari_media_helper>" -DNAME=hikari-media-helper -DROOTS=hikari-ffms2
    "-DSTATUS=${VCPKG_INSTALLED_DIR}/vcpkg/status" "-DINSTALLED=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
    "-DTRIPLET=${VCPKG_TARGET_TRIPLET}" "-DLICENCES=${PROJECT_SOURCE_DIR}/cmake/locks/media-helper-licences.json"
    "-DOUT=${CMAKE_BINARY_DIR}/provision-evidence/media-helper-manifest.json"
    -P "${PROJECT_SOURCE_DIR}/cmake/manifest/HelperManifest.cmake")
set_tests_properties(media-helper-manifest PROPERTIES LABELS packaging TIMEOUT 120)
# Control: a closure package without a reviewed licence fails the manifest.
file(READ "${PROJECT_SOURCE_DIR}/cmake/locks/media-helper-licences.json" _hikari_licences)
string(JSON _hikari_licences REMOVE "${_hikari_licences}" packages zlib)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/licences-without-zlib.json" "${_hikari_licences}")
add_test(NAME media-helper-manifest-control COMMAND "${CMAKE_COMMAND}"
    "-DHELPER=$<TARGET_FILE:hikari_media_helper>" -DNAME=hikari-media-helper -DROOTS=hikari-ffms2
    "-DSTATUS=${VCPKG_INSTALLED_DIR}/vcpkg/status" "-DINSTALLED=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
    "-DTRIPLET=${VCPKG_TARGET_TRIPLET}" "-DLICENCES=${CMAKE_CURRENT_BINARY_DIR}/licences-without-zlib.json"
    "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/manifest-control.json"
    -P "${PROJECT_SOURCE_DIR}/cmake/manifest/HelperManifest.cmake")
set_tests_properties(media-helper-manifest-control PROPERTIES LABELS packaging TIMEOUT 120 WILL_FAIL TRUE)

# I3: Line audition through the editor output (simulated device everywhere,
# PortAudio where a device exists).
hikari_add_gtest(hikari_backends_line_audition_tests SOURCES line_audition_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends PROPERTIES RUN_SERIAL TRUE)
target_compile_definitions(hikari_backends_line_audition_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_line_audition_tests hikari_media_helper hikari_media_fixtures)

# I4: general <-> indexed handoff (real Qt player and media helper).
hikari_add_qttest(hikari_backends_transport_tests SOURCES transport_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui Qt6::Multimedia)
target_compile_definitions(hikari_backends_transport_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_transport_tests hikari_media_helper hikari_media_fixtures)

# I5: whole-document collection, reimport, refresh, concurrency and the
# multilingual corpus. font-refresh starts empty; the refresh test copies a
# font into it and removes it again.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/font-refresh")
hikari_add_gtest(hikari_backends_font_collection_tests SOURCES font_collection_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends
    PROPERTIES ENVIRONMENT "FONTCONFIG_FILE=${CMAKE_CURRENT_BINARY_DIR}/font-fixtures.conf" RUN_SERIAL TRUE)
target_compile_definitions(hikari_backends_font_collection_tests PRIVATE
    HIKARI_FONT_FIXTURES="${HIKARI_FONT_FIXTURE_DIR}"
    HIKARI_FONTCONFIG_FILE="${CMAKE_CURRENT_BINARY_DIR}/font-fixtures.conf"
    HIKARI_FONT_REFRESH_SOURCE="${CMAKE_CURRENT_BINARY_DIR}/font-refresh-source"
    HIKARI_FONT_REFRESH_DIR="${CMAKE_CURRENT_BINARY_DIR}/font-refresh")
add_dependencies(hikari_backends_font_collection_tests hikari_font_fixtures)

# L1: the automation manager and macro identity.
hikari_add_gtest(hikari_backends_automation_manager_tests SOURCES automation_manager_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_automation_manager_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include"
    HIKARI_EDGEBLUR_SCRIPT="${PROJECT_SOURCE_DIR}/Automation/automation/Autoload/macro-1-edgeblur.lua")
add_dependencies(hikari_backends_automation_manager_tests hikari_lua_helper)

# I1: the video session over the real media helper and libass.
hikari_add_gtest(hikari_backends_video_session_tests SOURCES video_session_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui LABELS backends)
target_compile_definitions(hikari_backends_video_session_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_video_session_tests hikari_media_helper hikari_media_fixtures)
