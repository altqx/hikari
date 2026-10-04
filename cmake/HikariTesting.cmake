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

# hikari_add_qttest(<target> SOURCES <src>... [LIBRARIES <lib>...] [OFFSCREEN_ONLY]) — C++ QtTest.
# OFFSCREEN_ONLY suits rendered-image comparisons: a reference belongs to one
# renderer, and offscreen uses the deterministic software rasterizer.
function(hikari_add_qttest target)
    cmake_parse_arguments(arg "OFFSCREEN_ONLY" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${target} ${arg_SOURCES})
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    target_link_libraries(${target} PRIVATE Qt6::Test ${arg_LIBRARIES})
    # ctest keeps no QtTest output on Windows: also write it to a file beside
    # the build (CI collects qttest-*.txt).
    set(output)
    if(WIN32)
        set(output -o "${CMAKE_BINARY_DIR}/qttest-${target}.txt,txt" -o "-,txt")
    endif()
    if(arg_OFFSCREEN_ONLY)
        _hikari_add_qt_modes(${target} qttest OFFSCREEN_ONLY $<TARGET_FILE:${target}> ${output})
    else()
        _hikari_add_qt_modes(${target} qttest $<TARGET_FILE:${target}> ${output})
    endif()
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
