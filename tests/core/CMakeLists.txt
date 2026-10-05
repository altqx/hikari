# Core behavior tests. Qt Core is used here only to read JSON fixtures; the
# core library itself stays Qt-free (enforced by HikariModules).
hikari_add_gtest(hikari_core_time_tests SOURCES time_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_time_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures"
    HIKARI_LEGACY_OBSERVATIONS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-36591631319/observations.json")

hikari_add_gtest(hikari_core_ass_load_tests SOURCES ass_load_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_ass_load_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")

hikari_add_gtest(hikari_core_ass_save_tests SOURCES ass_save_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_ass_save_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures"
    HIKARI_LEGACY_OUTPUTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-36591631319/outputs")

hikari_add_gtest(hikari_core_srt_tests SOURCES srt_tests.cpp LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_srt_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures"
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs"
    HIKARI_CRLF_OBSERVATION="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-37011681370/outputs/srt-crlf-blank-lines/saved-crlf-two-cues.srt")

hikari_add_gtest(hikari_core_line_format_tests SOURCES line_formats_tests.cpp LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_line_format_tests PRIVATE
    HIKARI_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/docs/qt/fixtures/approved-departures")

hikari_add_gtest(hikari_core_style_tests SOURCES style_tests.cpp LIBRARIES hikari_core Qt6::Core LABELS core)
target_compile_definitions(hikari_core_style_tests PRIVATE
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs"
    HIKARI_C4SSA_OUTPUTS="${PROJECT_SOURCE_DIR}/tests/fixtures/legacy-observations/run-37014681128/outputs/c4-ssa-edge-families")

hikari_add_gtest(hikari_core_text_projection_tests SOURCES text_projection_tests.cpp
    LIBRARIES hikari_core Qt6::Core LABELS core)

hikari_add_gtest(hikari_core_tag_commands_tests SOURCES tag_commands_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_editor_font_colour_tests SOURCES editor_font_colour_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_clipboard_rows_tests SOURCES clipboard_rows_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_conversion_tests SOURCES conversion_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_legacy_regex_tests SOURCES legacy_regex_tests.cpp LIBRARIES hikari_core LABELS core)
hikari_add_gtest(hikari_core_spelling_tests SOURCES spelling_tests.cpp LIBRARIES hikari_core LABELS core)

# Y7: the colour picker's conversions against legacy colorspace.cpp itself,
# copied unchanged into the build and compiled against the stubs in
# legacy-colorspace (a quoted include looks beside the source first, so the
# copy keeps HikariSub/config.h out).
set(_legacy_colorspace "${CMAKE_CURRENT_BINARY_DIR}/legacy-colorspace")
configure_file("${PROJECT_SOURCE_DIR}/HikariSub/colorspace.cpp" "${_legacy_colorspace}/colorspace.cpp" COPYONLY)
configure_file("${PROJECT_SOURCE_DIR}/HikariSub/colorspace.h" "${_legacy_colorspace}/colorspace.h" COPYONLY)
hikari_add_gtest(hikari_core_colour_space_tests
    SOURCES colour_space_tests.cpp "${_legacy_colorspace}/colorspace.cpp"
    LIBRARIES hikari_core LABELS core)
target_include_directories(hikari_core_colour_space_tests PRIVATE
    "${_legacy_colorspace}" "${CMAKE_CURRENT_SOURCE_DIR}/legacy-colorspace")
