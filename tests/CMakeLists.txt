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
        AutomationFoldersTests.cpp
        update/UpdateApplyTests.cpp
        update/UpdateVerifyTests.cpp
        ../HikariSub/UpdateApply.cpp
        ../HikariSub/WindowsSaveFile.cpp
    )
    # HIKARISUB_TESTING: the seam that makes a volume's refusals, which the
# application is built without
target_compile_definitions(hikarisub_windows_tests PRIVATE UNICODE _UNICODE NOMINMAX HIKARISUB_TESTING)
    target_link_libraries(hikarisub_windows_tests PRIVATE advapi32 bcrypt crypt32 winhttp)
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
    AutomationFoldersTests.cpp
    DependencyControlModulesTests.cpp
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
    TimebaseTests.cpp
    UndoHistoryTests.cpp
    UpdatePolicyTests.cpp
    update/UpdateManifestTests.cpp
    update/UpdateVerifyTests.cpp
    TextComparisonTests.cpp
    WaveformPeaksTests.cpp
    ../HikariSub/AssBlend.cpp
    ../HikariSub/JsonValue.cpp
    ../HikariSub/FrameQueue.cpp
    ../HikariSub/LineGroups.cpp
    ../HikariSub/Playback.cpp
    ../HikariSub/SaveFile.cpp
    ../HikariSub/PrivateFolder.cpp
    ../HikariSub/TextComparison.cpp
    ../HikariSub/Timebase.cpp
    ../HikariSub/WaveformPeaks.cpp
)

# libcurl as the application links it, so OpenSSL's libcrypto is loaded when
# the modules are.
target_link_libraries(hikarisub_tests PRIVATE hikarisub_luajit PkgConfig::HIKARISUB_DEPCTRL_CURL)
# HIKARISUB_JSON_NO_WX: JsonValue without its wxString accessors, for the
# update manifest's tests.
target_compile_definitions(hikarisub_tests PRIVATE
    HIKARISUB_JSON_NO_WX
    HIKARISUB_TEST_AUTOMATION_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/../Automation/automation/Include"
    HIKARISUB_TEST_DEPCTRL_DIR="${HIKARISUB_TEST_DEPCTRL_DIR}")
add_dependencies(hikarisub_tests
    hikarisub_depctrl_badmutex hikarisub_depctrl_precisetimer hikarisub_depctrl_downloadmanager)

enable_testing()
add_test(NAME hikarisub_tests COMMAND hikarisub_tests)
