# Test-only performance harness (E2-perf). Never linked into shipping targets.
add_library(hikari_perf_harness STATIC perf_harness.cpp)
target_include_directories(hikari_perf_harness PUBLIC .)
