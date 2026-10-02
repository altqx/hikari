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
    HIKARI_CAPTURE_INPUTS="${PROJECT_SOURCE_DIR}/tools/legacy-capture/inputs")
