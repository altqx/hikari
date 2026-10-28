cmake_minimum_required(VERSION 3.25)

# This throwaway checks the pre-project boundary only. It never provisions an SDK.
if(CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "This preflight does not load a toolchain. Use its supplied preset without an external toolchain.")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/preflight.cmake")

project(HikariLinuxDependencyPreflight LANGUAGES NONE)
add_custom_target(preflight-report
    COMMAND "${CMAKE_COMMAND}" -E echo
        "Preflight collection completed. Dependency configure/build remains BLOCKED; see evidence/report.md."
    VERBATIM)
