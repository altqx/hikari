cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED PRESET OR PRESET STREQUAL "")
    message(FATAL_ERROR "PRESET is required. Choose an existing configured build/test preset from docs/build.md.")
endif()
if(DEFINED TEST_REGEX AND TEST_REGEX STREQUAL "")
    message(FATAL_ERROR "TEST_REGEX is empty. Omit it to check the full preset.")
endif()

get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
message(STATUS "Local check: build/test preset '${PRESET}'")
execute_process(COMMAND "${CMAKE_COMMAND}" --build --preset "${PRESET}"
    WORKING_DIRECTORY "${root}" RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR
        "Build failed (${result}). Use an existing matching build/test preset and configure its tree first; see docs/build.md. Local checks do not provision or configure a missing tree.")
endif()

string(REPLACE ";" "\\;" preset_arg "${PRESET}")
set(selection --preset "${preset_arg}")
if(DEFINED TEST_REGEX)
    message(STATUS "Focused local feedback only, not CI qualification: TEST_REGEX='${TEST_REGEX}'")
    string(REPLACE ";" "\\;" regex_arg "${TEST_REGEX}")
    list(APPEND selection --tests-regex "${regex_arg}")
else()
    message(STATUS "Checking the full preset selection")
endif()

execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" ${selection} --show-only=json-v1
    WORKING_DIRECTORY "${root}" OUTPUT_VARIABLE inventory RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "CTest discovery failed (${result}). Check the test preset '${PRESET}' and its configured tree.")
endif()

string(JSON tests_type ERROR_VARIABLE json_error TYPE "${inventory}" tests)
if(json_error OR NOT tests_type STREQUAL "ARRAY")
    message(FATAL_ERROR "CTest discovery returned a malformed test inventory: ${json_error}")
endif()
string(JSON count LENGTH "${inventory}" tests)
if(count EQUAL 0)
    message(FATAL_ERROR "CTest selection is empty. Check PRESET and TEST_REGEX; no tests were run.")
endif()

math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
    string(JSON name ERROR_VARIABLE name_error GET "${inventory}" tests ${index} name)
    string(JSON command_type ERROR_VARIABLE command_error TYPE "${inventory}" tests ${index} command)
    if(name_error OR name STREQUAL "" OR name MATCHES "_NOT_BUILT" OR
            command_error OR NOT command_type STREQUAL "ARRAY")
        message(FATAL_ERROR "CTest selection has an unresolved or malformed test '${name}'. Build its test executable before checking.")
    endif()
    string(JSON command_size LENGTH "${inventory}" tests ${index} command)
    if(command_size EQUAL 0)
        message(FATAL_ERROR "CTest test '${name}' has no command.")
    endif()
endforeach()

message(STATUS "Running ${count} selected tests")
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" ${selection} --no-tests=error
    WORKING_DIRECTORY "${root}" RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "CTest execution failed (${result}) for preset '${PRESET}'.")
endif()
