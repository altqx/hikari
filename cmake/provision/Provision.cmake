# Build provisioning. The root CMakeLists includes this before project():
# every input comes from cmake/locks, is verified, and nothing falls back to a
# system library or another Qt. Idempotent; reconfiguration reuses verified state.
include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.28)

if(CMAKE_HOST_WIN32)
    set(HIKARI_PLATFORM windows)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(HIKARI_PLATFORM linux)
else()
    message(FATAL_ERROR "No provisioning lock for ${CMAKE_HOST_SYSTEM_NAME}; macOS stays possible but is not provisioned")
endif()
cmake_host_system_information(RESULT _hikari_arch QUERY OS_PLATFORM)
if(NOT _hikari_arch MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Provisioning is locked for x64 hosts only (found ${_hikari_arch})")
endif()

# Declared development-tool prerequisites (docs/qt/build.md). Checked up
# front so a missing tool is named here, not deep inside a dependency build.
if(HIKARI_PLATFORM STREQUAL "linux")
    set(_hikari_missing "")
    foreach(tool git ninja cc c++ make pkg-config autoconf automake libtoolize nasm python3 curl tar zip unzip)
        find_program(_hikari_tool_${tool} NAMES ${tool})
        if(NOT _hikari_tool_${tool})
            list(APPEND _hikari_missing ${tool})
        endif()
    endforeach()
    # autoconf-archive has no executable; vcpkg's autotools ports need its macros.
    if(_hikari_tool_automake)
        execute_process(COMMAND aclocal --print-ac-dir OUTPUT_VARIABLE _hikari_acdir
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT EXISTS "${_hikari_acdir}/ax_pthread.m4")
            list(APPEND _hikari_missing autoconf-archive)
        endif()
    endif()
    if(_hikari_missing)
        list(JOIN _hikari_missing " " _hikari_missing)
        message(FATAL_ERROR "Missing declared host tools: ${_hikari_missing}. Install them with the distribution's package manager.")
    endif()
endif()

if(HIKARI_PLATFORM STREQUAL "windows")
    # vcpkg fetches its own build tools; the provisioner itself needs git, and
    # the build needs ninja and cl (the Visual Studio developer environment).
    set(_hikari_missing "")
    foreach(tool git ninja cl)
        find_program(_hikari_tool_${tool} NAMES ${tool})
        if(NOT _hikari_tool_${tool})
            list(APPEND _hikari_missing ${tool})
        endif()
    endforeach()
    if(_hikari_missing)
        list(JOIN _hikari_missing " " _hikari_missing)
        message(FATAL_ERROR "Missing declared host tools: ${_hikari_missing}. Run from a Visual Studio x64 developer environment with Git for Windows on PATH.")
    endif()
endif()

set(HIKARI_LOCK_DIR "${CMAKE_CURRENT_LIST_DIR}/../locks")
get_filename_component(HIKARI_LOCK_DIR "${HIKARI_LOCK_DIR}" ABSOLUTE)
set(HIKARI_SDK_DIR "${CMAKE_SOURCE_DIR}/out/sdk" CACHE PATH
    "Shared, ignored directory for the Qt mirror/installation and vcpkg checkout")
set(HIKARI_EVIDENCE_DIR "${CMAKE_BINARY_DIR}/provision-evidence")
set(HIKARI_QT_ACQUIRE_LIMIT 805306368) # 768 MiB stop bound
option(HIKARI_QT_VERIFY_ONLY "Verify the Qt mirror without acquiring anything" OFF)
option(HIKARI_PROVISION_ONLY "Provision dependencies, then stop before enabling compilers" OFF)
set(HIKARI_QT_REPOSITORY_BASE "" CACHE STRING
    "Serve the Qt mirror from this URL instead of file:// (for example a loopback server with an access log)")
file(MAKE_DIRECTORY "${HIKARI_SDK_DIR}" "${HIKARI_EVIDENCE_DIR}")
set(CMAKE_TLS_VERIFY ON)

include("${CMAKE_CURRENT_LIST_DIR}/Qt.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Vcpkg.cmake")

hikari_qt_acquire(${HIKARI_PLATFORM})
if(NOT HIKARI_QT_VERIFY_ONLY)
    hikari_qt_install(${HIKARI_PLATFORM})
    hikari_provision_vcpkg(${HIKARI_PLATFORM})
    if(HIKARI_PLATFORM STREQUAL "windows")
        set(_hikari_qt_prefix "${HIKARI_QT_ROOT}/6.11.2/msvc2022_64")
        set(_hikari_triplet x64-hikari-windows-release)
    else()
        set(_hikari_qt_prefix "${HIKARI_QT_ROOT}/6.11.2/gcc_64")
        set(_hikari_triplet x64-hikari-linux-release)
    endif()
    set(HIKARI_QT_PREFIX "${_hikari_qt_prefix}" CACHE INTERNAL "Provisioned Qt prefix")
    set(ENV{HIKARI_QT_PREFIX} "${_hikari_qt_prefix}")
    set(CMAKE_TOOLCHAIN_FILE "${HIKARI_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH
        "Pinned vcpkg toolchain" FORCE)
    set(VCPKG_TARGET_TRIPLET "${_hikari_triplet}" CACHE STRING "Hikari-owned vcpkg triplet" FORCE)
    list(PREPEND CMAKE_PREFIX_PATH "${HIKARI_QT_PREFIX}")
    set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
    set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
endif()
