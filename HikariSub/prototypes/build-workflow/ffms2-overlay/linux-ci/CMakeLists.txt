cmake_minimum_required(VERSION 3.28)
# Throwaway CI driver; the real source consumer remains the parent workflow.
# Acquire and verify its toolchain before any compiler-enabled project starts.
include("${CMAKE_CURRENT_LIST_DIR}/provision.cmake")
project(HikariFFMS2LinuxFeasibility LANGUAGES NONE)
add_custom_target(observe ALL
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/build.cmake"
    USES_TERMINAL VERBATIM)
