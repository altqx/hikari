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
        CsriAttachmentsTests.cpp
        NaturalOrderTests.cpp
        ScriptAttachmentsTests.cpp
        update/UpdateApplyTests.cpp
        update/UpdateVerifyTests.cpp
        ../HikariSub/NaturalOrder.cpp
        ../HikariSub/ScriptAttachments.cpp
        ../HikariSub/UpdateApply.cpp
        ../HikariSub/WindowsSaveFile.cpp
    )
    # HIKARISUB_TESTING: the seam that makes a volume's refusals, which the
# application is built without
target_compile_definitions(hikarisub_windows_tests PRIVATE UNICODE _UNICODE NOMINMAX HIKARISUB_TESTING
        HIKARISUB_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    target_link_libraries(hikarisub_windows_tests PRIVATE advapi32 bcrypt crypt32 gdi32 winhttp)
    enable_testing()
    add_test(NAME hikarisub_windows_tests COMMAND hikarisub_windows_tests)

    # The xy-VSFilter CSRI renderer the solution builds (x64\Release\csri\xy-Vsfilter.dll),
    # which only the Windows build job has: pass its path to test it.
    set(HIKARISUB_TESTS_XYVSFILTER "" CACHE FILEPATH "xy-Vsfilter.dll from the solution's build, for its CSRI tests")
    if(HIKARISUB_TESTS_XYVSFILTER)
        add_executable(hikarisub_csri_tests check_main.cpp CsriXyVSFilterTests.cpp)
        target_compile_definitions(hikarisub_csri_tests PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(hikarisub_csri_tests PRIVATE psapi)
        add_test(NAME hikarisub_csri_tests COMMAND hikarisub_csri_tests)
        # backslashes: LoadLibraryEx's altered search path takes no forward slashes
        file(TO_NATIVE_PATH "${HIKARISUB_TESTS_XYVSFILTER}" xyvsfilter_dll)
        set_tests_properties(hikarisub_csri_tests PROPERTIES
            ENVIRONMENT "HIKARISUB_TEST_XYVSFILTER=${xyvsfilter_dll}")
    endif()
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
    CsriOverlayTests.cpp
    DependencyControlModulesTests.cpp
    FrameExchangeTests.cpp
    FrameQueueTests.cpp
    HotkeysFileTests.cpp
    LineGroupsTests.cpp
    LuaJitTests.cpp
    MenuBarHitTestTests.cpp
    MicroDvdStepsTests.cpp
    MoveTimesTests.cpp
    NaturalOrderTests.cpp
    NumberStepTests.cpp
    OwnedOrBorrowedTests.cpp
    ParsedScriptsTests.cpp
    PlaybackTests.cpp
    PrivateFolderTests.cpp
    SaveFileTests.cpp
    SaveTargetTests.cpp
    ScriptAttachmentsTests.cpp
    SemVerTests.cpp
    SrtTagsTests.cpp
    SubtitlesProviderChoiceTests.cpp
    TimebaseTests.cpp
    TimeCtrlStepsTests.cpp
    UndoHistoryTests.cpp
    UpdatePolicyTests.cpp
    VideoFrameAreaTests.cpp
    update/UpdateManifestTests.cpp
    update/UpdateVerifyTests.cpp
    TextComparisonTests.cpp
    WaveformPeaksTests.cpp
    ../HikariSub/AssBlend.cpp
    ../HikariSub/CsriOverlay.cpp
    ../HikariSub/FrameExchange.cpp
    ../HikariSub/JsonValue.cpp
    ../HikariSub/FrameQueue.cpp
    ../HikariSub/LineGroups.cpp
    ../HikariSub/NaturalOrder.cpp
    ../HikariSub/Playback.cpp
    ../HikariSub/SaveFile.cpp
    ../HikariSub/ScriptAttachments.cpp
    ../HikariSub/SubtitlesProviderChoice.cpp
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
    HIKARISUB_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    HIKARISUB_TEST_AUTOMATION_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/../Automation/automation/Include"
    HIKARISUB_TEST_DEPCTRL_DIR="${HIKARISUB_TEST_DEPCTRL_DIR}")
add_dependencies(hikarisub_tests
    hikarisub_depctrl_badmutex hikarisub_depctrl_precisetimer hikarisub_depctrl_downloadmanager)

enable_testing()
add_test(NAME hikarisub_tests COMMAND hikarisub_tests)

# Every string the sources translate, in Locale/template.pot and each catalog:
# one missing from them shows in English in every language.
find_package(Python3 COMPONENTS Interpreter)
find_program(HIKARISUB_TESTS_XGETTEXT xgettext)
find_program(HIKARISUB_TESTS_MSGCMP msgcmp)
if(Python3_Interpreter_FOUND AND HIKARISUB_TESTS_XGETTEXT AND HIKARISUB_TESTS_MSGCMP)
    add_test(NAME hikarisub_catalogs
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_catalogs.py")
else()
    message(STATUS "hikarisub_catalogs left out: needs Python 3, xgettext and msgcmp")
endif()

# wxGTK's key events and the colour pickers' eyedropper pointer, against the
# pinned wxWidgets build the application uses (Thirdparty/build-wxwidgets-linux.sh,
# which applies Thirdparty/wxWidgets-patches) and under xvfb-run for a display. Left out when either is missing, as in the
# CI tests job, which builds no wxWidgets.
set(HIKARISUB_TESTS_WX_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../build-wx-linux/prefix"
    CACHE PATH "Prefix of the pinned wxWidgets 3.3.4 Linux build, for the wxGTK tests")
find_program(HIKARISUB_TESTS_XVFB_RUN xvfb-run)
if(EXISTS "${HIKARISUB_TESTS_WX_PREFIX}/lib/cmake/wxWidgets-3.3" AND HIKARISUB_TESTS_XVFB_RUN)
    find_package(wxWidgets 3.3.4 EXACT CONFIG REQUIRED COMPONENTS core base
        PATHS "${HIKARISUB_TESTS_WX_PREFIX}" NO_DEFAULT_PATH)
    pkg_check_modules(HIKARISUB_TESTS_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(HIKARISUB_TESTS_X11 REQUIRED IMPORTED_TARGET x11 xfixes)
    add_executable(hikarisub_wxgtk_tests check_main.cpp WxGtkKeyTests.cpp DropperPointerTests.cpp
        ../HikariSub/DropperPointer.cpp)
    target_compile_definitions(hikarisub_wxgtk_tests PRIVATE
        HIKARISUB_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(hikarisub_wxgtk_tests PRIVATE wx::core wx::base PkgConfig::HIKARISUB_TESTS_GTK3
        PkgConfig::HIKARISUB_TESTS_X11)
    add_test(NAME hikarisub_wxgtk_tests
        COMMAND "${HIKARISUB_TESTS_XVFB_RUN}" -a $<TARGET_FILE:hikarisub_wxgtk_tests>)
    set_tests_properties(hikarisub_wxgtk_tests PROPERTIES ENVIRONMENT "GDK_BACKEND=x11")
else()
    message(STATUS "hikarisub_wxgtk_tests left out: needs ${HIKARISUB_TESTS_WX_PREFIX} and xvfb-run")
endif()

# Subtitles read from Matroska through the FFMS2 extension the application
# compiles (Thirdparty/Build/FFMS2/indexing_additional.cpp), and a script's
# attached font drawn by libass, against FFMS2, FFmpeg and libass from
# pkg-config as the application links them, and the Thirdparty/ffms2 fork's
# private headers. Left out when any is missing, as in the CI tests job.
# The application links the libass fork Thirdparty/build-libass-linux.sh
# builds; so do these tests when it is there.
set(HIKARISUB_TESTS_LIBASS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../build-libass-linux/prefix" CACHE PATH
    "Prefix holding the libass fork (lib/pkgconfig/libass.pc)")
if(EXISTS "${HIKARISUB_TESTS_LIBASS_PREFIX}/lib/pkgconfig/libass.pc")
    set(ENV{PKG_CONFIG_PATH} "${HIKARISUB_TESTS_LIBASS_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()
pkg_check_modules(HIKARISUB_TESTS_MEDIA QUIET IMPORTED_TARGET ffms2 libavformat libavcodec libavutil)
# On its own, so the -L of another module (/usr/lib) cannot pick the
# distribution's libass over the fork.
pkg_check_modules(HIKARISUB_TESTS_LIBASS QUIET IMPORTED_TARGET libass)
if(HIKARISUB_TESTS_MEDIA_FOUND AND HIKARISUB_TESTS_LIBASS_FOUND
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Thirdparty/ffms2/src/core/indexing.h")
    add_executable(hikarisub_media_tests
        check_main.cpp
        LibassAttachedFontTests.cpp
        MatroskaSubtitlesTests.cpp
        LibassForkTests.cpp
        SubtitleTimeBaseTests.cpp
        ../HikariSub/ScriptAttachments.cpp
        ../Thirdparty/Build/FFMS2/indexing_additional.cpp)
    target_include_directories(hikarisub_media_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Thirdparty/ffms2/src)
    target_compile_definitions(hikarisub_media_tests PRIVATE
        HIKARISUB_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    target_link_libraries(hikarisub_media_tests PRIVATE PkgConfig::HIKARISUB_TESTS_LIBASS PkgConfig::HIKARISUB_TESTS_MEDIA)
    add_test(NAME hikarisub_media_tests COMMAND hikarisub_media_tests)
else()
    message(STATUS "hikarisub_media_tests left out: needs ffms2, FFmpeg and libass (pkg-config) and Thirdparty/ffms2")
endif()
