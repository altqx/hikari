if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    hikari_add_gtest(hikari_backends_posix_file_tests SOURCES posix_file_port_tests.cpp
        LIBRARIES hikari_backends LABELS backends)
endif()
if(WIN32)
    hikari_add_gtest(hikari_backends_win_file_tests SOURCES win_file_port_tests.cpp
        LIBRARIES hikari_backends LABELS backends)
    target_compile_definitions(hikari_backends_win_file_tests PRIVATE
        HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/fixtures/approved-departures")
endif()
hikari_add_gtest(hikari_backends_legacy_text_tests SOURCES legacy_text_file_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
hikari_add_gtest(hikari_backends_open_save_tests SOURCES open_save_reopen_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
target_compile_definitions(hikari_backends_open_save_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/fixtures/approved-departures")

hikari_add_gtest(hikari_backends_overlay_blend_tests SOURCES overlay_blend_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
hikari_add_gtest(hikari_backends_libass_tests SOURCES libass_renderer_tests.cpp
    LIBRARIES hikari_backends hikari_image_testing Hikari::libass Qt6::Gui LABELS backends)
target_compile_definitions(hikari_backends_libass_tests PRIVATE
    HIKARI_REFERENCE_DIR="${PROJECT_SOURCE_DIR}/tests/references"
    HIKARI_ATTACHMENT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/attachments"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_LIBASS_FORK_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/libass-fork"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/libass")
hikari_add_gtest(hikari_backends_frame_tools_tests SOURCES frame_tools_tests.cpp
    "${PROJECT_SOURCE_DIR}/tests/api/script_client.cpp"
    LIBRARIES hikari_api hikari_backends hikari_regex Qt6::Gui LABELS backends api)
target_include_directories(hikari_backends_frame_tools_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/api")
target_compile_definitions(hikari_backends_frame_tools_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")
hikari_add_gtest(hikari_backends_visual_geometry_tests SOURCES visual_geometry_tests.cpp
    LIBRARIES hikari_backends hikari_application Qt6::Gui LABELS backends)
target_compile_definitions(hikari_backends_visual_geometry_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/visual-t3")

hikari_add_gtest(hikari_backends_visual_perspective_render_tests SOURCES visual_perspective_render_tests.cpp
    LIBRARIES hikari_backends hikari_application Qt6::Gui LABELS backends)
target_compile_definitions(hikari_backends_visual_perspective_render_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")

add_executable(hikari_test_helper test_helper.cpp)
target_link_libraries(hikari_test_helper PRIVATE hikari_helper_endpoint)
hikari_add_gtest(hikari_backends_helper_tests SOURCES helper_host_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_helper_tests PRIVATE HIKARI_TEST_HELPER="$<TARGET_FILE:hikari_test_helper>")
add_dependencies(hikari_backends_helper_tests hikari_test_helper)
hikari_add_gtest(hikari_backends_plugin_process_tests SOURCES plugin_process_tests.cpp
    LIBRARIES hikari_backends Qt6::Core Qt6::Network Qt6::Test LABELS backends plugins)
target_compile_definitions(hikari_backends_plugin_process_tests PRIVATE
    HIKARI_PLUGIN_HELPER="$<TARGET_FILE:hikari_plugin_helper>" HIKARI_SANDBOX_PROBE="$<TARGET_FILE:hikari_sandbox_probe>")
add_dependencies(hikari_backends_plugin_process_tests hikari_plugin_helper hikari_sandbox_probe)

# Generated at build time: discovered GoogleTest cases ignore a CTest setup fixture.
set(HIKARI_MEDIA_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/media-fixtures")
set(_hikari_media_fixtures "")
foreach(kind cfr vfr bframes longgop long longaudio audio tracks unknown color601 color709 color709full colorhd colorhdr audiodelay audioonly sar mkvextract yuv10 keypoint fade)
    set(out "${HIKARI_MEDIA_FIXTURE_DIR}/${kind}.mkv")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HIKARI_MEDIA_FIXTURE_DIR}"
        COMMAND hikari_media_fixture "${out}" ${kind}
        DEPENDS hikari_media_fixture
        COMMENT "Generating media fixture ${kind}")
    list(APPEND _hikari_media_fixtures "${out}")
endforeach()
foreach(kind subs onesub nosubs scanline scale100us scale10ms)
    set(out "${HIKARI_MEDIA_FIXTURE_DIR}/mkv-${kind}.mkv")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HIKARI_MEDIA_FIXTURE_DIR}"
        COMMAND hikari_mkv_fixture "${out}" ${kind}
        DEPENDS hikari_mkv_fixture
        COMMENT "Generating Matroska fixture ${kind}")
    list(APPEND _hikari_media_fixtures "${out}")
endforeach()
foreach(spec "subs-mp4|mp4" "subs-mov|mov" "subs-ts|ts")
    string(REPLACE "|" ";" spec "${spec}")
    list(GET spec 0 kind)
    list(GET spec 1 extension)
    set(out "${HIKARI_MEDIA_FIXTURE_DIR}/${kind}.${extension}")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HIKARI_MEDIA_FIXTURE_DIR}"
        COMMAND hikari_media_fixture "${out}" ${kind}
        DEPENDS hikari_media_fixture
        COMMENT "Generating media fixture ${kind}")
    list(APPEND _hikari_media_fixtures "${out}")
endforeach()
# Formats stock DirectShow plays (AVI splitter, WM ASF reader); MPEG-1 adds a filter with property pages.
foreach(spec "dshow-cfr|avi" "dshow-vfr|wmv" "dshow-mpg|mpg")
    string(REPLACE "|" ";" spec "${spec}")
    list(GET spec 0 kind)
    list(GET spec 1 extension)
    set(out "${HIKARI_MEDIA_FIXTURE_DIR}/${kind}.${extension}")
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
    HIKARI_FAILING_MEDIA_HELPER="$<TARGET_FILE:hikari_failing_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}"
    HIKARI_WEBM_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/webm")
add_executable(hikari_failing_media_helper failing_media_helper.cpp)
target_link_libraries(hikari_failing_media_helper PRIVATE hikari_helper_endpoint)
add_dependencies(hikari_backends_indexed_source_tests hikari_media_helper hikari_failing_media_helper
    hikari_media_fixtures)
target_include_directories(hikari_backends_indexed_source_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")

# CI Linux has only a null ALSA PCM (HIKARI_TEST_AUDIO_DEVICE).
hikari_add_gtest(hikari_backends_audio_engine_tests SOURCES audio_engine_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
hikari_add_gtest(hikari_backends_portaudio_tests SOURCES portaudio_output_tests.cpp
    LIBRARIES hikari_backends hikari_regex LABELS backends PROPERTIES RUN_SERIAL TRUE)
hikari_add_gtest(hikari_backends_audio_box_player_tests SOURCES audio_box_player_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends PROPERTIES RUN_SERIAL TRUE)

hikari_add_gtest(hikari_backends_lua_helper_tests SOURCES lua_helper_tests.cpp
    LIBRARIES hikari_backends Qt6::Core Qt6::Network LABELS backends)
target_compile_definitions(hikari_backends_lua_helper_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/lua-fixtures"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_EDGEBLUR_SCRIPT="${PROJECT_SOURCE_DIR}/Automation/automation/Autoload/macro-1-edgeblur.lua"
    HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    HIKARI_CAPTURE_PROBE="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/automation/capture-probe.lua")
add_dependencies(hikari_backends_lua_helper_tests hikari_lua_helper hikari_automation_stage)

add_executable(hikari_fake_lua_node_helper fake_lua_node_helper.cpp)
target_link_libraries(hikari_fake_lua_node_helper PRIVATE hikari_lua_protocol)
hikari_add_gtest(hikari_backends_lua_node_helper_tests SOURCES lua_node_helper_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_lua_node_helper_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_FAKE_LUA_NODE_HELPER="$<TARGET_FILE:hikari_fake_lua_node_helper>"
    HIKARI_KARAOKE_PRESET_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/karaoke-presets")
add_dependencies(hikari_backends_lua_node_helper_tests hikari_lua_helper hikari_fake_lua_node_helper)

hikari_add_gtest(hikari_backends_karaoke_templater_tests SOURCES karaoke_templater_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_karaoke_templater_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_KARAOKE_TEMPLATER_ORACLE="${CMAKE_CURRENT_SOURCE_DIR}/karaoke-corpus/kara-templater-hikari.lua"
    HIKARI_KARAOKE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/karaoke-corpus")
target_include_directories(hikari_backends_karaoke_templater_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
add_dependencies(hikari_backends_karaoke_templater_tests hikari_lua_helper hikari_automation_stage)
hikari_add_gtest(hikari_backends_bundled_macros_tests SOURCES bundled_macros_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_bundled_macros_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_AUTOLOAD_DIR="${HIKARI_AUTOMATION_STAGE}/Autoload"
    HIKARI_MACRO_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/macro-corpus")
add_dependencies(hikari_backends_bundled_macros_tests hikari_lua_helper hikari_automation_stage)
hikari_add_gtest(hikari_backends_karaoke_preview_tests SOURCES karaoke_preview_runner_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_karaoke_preview_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_KARAOKE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/karaoke-corpus")
target_include_directories(hikari_backends_karaoke_preview_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
add_dependencies(hikari_backends_karaoke_preview_tests hikari_lua_helper hikari_automation_stage)

hikari_add_gtest(hikari_backends_karaoke_presets_tests SOURCES karaoke_presets_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_karaoke_presets_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_KARAOKE_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/karaoke-corpus"
    HIKARI_KARAOKE_PRESETS="${PROJECT_SOURCE_DIR}/src/application/karaoke-presets"
    HIKARI_KARAOKE_PRESET_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/karaoke-presets")
target_include_directories(hikari_backends_karaoke_presets_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
add_dependencies(hikari_backends_karaoke_presets_tests hikari_lua_helper hikari_automation_stage)

hikari_add_gtest(hikari_backends_motion_parity_tests SOURCES motion_parity_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends PROPERTIES TIMEOUT 300)
target_compile_definitions(hikari_backends_motion_parity_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_AMO_SCRIPT="${HIKARI_AUTOMATION_STAGE}/Autoload/a-mo.Aegisub-Motion.moon"
    HIKARI_AUTOMATION_STAGE_DIR="${HIKARI_AUTOMATION_STAGE}"
    HIKARI_MOTION_CORPUS="${PROJECT_SOURCE_DIR}/tests/fixtures/motion-data")
target_include_directories(hikari_backends_motion_parity_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
add_dependencies(hikari_backends_motion_parity_tests hikari_lua_helper hikari_automation_stage)

set(_hikari_thirdparty "${PROJECT_SOURCE_DIR}/tests/fixtures/automation-thirdparty")
set(_hikari_thirdparty_stage "${CMAKE_CURRENT_BINARY_DIR}/automation-thirdparty/stage")
add_test(NAME automation-thirdparty-stage COMMAND "${CMAKE_COMMAND}"
    -D "MANIFEST=${_hikari_thirdparty}/manifest.json"
    -D "FETCH_DIR=${CMAKE_CURRENT_BINARY_DIR}/automation-thirdparty/fetched"
    -D "OUT=${_hikari_thirdparty_stage}"
    -D "STAGE_INCLUDE=${HIKARI_AUTOMATION_STAGE}/Include"
    -P "${_hikari_thirdparty}/stage.cmake")
set_tests_properties(automation-thirdparty-stage PROPERTIES FIXTURES_SETUP automation_thirdparty
    LABELS backends TIMEOUT 300)
hikari_add_gtest(hikari_backends_automation_thirdparty_tests SOURCES automation_thirdparty_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends
    PROPERTIES FIXTURES_REQUIRED automation_thirdparty TIMEOUT 600)
target_compile_definitions(hikari_backends_automation_thirdparty_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_THIRDPARTY_DIR="${_hikari_thirdparty}"
    HIKARI_THIRDPARTY_STAGE="${_hikari_thirdparty_stage}"
    HIKARI_CMAKE_COMMAND="${CMAKE_COMMAND}"
    HIKARI_THIRDPARTY_FETCH="${CMAKE_CURRENT_BINARY_DIR}/automation-thirdparty/fetched"
    HIKARI_AUTOMATION_INCLUDE="${HIKARI_AUTOMATION_STAGE}/Include"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    HIKARI_CAPTURE_PROBE="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/automation/capture-probe.lua")
target_include_directories(hikari_backends_automation_thirdparty_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
add_dependencies(hikari_backends_automation_thirdparty_tests hikari_lua_helper hikari_automation_stage)

hikari_add_qttest(hikari_backends_general_player_tests SOURCES general_player_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui Qt6::Multimedia)
target_compile_definitions(hikari_backends_general_player_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}"
    HIKARI_WEBM_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/webm")
add_dependencies(hikari_backends_general_player_tests hikari_media_helper hikari_media_fixtures)

hikari_add_qttest(hikari_backends_video_frame_bgra_tests SOURCES video_frame_bgra_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui Qt6::Multimedia)

# A private fontconfig file lists only these fixtures and Qt's example fonts, so matches are deterministic.
set(HIKARI_FONT_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/font-fixtures")
add_custom_command(OUTPUT "${HIKARI_FONT_FIXTURE_DIR}/base.ttf"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${HIKARI_FONT_FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/font-refresh-source"
    COMMAND hikari_font_fixture "${HIKARI_FONT_FIXTURE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/font-refresh-source"
    DEPENDS hikari_font_fixture
    COMMENT "Generating font fixtures")
add_custom_target(hikari_font_fixtures ALL DEPENDS "${HIKARI_FONT_FIXTURE_DIR}/base.ttf")
set(HIKARI_QT_EXAMPLE_FONTS "${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}")
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

add_test(NAME media-helper-manifest COMMAND "${CMAKE_COMMAND}"
    "-DHELPER=$<TARGET_FILE:hikari_media_helper>" -DNAME=hikari-media-helper -DROOTS=hikari-ffms2
    "-DSTATUS=${VCPKG_INSTALLED_DIR}/vcpkg/status" "-DINSTALLED=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
    "-DTRIPLET=${VCPKG_TARGET_TRIPLET}" "-DLICENCES=${PROJECT_SOURCE_DIR}/cmake/locks/media-helper-licences.json"
    "-DOUT=${CMAKE_BINARY_DIR}/provision-evidence/media-helper-manifest.json"
    -P "${PROJECT_SOURCE_DIR}/cmake/manifest/HelperManifest.cmake")
set_tests_properties(media-helper-manifest PROPERTIES LABELS packaging TIMEOUT 120)
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

hikari_add_gtest(hikari_backends_line_audition_tests SOURCES line_audition_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends PROPERTIES RUN_SERIAL TRUE)
target_compile_definitions(hikari_backends_line_audition_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_line_audition_tests hikari_media_helper hikari_media_fixtures)

hikari_add_qttest(hikari_backends_transport_tests SOURCES transport_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui Qt6::Multimedia)
target_compile_definitions(hikari_backends_transport_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_transport_tests hikari_media_helper hikari_media_fixtures)

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
target_include_directories(hikari_backends_font_collection_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
if(WIN32)
    target_link_libraries(hikari_backends_font_collection_tests PRIVATE dwrite gdi32 advapi32)
endif()

hikari_add_gtest(hikari_backends_automation_manager_tests SOURCES automation_manager_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_automation_manager_tests PRIVATE
    HIKARI_LUA_HELPER="$<TARGET_FILE:hikari_lua_helper>"
    HIKARI_LUA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/lua-fixtures"
    HIKARI_LUA_INCLUDE="${PROJECT_SOURCE_DIR}/Automation/automation/Include"
    HIKARI_EDGEBLUR_SCRIPT="${PROJECT_SOURCE_DIR}/Automation/automation/Autoload/macro-1-edgeblur.lua")
add_dependencies(hikari_backends_automation_manager_tests hikari_lua_helper)

hikari_add_gtest(hikari_backends_video_session_tests SOURCES video_session_tests.cpp
    LIBRARIES hikari_backends Qt6::Gui LABELS backends)
target_compile_definitions(hikari_backends_video_session_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
add_dependencies(hikari_backends_video_session_tests hikari_media_helper hikari_media_fixtures)

hikari_add_gtest(hikari_backends_legacy_spelling_tests SOURCES legacy_spelling_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
target_compile_definitions(hikari_backends_legacy_spelling_tests PRIVATE
    HIKARI_SPELLING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/spelling"
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-cases/inputs")

hikari_add_gtest(hikari_backends_bundled_dictionaries_tests SOURCES bundled_dictionaries_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
target_compile_definitions(hikari_backends_bundled_dictionaries_tests PRIVATE
    HIKARI_BUNDLED_DICTIONARIES="${HIKARI_BUNDLED_DICTIONARIES}"
    HIKARI_SPELLING_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/spelling")

hikari_add_gtest(hikari_backends_harper_client_tests SOURCES harper_client_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
target_compile_definitions(hikari_backends_harper_client_tests PRIVATE HIKARI_HARPER_LS="${HIKARI_HARPER_LS}")

hikari_add_gtest(hikari_backends_mecab_readings_tests SOURCES mecab_readings_tests.cpp
    LIBRARIES hikari_backends LABELS backends)
target_compile_definitions(hikari_backends_mecab_readings_tests PRIVATE HIKARI_UNIDIC_DIR="${HIKARI_UNIDIC_DIR}")

find_package(ZLIB REQUIRED)
hikari_add_gtest(hikari_backends_font_collector_tests SOURCES font_collector_tests.cpp
    LIBRARIES hikari_backends Qt6::Core ZLIB::ZLIB LABELS backends
    PROPERTIES ENVIRONMENT "FONTCONFIG_FILE=${CMAKE_CURRENT_BINARY_DIR}/font-fixtures.conf" RUN_SERIAL TRUE)
target_compile_definitions(hikari_backends_font_collector_tests PRIVATE
    HIKARI_FONT_FIXTURES="${HIKARI_FONT_FIXTURE_DIR}"
    HIKARI_FONTCONFIG_FILE="${CMAKE_CURRENT_BINARY_DIR}/font-fixtures.conf")
add_dependencies(hikari_backends_font_collector_tests hikari_font_fixtures)
target_include_directories(hikari_backends_font_collector_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
if(WIN32)
    target_link_libraries(hikari_backends_font_collector_tests PRIVATE dwrite gdi32 advapi32)
endif()

hikari_add_gtest(hikari_backends_matroska_tests SOURCES matroska_tests.cpp
    LIBRARIES hikari_backends hikari_application Qt6::Core LABELS backends)
target_include_directories(hikari_backends_matroska_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support/media")
target_compile_definitions(hikari_backends_matroska_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}"
    HIKARI_MUXED_MKV="${PROJECT_SOURCE_DIR}/tests/fixtures/matroska-muxed")
add_dependencies(hikari_backends_matroska_tests hikari_media_helper hikari_media_fixtures)
hikari_add_gtest(hikari_backends_matroska_capture_tests SOURCES matroska_capture_tests.cpp
    LIBRARIES hikari_backends Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_matroska_capture_tests PRIVATE
    HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
    HIKARI_MUXED_MKV="${PROJECT_SOURCE_DIR}/tests/fixtures/matroska-muxed"
    HIKARI_DEMUX_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-y9-demux-20261013/observations.jsonl")
add_dependencies(hikari_backends_matroska_capture_tests hikari_media_helper)

hikari_add_gtest(hikari_backends_visual_clip_render_tests SOURCES visual_clip_render_tests.cpp
    LIBRARIES hikari_backends hikari_application Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_visual_clip_render_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")

hikari_add_gtest(hikari_backends_visual_drawing_render_tests SOURCES visual_drawing_render_tests.cpp
    LIBRARIES hikari_backends hikari_application Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_visual_drawing_render_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")

hikari_add_gtest(hikari_backends_visual_move_render_tests SOURCES visual_move_render_tests.cpp
    LIBRARIES hikari_backends hikari_application Qt6::Gui LABELS backends)
target_compile_definitions(hikari_backends_visual_move_render_tests PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
    HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/visual-t2")
hikari_add_qttest(hikari_backends_show_in_folder_tests SOURCES show_in_folder_tests.cpp LIBRARIES hikari_backends)
if(WIN32)
    target_link_libraries(hikari_backends_show_in_folder_tests PRIVATE Qt6::Gui ole32 oleaut32 uuid)
else()
    target_link_libraries(hikari_backends_show_in_folder_tests PRIVATE Qt6::DBus)
endif()

hikari_add_gtest(hikari_backends_csri_tests SOURCES csri_renderer_tests.cpp
    $<$<NOT:$<PLATFORM_ID:Windows>>:${PROJECT_SOURCE_DIR}/src/backends/csri_renderer.cpp>
    LIBRARIES hikari_backends LABELS backends)
if(WIN32)
    hikari_add_gtest(hikari_backends_vsfiltermod_tests SOURCES csri_vsfiltermod_tests.cpp
        LIBRARIES hikari_backends Qt6::Gui LABELS backends PROPERTIES TIMEOUT 300)
    target_compile_definitions(hikari_backends_vsfiltermod_tests PRIVATE
        HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts/vsfiltermod"
        HIKARI_ATTACHMENT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/attachments"
        HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-${HIKARI_QT_VERSION}/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf"
        HIKARI_W2_LEGACY_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/local-w2-vsfiltermod-20261015")
    if(HIKARI_TEST_VSFILTERMOD_DLL)
        set(_w2_csri "${CMAKE_CURRENT_BINARY_DIR}/vsfiltermod-csri")
        add_custom_command(TARGET hikari_backends_vsfiltermod_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_w2_csri}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${HIKARI_TEST_VSFILTERMOD_DLL}" "${_w2_csri}/VSFiltermod.dll"
            VERBATIM)
        target_compile_definitions(hikari_backends_vsfiltermod_tests PRIVATE HIKARI_VSFILTERMOD_CSRI_DIR="${_w2_csri}")
    endif()
endif()
add_test(NAME vsfilter-task-environment COMMAND "${CMAKE_COMMAND}"
    "-DTASKS=${PROJECT_SOURCE_DIR}/winix.yaml" -DTASK=vsfiltermod-test
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/csri_vsfiltermod_tests.cpp"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/vsfilter_task_environment.cmake")
add_test(NAME vsfilter-task-environment-control COMMAND "${CMAKE_COMMAND}"
    "-DTASKS=${PROJECT_SOURCE_DIR}/winix.yaml" -DTASK=vsfiltermod-test
    "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/csri_vsfiltermod_tests.cpp"
    "-DDROP=$env:HIKARI_VSFILTERMOD_CSRI = \"$PWD/out/build/windows-x64-vsfiltermod/tests/backends/vsfiltermod-csri\"; "
    -P "${CMAKE_CURRENT_SOURCE_DIR}/vsfilter_task_environment.cmake")
set_tests_properties(vsfilter-task-environment-control PROPERTIES LABELS backends TIMEOUT 60 WILL_FAIL TRUE)
hikari_add_gtest(hikari_backends_directshow_frames_tests SOURCES directshow_frames_tests.cpp
    "${PROJECT_SOURCE_DIR}/src/backends/directshow/directshow_frames.cpp" LIBRARIES hikari_application)
target_include_directories(hikari_backends_directshow_frames_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/directshow")
if(WIN32)
    hikari_add_qttest(hikari_backends_directshow_tests SOURCES directshow_player_tests.cpp
        LIBRARIES hikari_directshow hikari_backends Qt6::Gui Qt6::Multimedia)
    target_include_directories(hikari_backends_directshow_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/backends/directshow"
        "${PROJECT_SOURCE_DIR}/tests/support")
    target_link_libraries(hikari_backends_directshow_tests PRIVATE hikari_dshow_baseclasses strmiids winmm ole32)
    target_compile_definitions(hikari_backends_directshow_tests PRIVATE
        HIKARI_MEDIA_HELPER="$<TARGET_FILE:hikari_media_helper>"
        HIKARI_MEDIA_FIXTURES="${HIKARI_MEDIA_FIXTURE_DIR}")
    add_dependencies(hikari_backends_directshow_tests hikari_media_helper hikari_media_fixtures)
    set_tests_properties(hikari_backends_directshow_tests.offscreen PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME directshow-manifest COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:hikari_directshow_probe>"
        "-DOUT=${CMAKE_BINARY_DIR}/provision-evidence/directshow-manifest.json"
        -P "${PROJECT_SOURCE_DIR}/cmake/manifest/DirectShowManifest.cmake")
    set_tests_properties(directshow-manifest PROPERTIES LABELS packaging TIMEOUT 120)
endif()
if(WIN32)
    set(_hikari_app_code hikarisub_app)
else()
    set(_hikari_app_code hikarisub)
endif()
add_test(NAME directshow-adapter-presence COMMAND "${CMAKE_COMMAND}"
    "-DEXECUTABLE=$<TARGET_FILE:${_hikari_app_code}>" "-DEXPECT=$<IF:$<PLATFORM_ID:Windows>,present,absent>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/directshow_presence.cmake")
set_tests_properties(directshow-adapter-presence PROPERTIES LABELS packaging TIMEOUT 120)

hikari_add_gtest(hikari_backends_ai_config_store_tests SOURCES ai_config_store_tests.cpp
    LIBRARIES hikari_ai Qt6::Core LABELS backends)
hikari_add_gtest(hikari_backends_ai_keychain_tests SOURCES ai_keychain_tests.cpp
    LIBRARIES hikari_ai Qt6::Core LABELS backends)
add_executable(hikari_sandbox_probe sandbox_probe.cpp)
if(WIN32)
    target_link_libraries(hikari_sandbox_probe PRIVATE ws2_32)
endif()
hikari_add_gtest(hikari_backends_sandbox_tests SOURCES sandbox_tests.cpp
    LIBRARIES hikari_sandbox Qt6::Core Qt6::Network LABELS backends)
target_compile_definitions(hikari_backends_sandbox_tests PRIVATE
    HIKARI_SANDBOX_PROBE="$<TARGET_FILE:hikari_sandbox_probe>")
add_dependencies(hikari_backends_sandbox_tests hikari_sandbox_probe)
add_executable(hikari_fake_ai_sidecar fake_ai_sidecar.cpp)
target_link_libraries(hikari_fake_ai_sidecar PRIVATE hikari_api)
hikari_add_gtest(hikari_backends_ai_sidecar_host_tests SOURCES ai_sidecar_host_tests.cpp
    LIBRARIES hikari_ai hikari_core Qt6::Core Qt6::Test LABELS backends)
target_include_directories(hikari_backends_ai_sidecar_host_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/api")
target_compile_definitions(hikari_backends_ai_sidecar_host_tests PRIVATE
    HIKARI_FAKE_AI_SIDECAR="$<TARGET_FILE:hikari_fake_ai_sidecar>")
add_dependencies(hikari_backends_ai_sidecar_host_tests hikari_fake_ai_sidecar)
hikari_add_gtest(hikari_backends_ai_requests_tests SOURCES ai_requests_tests.cpp
    LIBRARIES hikari_ai Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_ai_requests_tests PRIVATE
    HIKARI_FAKE_AI_SIDECAR="$<TARGET_FILE:hikari_fake_ai_sidecar>")
add_dependencies(hikari_backends_ai_requests_tests hikari_fake_ai_sidecar)
hikari_add_gtest(hikari_backends_ai_agent_tests SOURCES ai_agent_tests.cpp
    LIBRARIES hikari_ai hikari_core Qt6::Core LABELS backends)
target_include_directories(hikari_backends_ai_agent_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/api")
target_compile_definitions(hikari_backends_ai_agent_tests PRIVATE
    HIKARI_FAKE_AI_SIDECAR="$<TARGET_FILE:hikari_fake_ai_sidecar>")
add_dependencies(hikari_backends_ai_agent_tests hikari_fake_ai_sidecar)
hikari_add_gtest(hikari_backends_ai_http_relay_tests SOURCES ai_http_relay_tests.cpp
    LIBRARIES hikari_ai Qt6::Core Qt6::Network LABELS backends)
add_executable(hikari_fake_agent fake_agent.cpp)
target_link_libraries(hikari_fake_agent PRIVATE hikari_api)
hikari_add_gtest(hikari_backends_ai_agent_broker_tests SOURCES ai_agent_broker_tests.cpp
    LIBRARIES hikari_ai Qt6::Core LABELS backends)
target_compile_definitions(hikari_backends_ai_agent_broker_tests PRIVATE
    HIKARI_FAKE_AGENT="$<TARGET_FILE:hikari_fake_agent>")
add_dependencies(hikari_backends_ai_agent_broker_tests hikari_fake_agent)
hikari_add_gtest(hikari_backends_ai_signin_tests SOURCES ai_signin_tests.cpp
    LIBRARIES hikari_ai Qt6::Core Qt6::Network LABELS backends)
target_include_directories(hikari_backends_ai_signin_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
if(HIKARI_AI_SIDECAR)
    hikari_add_gtest(hikari_backends_ai_tools_tests SOURCES ai_tools_tests.cpp
        LIBRARIES hikari_ai hikari_core Qt6::Core LABELS backends ai-sidecar)
    target_include_directories(hikari_backends_ai_tools_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/api")
    target_compile_definitions(hikari_backends_ai_tools_tests PRIVATE
        HIKARI_AI_RUNTIME_FILE="${HIKARI_AI_RUNTIME_FILE}" HIKARI_AI_BUNDLE_FILE="${HIKARI_AI_BUNDLE_FILE}"
        HIKARI_AI_BUILD_ID="${HIKARI_AI_BUILD_ID}")
    add_dependencies(hikari_backends_ai_tools_tests hikari_ai_sidecar)
    hikari_add_gtest(hikari_backends_ai_relay_sidecar_tests SOURCES ai_relay_sidecar_tests.cpp
        LIBRARIES hikari_ai Qt6::Core Qt6::Network LABELS backends ai-sidecar)
    target_compile_definitions(hikari_backends_ai_relay_sidecar_tests PRIVATE
        HIKARI_AI_RUNTIME_FILE="${HIKARI_AI_RUNTIME_FILE}" HIKARI_AI_BUNDLE_FILE="${HIKARI_AI_BUNDLE_FILE}"
        HIKARI_AI_BUILD_ID="${HIKARI_AI_BUILD_ID}")
    add_dependencies(hikari_backends_ai_relay_sidecar_tests hikari_ai_sidecar)
    hikari_add_gtest(hikari_backends_ai_mcp_tests SOURCES ai_mcp_tests.cpp
        LIBRARIES hikari_ai hikari_core Qt6::Core Qt6::Network LABELS backends ai-sidecar)
    target_include_directories(hikari_backends_ai_mcp_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/api")
    target_compile_definitions(hikari_backends_ai_mcp_tests PRIVATE
        HIKARI_AI_RUNTIME_FILE="${HIKARI_AI_RUNTIME_FILE}" HIKARI_AI_BUNDLE_FILE="${HIKARI_AI_BUNDLE_FILE}"
        HIKARI_AI_BUILD_ID="${HIKARI_AI_BUILD_ID}")
    add_dependencies(hikari_backends_ai_mcp_tests hikari_ai_sidecar)
endif()
find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS CorePrivate)
hikari_add_gtest(hikari_backends_ai_sidecar_updater_tests SOURCES ai_sidecar_updater_tests.cpp
    LIBRARIES hikari_ai Qt6::Core Qt6::CorePrivate Qt6::Network ZLIB::ZLIB Hikari::sodium LABELS backends)
target_include_directories(hikari_backends_ai_sidecar_updater_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
add_executable(hikari_fake_agent_cli fake_agent_cli.cpp)
hikari_add_gtest(hikari_backends_ai_agent_cli_tests SOURCES ai_agent_cli_tests.cpp
    LIBRARIES hikari_ai Qt6::Core Qt6::CorePrivate Qt6::Network ZLIB::ZLIB Hikari::sodium LABELS backends)
target_include_directories(hikari_backends_ai_agent_cli_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
target_compile_definitions(hikari_backends_ai_agent_cli_tests PRIVATE
    HIKARI_FAKE_AGENT_CLI="$<TARGET_FILE:hikari_fake_agent_cli>")
add_dependencies(hikari_backends_ai_agent_cli_tests hikari_fake_agent_cli)
