cmake_minimum_required(VERSION 3.21)
project(HikariSubTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The application's LuaJIT build, so its tests check what the application links.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/VendoredLuaJIT.cmake)

# Headless tests for the modules that need neither wxWidgets nor a window.
add_executable(hikarisub_tests
    check_main.cpp
    AssBlendTests.cpp
    FrameQueueTests.cpp
    HotkeysFileTests.cpp
    LineGroupsTests.cpp
    LuaJitTests.cpp
    ParsedScriptsTests.cpp
    PlaybackTests.cpp
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

target_link_libraries(hikarisub_tests PRIVATE hikarisub_luajit)

enable_testing()
add_test(NAME hikarisub_tests COMMAND hikarisub_tests)
