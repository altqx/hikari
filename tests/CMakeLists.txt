cmake_minimum_required(VERSION 3.21)
project(HikariSubTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# On Windows, the tests of the save code that is Windows' own: the other tests
# are of code that needs POSIX, and of LuaJIT and libcurl as Linux builds them.
if(WIN32)
    add_executable(hikarisub_windows_tests
        check_main.cpp
        WindowsSaveFileTests.cpp
        ../HikariSub/WindowsSaveFile.cpp
    )
    # HIKARISUB_TESTING: the seam that makes a volume's refusals, which the
# application is built without
target_compile_definitions(hikarisub_windows_tests PRIVATE UNICODE _UNICODE NOMINMAX HIKARISUB_TESTING)
    target_link_libraries(hikarisub_windows_tests PRIVATE advapi32)
    enable_testing()
    add_test(NAME hikarisub_windows_tests COMMAND hikarisub_windows_tests)
    return()
endif()

# The application's LuaJIT build, so its tests check what the application links.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/VendoredLuaJIT.cmake)
# DependencyControl's native modules, built as the application builds them.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DependencyControlModules.cmake)
set(HIKARISUB_TEST_DEPCTRL_DIR "${CMAKE_BINARY_DIR}/depctrl")
hikarisub_add_dependencycontrol_modules("${HIKARISUB_TEST_DEPCTRL_DIR}")

# Headless tests for the modules that need neither wxWidgets nor a window.
add_executable(hikarisub_tests
    check_main.cpp
    AssBlendTests.cpp
    DependencyControlModulesTests.cpp
    FrameExchangeTests.cpp
    FrameQueueTests.cpp
    HotkeysFileTests.cpp
    LineGroupsTests.cpp
    LuaJitTests.cpp
    OwnedOrBorrowedTests.cpp
    ParsedScriptsTests.cpp
    PlaybackTests.cpp
    PrivateFolderTests.cpp
    SaveFileTests.cpp
    SaveTargetTests.cpp
    SemVerTests.cpp
    SrtTagsTests.cpp
    SubtitlesProviderChoiceTests.cpp
    TimebaseTests.cpp
    UndoHistoryTests.cpp
    UpdatePolicyTests.cpp
    TextComparisonTests.cpp
    WaveformPeaksTests.cpp
    ../HikariSub/AssBlend.cpp
    ../HikariSub/FrameExchange.cpp
    ../HikariSub/FrameQueue.cpp
    ../HikariSub/LineGroups.cpp
    ../HikariSub/Playback.cpp
    ../HikariSub/SaveFile.cpp
    ../HikariSub/SubtitlesProviderChoice.cpp
    ../HikariSub/PrivateFolder.cpp
    ../HikariSub/TextComparison.cpp
    ../HikariSub/Timebase.cpp
    ../HikariSub/WaveformPeaks.cpp
)

# libcurl as the application links it, so OpenSSL's libcrypto is loaded when
# the modules are.
target_link_libraries(hikarisub_tests PRIVATE hikarisub_luajit PkgConfig::HIKARISUB_DEPCTRL_CURL)
target_compile_definitions(hikarisub_tests PRIVATE
    HIKARISUB_TEST_AUTOMATION_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/../Automation/automation/Include"
    HIKARISUB_TEST_DEPCTRL_DIR="${HIKARISUB_TEST_DEPCTRL_DIR}")
add_dependencies(hikarisub_tests
    hikarisub_depctrl_badmutex hikarisub_depctrl_precisetimer hikarisub_depctrl_downloadmanager)

enable_testing()
add_test(NAME hikarisub_tests COMMAND hikarisub_tests)

# wxGTK's key events, against the pinned wxWidgets build the application uses
# (Thirdparty/build-wxwidgets-linux.sh, which applies Thirdparty/wxWidgets-patches)
# and under xvfb-run for a display. Left out when either is missing, as in the
# CI tests job, which builds no wxWidgets.
set(HIKARISUB_TESTS_WX_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../build-wx-linux/prefix"
    CACHE PATH "Prefix of the pinned wxWidgets 3.3.4 Linux build, for the wxGTK tests")
find_program(HIKARISUB_TESTS_XVFB_RUN xvfb-run)
if(EXISTS "${HIKARISUB_TESTS_WX_PREFIX}/lib/cmake/wxWidgets-3.3" AND HIKARISUB_TESTS_XVFB_RUN)
    find_package(wxWidgets 3.3.4 EXACT CONFIG REQUIRED COMPONENTS core base
        PATHS "${HIKARISUB_TESTS_WX_PREFIX}" NO_DEFAULT_PATH)
    pkg_check_modules(HIKARISUB_TESTS_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    add_executable(hikarisub_wxgtk_tests check_main.cpp WxGtkKeyTests.cpp)
    target_link_libraries(hikarisub_wxgtk_tests PRIVATE wx::core wx::base PkgConfig::HIKARISUB_TESTS_GTK3)
    add_test(NAME hikarisub_wxgtk_tests
        COMMAND "${HIKARISUB_TESTS_XVFB_RUN}" -a $<TARGET_FILE:hikarisub_wxgtk_tests>)
    set_tests_properties(hikarisub_wxgtk_tests PROPERTIES ENVIRONMENT "GDK_BACKEND=x11")
else()
    message(STATUS "hikarisub_wxgtk_tests left out: needs ${HIKARISUB_TESTS_WX_PREFIX} and xvfb-run")
endif()
