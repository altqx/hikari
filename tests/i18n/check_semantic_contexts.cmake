include("${CMAKE_CURRENT_LIST_DIR}/extract_sources.cmake")
execute_process(COMMAND "${TOOL}" --contexts "${SOURCE_DIR}/i18n/contexts.tsv" --extracted "${EXTRACTED}"
    --sources "${SOURCE_DIR}/src" RESULT_VARIABLE linted OUTPUT_VARIABLE summary ERROR_VARIABLE problems)
if(NOT linted EQUAL 0)
    message(FATAL_ERROR "${problems}")
endif()
message(STATUS "${summary}")
