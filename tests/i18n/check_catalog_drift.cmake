include("${CMAKE_CURRENT_LIST_DIR}/extract_sources.cmake")
string(REPLACE "|" ";" catalogs "${CATALOGS}")
execute_process(COMMAND "${TOOL}" --extracted "${EXTRACTED}" --catalogs "${catalogs}"
    RESULT_VARIABLE checked OUTPUT_VARIABLE summary ERROR_VARIABLE problems)
if(NOT checked EQUAL 0)
    message(FATAL_ERROR "${problems}")
endif()
message(STATUS "${summary}")
