# cmake -DEXECUTABLE=<QtTest executable> -DSPECS=<FROM..BEFORE>,... -P QtTestParts.cmake
# hikari_add_qttest's PARTS check: the parts' functions, in order, are the executable's functions, each once.
cmake_minimum_required(VERSION 3.25)

function(functions_of out)
    execute_process(COMMAND "${EXECUTABLE}" ${ARGN} -functions
        RESULT_VARIABLE code OUTPUT_VARIABLE listed ERROR_VARIABLE errors)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "${EXECUTABLE} ${ARGN} -functions failed (${code}): ${errors}")
    endif()
    string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*\\(\\)" names "${listed}")
    set(${out} "${names}" PARENT_SCOPE)
endfunction()

functions_of(all)
list(LENGTH all total)
if(total EQUAL 0)
    message(FATAL_ERROR "${EXECUTABLE} lists no test functions")
endif()
string(REPLACE "," ";" specs "${SPECS}")
set(parts)
foreach(spec IN LISTS specs)
    functions_of(part -part "${spec}")
    list(LENGTH part count)
    message(STATUS "-part ${spec}: ${count} functions")
    list(APPEND parts ${part})
endforeach()
if(NOT parts STREQUAL all)
    set(missing ${all})
    list(REMOVE_ITEM missing ${parts})
    message(FATAL_ERROR "The parts do not run each of the ${total} functions exactly once, in order. "
        "Not in any part: ${missing}")
endif()
# A part naming an unknown function fails instead of running nothing.
execute_process(COMMAND "${EXECUTABLE}" -part "noSuchTestFunction.." -functions
    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
if(code EQUAL 0)
    message(FATAL_ERROR "-part with an unknown function did not fail")
endif()
message(STATUS "${total} functions in ${SPECS}")
