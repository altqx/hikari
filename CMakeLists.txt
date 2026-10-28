cmake_minimum_required(VERSION 3.28)
# Provisioning runs before project(): it verifies the locked Qt mirror and
# installation and the pinned vcpkg checkout/tool, then sets the toolchain.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/provision/Provision.cmake")
if(HIKARI_PROVISION_ONLY OR HIKARI_QT_VERIFY_ONLY)
    project(HikariSubProvision LANGUAGES NONE)
    return()
endif()

project(HikariSub VERSION 0.1.0 LANGUAGES C CXX)
# ADR 0019: the version the program reports and compares, and the release tag
# without its "v": project(VERSION) and, for a prerelease, "-" and the suffix.
set(HIKARI_VERSION_SUFFIX "" CACHE STRING "SemVer prerelease of the version, for example beta.2; empty for a release")
if(HIKARI_VERSION_SUFFIX STREQUAL "")
    set(HIKARI_VERSION "${PROJECT_VERSION}")
elseif(HIKARI_VERSION_SUFFIX MATCHES "^[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*$")
    set(HIKARI_VERSION "${PROJECT_VERSION}-${HIKARI_VERSION_SUFFIX}")
else()
    message(FATAL_ERROR "HIKARI_VERSION_SUFFIX \"${HIKARI_VERSION_SUFFIX}\" is not a SemVer prerelease")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
# Sources are UTF-8; MSVC otherwise reads them in the system code page.
add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>" "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/utf-8>")
# #320: the Linux CI job bounds-checks libstdc++ (operator[], front()/back(),
# span, string_view); an out-of-bounds read aborts with its file and line.
# Our targets only, not the vcpkg ports; presets and packages leave it off.
option(HIKARI_GLIBCXX_ASSERTIONS "Build with -D_GLIBCXX_ASSERTIONS (CI)" OFF)
if(HIKARI_GLIBCXX_ASSERTIONS)
    add_compile_definitions("$<$<COMPILE_LANGUAGE:CXX>:_GLIBCXX_ASSERTIONS>")
endif()
# CR1 (#452): the package workflows build with debug info, which they split
# off (Linux .debug files, Windows PDBs) and keep as the release's symbol
# archives; the packages carry none (docs/qt/crash-reports.md).
option(HIKARI_DEBUG_SYMBOLS "Build with debug info kept apart from the packages (package CI)" OFF)
if(HIKARI_DEBUG_SYMBOLS)
    if(MSVC)
        add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Z7>" "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/Z7>")
        add_link_options("$<$<LINK_LANG_AND_ID:CXX,MSVC>:/DEBUG:FULL>" "$<$<LINK_LANG_AND_ID:CXX,MSVC>:/OPT:REF>"
            "$<$<LINK_LANG_AND_ID:CXX,MSVC>:/OPT:ICF>")
    else()
        add_compile_options(-g)
    endif()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cmake/HikariDependencies.cmake")
# GR1 (#364): the pinned harper-ls (HikariHarper.cmake), which every package
# ships beside the app; the build tree runs the downloaded copy.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/HikariHarper.cmake")
set(HIKARI_HARPER_DOWNLOADS "${CMAKE_BINARY_DIR}/harper-downloads" CACHE PATH
    "Folder the pinned harper-ls is fetched into (seed it to configure offline)")
hikari_fetch_harper("${HIKARI_HARPER_DOWNLOADS}" HIKARI_HARPER_LS)
# DK1 (#473): the pinned spell-check dictionaries every package bundles
# (HikariDictionaries.cmake), with their notices; the tests check them too.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/HikariDictionaries.cmake")
set(HIKARI_DICTIONARY_DOWNLOADS "${CMAKE_BINARY_DIR}/dictionary-downloads" CACHE PATH
    "Folder the pinned dictionaries are fetched into (seed it to configure offline)")
hikari_fetch_dictionaries("${HIKARI_DICTIONARY_DOWNLOADS}" HIKARI_BUNDLED_DICTIONARIES)
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS QuickControls2)
qt_standard_project_setup(REQUIRES 6.11)
add_subdirectory(tools/i18n) # O4: the PO-to-TS migration tool
add_subdirectory(i18n) # O4: TS catalogs, QM in the build
# installers: the app's QML is compiled into a static module under src/ui, so
# the deploy scan for its Qt Quick imports must read that folder too.
set(QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
add_subdirectory(src)
unset(QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/HikariInstall.cmake") # installers: the app tree and the packages
include(i18n/Extraction.cmake) # O4: lupdate and the migration, on request
add_subdirectory(tests/dependency-load)
if(BUILD_TESTING)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/HikariTesting.cmake")
    add_subdirectory(tests/runners/gtest)
    add_subdirectory(tests/runners/qt)
    add_subdirectory(tests/runners/ui)
    add_subdirectory(tests/support/a11y)
    add_subdirectory(tests/runners/a11y)
    add_subdirectory(tests/support/image)
    add_subdirectory(tests/support/media)
    add_subdirectory(tests/support/fonts)
    add_subdirectory(tests/runners/image)
    add_subdirectory(tests/support/perf)
    add_subdirectory(tests/support/desktop) # 38e: no test reaches the desktop's programs
    add_subdirectory(tests/perf)
    add_subdirectory(tests/modules)
    add_subdirectory(tests/core)
    add_subdirectory(tests/application)
    add_subdirectory(tests/backends)
    add_subdirectory(tests/ui)
    add_subdirectory(tests/i18n)
    add_subdirectory(tests/tools) # evidence harness scripts
    add_subdirectory(tools/human/h2-listening) # H2 listening driver, built on request
    add_subdirectory(tools/native-gate) # D1/D3 native gate: gatelib's tests
endif()
