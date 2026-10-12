cmake_minimum_required(VERSION 3.21)
project(HikariSubTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    FrameQueueTests.cpp
    HotkeysFileTests.cpp
    LineGroupsTests.cpp
    LuaJitTests.cpp
    ParsedScriptsTests.cpp
    PlaybackTests.cpp
    SaveTargetTests.cpp
    SemVerTests.cpp
    TimebaseTests.cpp
    UndoHistoryTests.cpp
    TextComparisonTests.cpp
    WaveformPeaksTests.cpp
    ../HikariSub/AssBlend.cpp
    ../HikariSub/FrameQueue.cpp
    ../HikariSub/LineGroups.cpp
    ../HikariSub/Playback.cpp
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
