# Test runner integration (E2 cards). Each runner family gets a CTest label so
# `ctest -L <label>` selects it; every test has a bounded run time.
include_guard(GLOBAL)

find_package(GTest 1.18.0 CONFIG REQUIRED)
include(GoogleTest)

# hikari_add_gtest(<target> SOURCES <src>... [LIBRARIES <lib>...] [LABELS <label>...])
# Plain-C++ tests; each test case becomes its own CTest entry labelled "gtest".
function(hikari_add_gtest target)
    cmake_parse_arguments(arg "" "" "SOURCES;LIBRARIES;LABELS;PROPERTIES" ${ARGN})
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE GTest::gtest_main ${arg_LIBRARIES})
    gtest_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "gtest;${arg_LABELS}" TIMEOUT 60 ${arg_PROPERTIES})
endfunction()

find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)
find_program(HIKARI_XVFB_RUN xvfb-run)

# Register one command in the platform modes available on this host: always
# offscreen, plus a real X server through xvfb-run when it is installed.
function(_hikari_add_qt_modes name label)
    cmake_parse_arguments(mode "OFFSCREEN_ONLY" "" "" ${ARGN})
    set(ARGN ${mode_UNPARSED_ARGUMENTS})
    add_test(NAME ${name}.offscreen COMMAND ${ARGN})
    set_tests_properties(${name}.offscreen PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "${label};offscreen" TIMEOUT 60)
    if(HIKARI_XVFB_RUN AND NOT mode_OFFSCREEN_ONLY)
        add_test(NAME ${name}.xvfb COMMAND "${HIKARI_XVFB_RUN}" -a ${ARGN})
        set_tests_properties(${name}.xvfb PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb" LABELS "${label};xvfb" TIMEOUT 60)
    endif()
endfunction()

# hikari_add_qttest(<target> SOURCES <src>... [LIBRARIES <lib>...] [OFFSCREEN_ONLY]
#                   [PARTS <name> <name>=<first function>... PART_TIMEOUT <s>]) — C++ QtTest.
# OFFSCREEN_ONLY suits rendered-image comparisons: a reference belongs to one
# renderer, and offscreen uses the deterministic software rasterizer.
# PARTS runs a long suite as several processes, one CTest entry per part and
# mode (<target>.<name>.offscreen, ...), each with its own timeout: a part
# runs its functions from its first function up to the next part's
# (tests/support/qtest_parts.h; the source uses HIKARI_QTEST_MAIN_IN_PARTS).
# The first part starts at the first function. <target>.parts checks that
# the parts run every function exactly once.
function(hikari_add_qttest target)
    cmake_parse_arguments(arg "OFFSCREEN_ONLY" "PART_TIMEOUT" "SOURCES;LIBRARIES;PARTS" ${ARGN})
    add_executable(${target} ${arg_SOURCES})
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    target_link_libraries(${target} PRIVATE Qt6::Test ${arg_LIBRARIES})
    set(modes)
    if(arg_OFFSCREEN_ONLY)
        set(modes OFFSCREEN_ONLY)
    endif()
    if(NOT arg_PARTS)
        _hikari_add_qttest_run(${target} ${target} "${modes}")
        return()
    endif()
    # Each part's -part FROM..BEFORE, BEFORE being the next part's first function.
    set(names)
    set(firsts)
    foreach(part IN LISTS arg_PARTS)
        string(FIND "${part}" "=" at)
        if(at LESS 0)
            set(name "${part}")
            set(first "")
        else()
            string(SUBSTRING "${part}" 0 ${at} name)
            math(EXPR at "${at} + 1")
            string(SUBSTRING "${part}" ${at} -1 first)
        endif()
        # Only the first part starts without a function.
        list(LENGTH names count)
        if(count EQUAL 0 AND NOT first STREQUAL "")
            message(FATAL_ERROR "${target} PARTS: the first part starts at the first function (${part})")
        elseif(count GREATER 0 AND first STREQUAL "")
            message(FATAL_ERROR "${target} PARTS: ${part} wants <name>=<first function>")
        endif()
        list(APPEND names "${name}")
        if(count GREATER 0)
            list(APPEND firsts "${first}") # firsts[i - 1] starts part i
        endif()
    endforeach()
    list(LENGTH names count)
    math(EXPR last "${count} - 1")
    set(specs)
    foreach(i RANGE ${last})
        list(GET names ${i} name)
        set(from "")
        set(before "")
        if(i GREATER 0)
            math(EXPR previous "${i} - 1")
            list(GET firsts ${previous} from)
        endif()
        if(i LESS last)
            list(GET firsts ${i} before)
        endif()
        list(APPEND specs "${from}..${before}")
        _hikari_add_qttest_run(${target}.${name} ${target} "${modes}" -part "${from}..${before}")
        foreach(mode offscreen xvfb)
            if(TEST ${target}.${name}.${mode} AND arg_PART_TIMEOUT)
                set_tests_properties(${target}.${name}.${mode} PROPERTIES TIMEOUT ${arg_PART_TIMEOUT})
            endif()
        endforeach()
    endforeach()
    # Commas: a list's semicolons would split the argument.
    string(JOIN "," specs ${specs})
    add_test(NAME ${target}.parts COMMAND "${CMAKE_COMMAND}" -DEXECUTABLE=$<TARGET_FILE:${target}>
        "-DSPECS=${specs}" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/QtTestParts.cmake")
    set_tests_properties(${target}.parts PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "qttest;offscreen" TIMEOUT 60)
endfunction()

# One run of a QtTest executable, as <name>.<mode>, with the extra arguments
# before the output options.
function(_hikari_add_qttest_run name target modes)
    # ctest keeps no QtTest output on Windows: also write it to a file beside
    # the build (CI collects qttest-*.txt).
    set(output)
    if(WIN32)
        set(output -o "${CMAKE_BINARY_DIR}/qttest-${name}.txt,txt" -o "-,txt")
    endif()
    _hikari_add_qt_modes(${name} qttest ${modes} $<TARGET_FILE:${target}> ${ARGN} ${output})
endfunction()

# hikari_add_quicktest(<target> QML_DIR <dir>) — Qt Quick Test over tst_*.qml files.
function(hikari_add_quicktest target)
    cmake_parse_arguments(arg "" "QML_DIR" "" ${ARGN})
    add_executable(${target} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/quicktest_main.cpp")
    target_link_libraries(${target} PRIVATE Qt6::QuickTest Qt6::Quick)
    _hikari_add_qt_modes(${target} quicktest $<TARGET_FILE:${target}> -input "${arg_QML_DIR}")
endfunction()

# Spix ships no version file; ports/spix pins 0.14.
find_package(Spix CONFIG REQUIRED)

# hikari_add_spix_test(<target> SOURCES <src>... [LIBRARIES <lib>...] [DEFINITIONS <def>...])
# In-process UI automation driving real Qt Quick items. Test hooks never ship:
# these targets exist only under BUILD_TESTING.
function(hikari_add_spix_test target)
    cmake_parse_arguments(arg "" "" "SOURCES;LIBRARIES;DEFINITIONS" ${ARGN})
    add_executable(${target} ${arg_SOURCES})
    target_compile_definitions(${target} PRIVATE ${arg_DEFINITIONS}
        HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    target_link_libraries(${target} PRIVATE Spix::SpixQtQuick Qt6::Gui Qt6::Qml Qt6::Quick ${arg_LIBRARIES})
    _hikari_add_qt_modes(${target} ui $<TARGET_FILE:${target}>)
endfunction()

# W2: VSFilterMod, the CSRI renderer the Windows package ships, for the tests
# that need a real CSRI renderer (the windows-x64-vsfiltermod preset turns it
# on): the configure fetches the pinned release (HikariVsfilterMod.cmake) and
# HIKARI_TEST_VSFILTERMOD_DLL is its DLL. Off by default, so the default
# build downloads nothing and has no CSRI renderer.
option(HIKARI_TEST_VSFILTERMOD "Fetch the pinned VSFilterMod for the CSRI tests (Windows)" OFF)
set(HIKARI_TEST_VSFILTERMOD_DLL "")
if(HIKARI_TEST_VSFILTERMOD)
    if(NOT WIN32)
        message(FATAL_ERROR "HIKARI_TEST_VSFILTERMOD: VSFilterMod is a Windows CSRI renderer")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/HikariVsfilterMod.cmake")
    hikari_fetch_vsfiltermod("${CMAKE_BINARY_DIR}/vsfiltermod-downloads" HIKARI_TEST_VSFILTERMOD_DLL)
endif()
