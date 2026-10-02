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
