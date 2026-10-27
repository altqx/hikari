# Performance runs are separate from ordinary tests: label "perf", excluded by
# the *-verify presets and run by the *-perf test presets.
add_executable(hikari_core_perf core_perf.cpp)
target_link_libraries(hikari_core_perf PRIVATE hikari_perf_harness hikari_core)
add_test(NAME perf.core
    COMMAND hikari_core_perf "${CMAKE_BINARY_DIR}/perf-report-core.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/reference-hosts.json")
set_tests_properties(perf.core PROPERTIES LABELS perf TIMEOUT 600 RUN_SERIAL TRUE)

add_executable(hikari_ui_perf ui_perf.cpp)
target_link_libraries(hikari_ui_perf PRIVATE hikari_perf_harness hikari_ui hikari_core Qt6::Gui)
add_test(NAME perf.ui
    COMMAND hikari_ui_perf "${CMAKE_BINARY_DIR}/perf-report-ui.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/reference-hosts.json")
set_tests_properties(perf.ui PROPERTIES LABELS perf TIMEOUT 600 RUN_SERIAL TRUE ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# SV1: libass drawing a heavy script at the video's and at screen sizes.
add_executable(hikari_subtitle_perf subtitle_perf.cpp)
target_link_libraries(hikari_subtitle_perf PRIVATE hikari_perf_harness hikari_backends Qt6::Core)
target_compile_definitions(hikari_subtitle_perf PRIVATE
    HIKARI_TEST_FONT="${HIKARI_QT_PREFIX}/../../Examples/Qt-6.11.2/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-Regular.ttf")
add_test(NAME perf.subtitles
    COMMAND hikari_subtitle_perf "${CMAKE_BINARY_DIR}/perf-report-subtitles.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/reference-hosts.json")
set_tests_properties(perf.subtitles PROPERTIES LABELS perf TIMEOUT 600 RUN_SERIAL TRUE)

# Ordinary (non-perf) self-checks for the harness itself.
hikari_add_gtest(hikari_perf_harness_tests SOURCES harness_tests.cpp
    LIBRARIES hikari_perf_harness LABELS control)
