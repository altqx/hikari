include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
file(WRITE "${evidence}/status.txt" "BUILD_RUNNING: source build not yet passed\n")
log_command(linux-overlay-workflow "${CMAKE_COMMAND}" --workflow --preset linux-overlay)
file(WRITE "${evidence}/status.txt" "SLICE_PASSED: parent configure/build/three-frame CTest completed; not full Task48 qualification\n")
