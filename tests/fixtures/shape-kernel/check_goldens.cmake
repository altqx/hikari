execute_process(COMMAND "${LUAJIT}" "${SCRIPT}" WORKING_DIRECTORY "${INCLUDE_DIR}" OUTPUT_FILE "${OUT}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "make_goldens.lua failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${GOLDENS}" RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
    message(FATAL_ERROR "The shipped Yutils.lua no longer gives ${GOLDENS}; rerun make_goldens.lua")
endif()
