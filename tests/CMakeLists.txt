cmake_minimum_required(VERSION 3.21)
project(HikariSubTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headless tests for the modules that need neither wxWidgets nor a window.
add_executable(hikarisub_tests
    check_main.cpp
    AssBlendTests.cpp
    FrameQueueTests.cpp
    ParsedScriptsTests.cpp
    PlaybackTests.cpp
    SemVerTests.cpp
    TimebaseTests.cpp
    UndoHistoryTests.cpp
    WaveformPeaksTests.cpp
    ../HikariSub/AssBlend.cpp
    ../HikariSub/FrameQueue.cpp
    ../HikariSub/Playback.cpp
    ../HikariSub/Timebase.cpp
    ../HikariSub/WaveformPeaks.cpp
)

enable_testing()
add_test(NAME hikarisub_tests COMMAND hikarisub_tests)
