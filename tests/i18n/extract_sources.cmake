execute_process(COMMAND "${CMAKE_COMMAND}" -DIN_FILE=${PROJECT_IN} -DOUT_FILE=${PROJECT_JSON}
    -P "${GENERATE_PROJECT}" RESULT_VARIABLE generated)
if(NOT generated EQUAL 0)
    message(FATAL_ERROR "could not write ${PROJECT_JSON} from ${PROJECT_IN}")
endif()
get_filename_component(extracted_dir "${EXTRACTED}" DIRECTORY)
file(MAKE_DIRECTORY "${extracted_dir}")
file(REMOVE "${EXTRACTED}")
execute_process(COMMAND "${LUPDATE}" -project "${PROJECT_JSON}" -locations absolute -no-obsolete -silent -ts "${EXTRACTED}"
    RESULT_VARIABLE extracted ERROR_VARIABLE extract_errors)
if(NOT extracted EQUAL 0 OR NOT EXISTS "${EXTRACTED}")
    message(FATAL_ERROR "lupdate could not extract the sources: ${extract_errors}")
endif()
