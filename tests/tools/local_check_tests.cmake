cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED WORK OR WORK STREQUAL "")
    message(FATAL_ERROR "WORK is required; use a disposable directory outside the source files.")
endif()
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(WORK "${WORK}" ABSOLUTE)
string(RANDOM LENGTH 12 nonce)
set(work "${WORK}/${nonce} local checks")
file(MAKE_DIRECTORY "${work}")

function(make_fixture name)
    set(fixture "${work}/${name}" PARENT_SCOPE)
    set(fixture "${work}/${name}")
    file(MAKE_DIRECTORY "${fixture}/tools" "${fixture}/.githooks")
    file(COPY "${root}/tools/local-check.cmake" DESTINATION "${fixture}/tools")
    file(COPY "${root}/.githooks/pre-push" DESTINATION "${fixture}/.githooks")
    file(WRITE "${fixture}/CMakePresets.json" [=[
{
  "version": 6,
  "configurePresets": [{"name": "fixture", "generator": "Ninja", "binaryDir": "${sourceDir}/build"}],
  "buildPresets": [{"name": "fixture", "configurePreset": "fixture"}],
  "testPresets": [{"name": "fixture", "configurePreset": "fixture", "filter": {"exclude": {"label": "perf"}}, "output": {"outputOnFailure": true}}]
}
]=])
    file(WRITE "${fixture}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(LocalCheckFixture LANGUAGES NONE)
enable_testing()
add_custom_target(build-marker ALL COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_SOURCE_DIR}/built")
if(FAIL_BUILD)
    add_custom_target(broken ALL COMMAND "${CMAKE_COMMAND}" -E false)
endif()
foreach(name IN ITEMS pass.one pass.two excluded.perf)
    add_test(NAME "${name}" COMMAND "${CMAKE_COMMAND}" "-DLOG=${CMAKE_SOURCE_DIR}/ran"
        "-DNAME=${name}" "-DFAIL_TEST=${FAIL_TEST}" -P "${CMAKE_SOURCE_DIR}/record.cmake")
endforeach()
set_tests_properties(excluded.perf PROPERTIES LABELS perf)
]=])
    file(WRITE "${fixture}/record.cmake" [=[
file(APPEND "${LOG}" "${NAME}\n")
if(FAIL_TEST OR NAME STREQUAL "excluded.perf")
    message(FATAL_ERROR "Fixture test failure")
endif()
]=])
    if(NOT name STREQUAL "unconfigured")
        execute_process(COMMAND "${CMAKE_COMMAND}" --preset fixture ${ARGN}
            WORKING_DIRECTORY "${fixture}" OUTPUT_VARIABLE output ERROR_VARIABLE error
            RESULT_VARIABLE result)
        if(NOT result STREQUAL "0")
            message(FATAL_ERROR "Cannot configure fixture '${name}': ${output}${error}")
        endif()
    endif()
endfunction()

function(expect name expected_result expected_output expected_tests)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${fixture}"
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    file(WRITE "${work}/${name}.log" "${output}${error}")
    if(expected_result STREQUAL "pass")
        if(NOT result STREQUAL "0")
            message(FATAL_ERROR "${name} unexpectedly failed (${result}): ${output}${error}")
        endif()
    elseif(result STREQUAL "0")
        message(FATAL_ERROR "${name} unexpectedly passed: ${output}${error}")
    endif()
    if(NOT "${output}${error}" MATCHES "${expected_output}")
        message(FATAL_ERROR "${name} did not report '${expected_output}': ${output}${error}")
    endif()
    set(actual_tests "")
    if(EXISTS "${fixture}/ran")
        file(STRINGS "${fixture}/ran" actual_tests)
        list(SORT actual_tests)
    endif()
    if(NOT actual_tests STREQUAL expected_tests)
        message(FATAL_ERROR "${name} ran '${actual_tests}', expected '${expected_tests}'")
    endif()
    message(STATUS "PASS ${name}")
endfunction()

make_fixture(full)
expect(full pass "Running 2 selected tests" "pass.one;pass.two"
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)
if(NOT EXISTS "${fixture}/built")
    message(FATAL_ERROR "The full check did not build the preset")
endif()

make_fixture(filtered)
expect(filtered pass "Running 1 selected tests" "pass.two"
    "${CMAKE_COMMAND}" -DPRESET=fixture "-DTEST_REGEX=^pass[.]two$" -P tools/local-check.cmake)

make_fixture(quoted-regex)
expect(quoted-regex pass "Running 1 selected tests" "pass.one"
    "${CMAKE_COMMAND}" -DPRESET=fixture "-DTEST_REGEX=^(pass[.]one|never\\;match with spaces)$" -P tools/local-check.cmake)

make_fixture(no-match)
expect(no-match fail "selection is empty" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -DTEST_REGEX=never -P tools/local-check.cmake)

make_fixture(semicolon-regex)
expect(semicolon-regex fail "selection is empty" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture "-DTEST_REGEX=^pass[.]two$\\;^pass[.]one$" -P tools/local-check.cmake)

make_fixture(empty-regex)
expect(empty-regex fail "TEST_REGEX is empty" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -DTEST_REGEX= -P tools/local-check.cmake)
if(EXISTS "${fixture}/built")
    message(FATAL_ERROR "An invalid regex reached the build")
endif()

make_fixture(missing-preset)
expect(missing-preset fail "PRESET is required" ""
    "${CMAKE_COMMAND}" -P tools/local-check.cmake)

make_fixture(invalid-preset)
expect(invalid-preset fail "existing matching build/test preset" ""
    "${CMAKE_COMMAND}" -DPRESET=missing -P tools/local-check.cmake)

make_fixture(unconfigured)
expect(unconfigured fail "configure[ \r\n]+its tree first" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)
if(EXISTS "${fixture}/build/CMakeCache.txt")
    message(FATAL_ERROR "The local check configured the missing tree")
endif()

make_fixture(build-failure -DFAIL_BUILD=ON)
expect(build-failure fail "Build failed" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)

make_fixture(discovery-failure)
file(WRITE "${fixture}/build/CTestTestfile.cmake" "unknown_ctest_command()\n")
expect(discovery-failure fail "CTest discovery failed" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)

make_fixture(missing-test-preset)
file(READ "${fixture}/CMakePresets.json" presets)
string(JSON presets REMOVE "${presets}" testPresets)
file(WRITE "${fixture}/CMakePresets.json" "${presets}")
expect(missing-test-preset fail "CTest discovery failed" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)

make_fixture(malformed-discovery)
file(APPEND "${fixture}/build/CTestTestfile.cmake"
    "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E echo not-json)\n")
expect(malformed-discovery fail "malformed test inventory" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)

make_fixture(not-built)
file(APPEND "${fixture}/build/CTestTestfile.cmake"
    "add_test(discovered_NOT_BUILT \"${CMAKE_COMMAND}\" -E true)\n")
expect(not-built fail "unresolved or malformed test" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)

make_fixture(missing-executable)
file(APPEND "${fixture}/build/CTestTestfile.cmake"
    "add_test(missing_executable \"${fixture}/absent-executable\")\n")
expect(missing-executable fail "unresolved or malformed test" ""
    "${CMAKE_COMMAND}" -DPRESET=fixture -P tools/local-check.cmake)

make_fixture(test-failure -DFAIL_TEST=ON)
expect(test-failure fail "CTest execution failed" "pass.two"
    "${CMAKE_COMMAND}" -DPRESET=fixture "-DTEST_REGEX=^pass[.]two$" -P tools/local-check.cmake)

find_package(Git REQUIRED)
make_fixture(hook)
function(git_fixture)
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN} WORKING_DIRECTORY "${fixture}"
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Git fixture setup failed: ${output}${error}")
    endif()
endfunction()
git_fixture(init --quiet)
git_fixture(add CMakeLists.txt)
git_fixture(-c core.hooksPath= -c user.name=Fixture -c user.email=fixture@example.invalid
    -c commit.gpgSign=false commit --quiet -m fixture)
git_fixture(init --bare --quiet "${work}/remote.git")

set(push "${GIT_EXECUTABLE}" -c "core.hooksPath=${fixture}/.githooks"
    push --quiet "${work}/remote.git" HEAD:refs/heads/checked)
set(clean_env "${CMAKE_COMMAND}" -E env --unset=HIKARI_LOCAL_CHECK_PRESET
    --unset=HIKARI_LOCAL_CHECK_TEST_REGEX --unset=HIKARI_SKIP_LOCAL_CHECK)

expect(hook-missing-preset fail "Local checks require" "" ${clean_env} ${push})
expect(hook-invalid-preset fail "existing matching build/test preset" "" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=invalid ${push})
expect(hook-empty-regex fail "TEST_REGEX is empty" "" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=fixture HIKARI_LOCAL_CHECK_TEST_REGEX= ${push})
expect(hook-no-match fail "selection is empty" "" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=fixture HIKARI_LOCAL_CHECK_TEST_REGEX=never ${push})
expect(hook-semicolon-regex fail "selection is empty" "" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=fixture "HIKARI_LOCAL_CHECK_TEST_REGEX=^pass[.]two$\\;^pass[.]one$" ${push})
expect(hook-bypass pass "bypasses local checks" "" ${clean_env}
    HIKARI_SKIP_LOCAL_CHECK=1 ${push})
git_fixture(--git-dir=${work}/remote.git update-ref -d refs/heads/checked)

expect(hook-env-preset pass "Running 1 selected tests" "pass.one" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=fixture "HIKARI_LOCAL_CHECK_TEST_REGEX=^(pass[.]one|never\\;match with spaces)$" ${push})
file(REMOVE "${fixture}/ran")
git_fixture(--git-dir=${work}/remote.git update-ref -d refs/heads/checked)
git_fixture(config --local hikari.localCheckPreset fixture)
expect(hook-local-config pass "Running 2 selected tests" "pass.one;pass.two" ${clean_env} ${push})
file(REMOVE "${fixture}/ran")
git_fixture(--git-dir=${work}/remote.git update-ref -d refs/heads/checked)
expect(hook-env-overrides-config fail "existing matching build/test preset" "" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=invalid ${push})
expect(hook-empty-env-overrides-config fail "Local checks require" "" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET= ${push})

execute_process(COMMAND "${CMAKE_COMMAND}" --preset fixture -DFAIL_TEST=ON
    WORKING_DIRECTORY "${fixture}" COMMAND_ERROR_IS_FATAL ANY)
expect(hook-failed-test fail "CTest execution failed" "pass.one;pass.two" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=fixture ${push})

file(WRITE "${fixture}/consume-input.sh" [=[
#!/bin/sh
while IFS= read -r line; do
    printf '%s\n' "$line"
done > "$1"
]=])
file(APPEND "${fixture}/CMakeLists.txt" [=[
find_program(SH_EXECUTABLE NAMES sh REQUIRED)
add_custom_target(read-stdin ALL
    COMMAND "${SH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/consume-input.sh" "${CMAKE_SOURCE_DIR}/consumed"
    USES_TERMINAL VERBATIM)
]=])
file(MAKE_DIRECTORY "${fixture}/manager")
file(WRITE "${fixture}/manager/pre-push" [=[
#!/bin/sh
set -eu
sh "$(git rev-parse --show-toplevel)/.githooks/pre-push" "$@" || exit $?
sh ./consume-input.sh remaining
]=])
file(CHMOD "${fixture}/manager/pre-push" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
execute_process(COMMAND "${CMAKE_COMMAND}" --preset fixture -DFAIL_TEST=OFF
    WORKING_DIRECTORY "${fixture}" COMMAND_ERROR_IS_FATAL ANY)
set(push "${GIT_EXECUTABLE}" -c "core.hooksPath=${fixture}/manager"
    push --quiet "${work}/remote.git" HEAD:refs/heads/checked)
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${fixture}"
    OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(LENGTH "${head}" hash_length)
string(REPEAT "0" ${hash_length} missing_ref)
foreach(mode IN ITEMS full filtered)
    file(REMOVE "${fixture}/ran" "${fixture}/remaining")
    set(selection)
    set(expected_tests "pass.one;pass.two")
    if(mode STREQUAL "filtered")
        set(selection "HIKARI_LOCAL_CHECK_TEST_REGEX=^pass[.]two$")
        set(expected_tests "pass.two")
    endif()
    expect(hook-stdin-${mode} pass "selected tests" "${expected_tests}" ${clean_env}
        HIKARI_LOCAL_CHECK_PRESET=fixture ${selection} ${push})
    file(READ "${fixture}/consumed" consumed)
    file(READ "${fixture}/remaining" remaining)
    if(NOT consumed STREQUAL "" OR
            NOT remaining STREQUAL "HEAD ${head} refs/heads/checked ${missing_ref}\n")
        message(FATAL_ERROR "The ${mode} local check consumed or changed the next hook's ref-list input")
    endif()
    git_fixture(--git-dir=${work}/remote.git update-ref -d refs/heads/checked)
endforeach()
file(REMOVE "${fixture}/ran" "${fixture}/remaining")
execute_process(COMMAND "${CMAKE_COMMAND}" --preset fixture -DFAIL_TEST=ON
    WORKING_DIRECTORY "${fixture}" COMMAND_ERROR_IS_FATAL ANY)
expect(hook-stdin-failed-test fail "CTest execution failed" "pass.one;pass.two" ${clean_env}
    HIKARI_LOCAL_CHECK_PRESET=fixture ${push})
if(EXISTS "${fixture}/remaining")
    message(FATAL_ERROR "The next hook ran after a rejected local check")
endif()
message(STATUS "Local check regression fixtures passed. Logs: ${work}")
