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

# Suites that also run under xvfb-run, for window-system behaviour offscreen cannot show.
set(HIKARI_XVFB_SUITES
    # xcb: a first-frame arrangement upset a tab's cursor, and xcb's Redo key let Ctrl+Y skip the draft.
    hikari_ui_shell_tests.documents
    # The Timing panel's sections and a floating panel's focus.
    hikari_ui_shell_tests.panels
    hikari_ui_shell_tests.hotkeys
    # On xcb the fullscreen rail took the hover from the picture.
    hikari_ui_shell_tests.video
    # A restart after Cancel stalled harper-ls under xcb.
    hikari_ui_grammar_tests
    # The progress card's timer fired only under X11.
    hikari_ui_automation_progress_tests
    hikari_ui_automation_workflow
    hikari_ui_video_fullscreen_workflow
    hikari_ui_popup_kit_tests
    hikari_ui_dialog_frame_tests
    hikari_ui_theme_startup_tests # the popups' palette at start
    hikari_ui_window_frame_tests
    hikari_ui_title_bar_tests
    hikari_ui_docking_qualification_tests # floating panels' windows
    hikari_ui_workspace_layout_tests # floating panels' windows
    hikari_ui_layout_migration_tests # floating panels' windows
    hikari_ui_single_instance_tests # raising and activating the running window
    hikari_ui_tabs_workflow # focus and keys through the window
    hikari_ui_shortcuts_workflow # focus and keys through the window
    hikari_ui_colour_picker_tests # the picker's pointer grab
    hikari_ui_automation_submenus_tests # submenus opened by hover
    # The runners' controls: the xvfb runner still reports a failure.
    hikari_qttest_controls hikari_qttest_failing_control
    hikari_quicktest_controls hikari_quicktest_failing_control
    hikari_ui_controls hikari_ui_failing_control
    hikari_a11y_controls hikari_a11y_failing_control)

function(_hikari_check_xvfb_suites)
    get_property(registered GLOBAL PROPERTY HIKARI_QT_MODE_NAMES)
    foreach(name IN LISTS HIKARI_XVFB_SUITES)
        if(NOT name IN_LIST registered)
            message(FATAL_ERROR "HIKARI_XVFB_SUITES names ${name}, which no test registers")
        endif()
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _hikari_check_xvfb_suites)

# 120 s: UI suites take 40-51 s on the shared Windows MSVC runners.
function(_hikari_add_qt_modes name label)
    cmake_parse_arguments(mode "OFFSCREEN_ONLY" "" "" ${ARGN})
    set(ARGN ${mode_UNPARSED_ARGUMENTS})
    set_property(GLOBAL APPEND PROPERTY HIKARI_QT_MODE_NAMES ${name})
    add_test(NAME ${name}.offscreen COMMAND ${ARGN})
    set_tests_properties(${name}.offscreen PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "${label};offscreen" TIMEOUT 120)
    if(HIKARI_XVFB_RUN AND NOT mode_OFFSCREEN_ONLY AND name IN_LIST HIKARI_XVFB_SUITES)
        add_test(NAME ${name}.xvfb COMMAND "${HIKARI_XVFB_RUN}" -a ${ARGN})
        set_tests_properties(${name}.xvfb PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb" LABELS "${label};xvfb" TIMEOUT 120)
    endif()
endfunction()

# hikari_add_qttest(<target> SOURCES <src>... [LIBRARIES <lib>...] [OFFSCREEN_ONLY]
#                   [PARTS <name> <name>=<first function>... PART_TIMEOUT <s>]) — C++ QtTest.
# OFFSCREEN_ONLY for rendered-image comparisons (offscreen is the deterministic software rasterizer).
# PARTS: one process and CTest entry per part, each running from its first function to the next part's
# (tests/support/qtest_parts.h, HIKARI_QTEST_MAIN_IN_PARTS); <target>.parts checks every function runs once.
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

function(_hikari_add_qttest_run name target modes)
    # ctest keeps no QtTest output on Windows, so it also goes to a file (CI collects qttest-*.txt).
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
function(hikari_add_spix_test target)
    cmake_parse_arguments(arg "" "" "SOURCES;LIBRARIES;DEFINITIONS" ${ARGN})
    add_executable(${target} ${arg_SOURCES})
    target_compile_definitions(${target} PRIVATE ${arg_DEFINITIONS}
        HIKARI_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    target_link_libraries(${target} PRIVATE Spix::SpixQtQuick Qt6::Gui Qt6::Qml Qt6::Quick ${arg_LIBRARIES})
    _hikari_add_qt_modes(${target} ui $<TARGET_FILE:${target}>)
endfunction()

option(HIKARI_TEST_VSFILTERMOD "Fetch the pinned VSFilterMod for the CSRI tests (Windows)" OFF)
set(HIKARI_TEST_VSFILTERMOD_DLL "")
if(HIKARI_TEST_VSFILTERMOD)
    if(NOT WIN32)
        message(FATAL_ERROR "HIKARI_TEST_VSFILTERMOD: VSFilterMod is a Windows CSRI renderer")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/HikariVsfilterMod.cmake")
    hikari_fetch_vsfiltermod("${CMAKE_BINARY_DIR}/vsfiltermod-downloads" HIKARI_TEST_VSFILTERMOD_DLL)
endif()
