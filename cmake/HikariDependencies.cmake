# Resolve the provisioned dependencies. Each has exactly one owner: Qt from
# the frozen official installation, everything else from the pinned vcpkg graph.
include_guard(GLOBAL)

# FFmpeg first: Qt puts its own FindFFmpeg.cmake on CMAKE_MODULE_PATH, and
# on a case-insensitive filesystem (Windows) find_package(FFMPEG) would load
# it instead of vcpkg's FindFFMPEG.
find_package(HikariFFMS2 5.1.0 EXACT CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Gui Qml Quick Multimedia Network)
cmake_path(IS_PREFIX HIKARI_QT_PREFIX "${Qt6_DIR}" NORMALIZE _hikari_qt_owned)
if(NOT _hikari_qt_owned)
    message(FATAL_ERROR "Qt6_DIR ${Qt6_DIR} is not the provisioned Qt at ${HIKARI_QT_PREFIX}")
endif()

# libass ships pkg-config metadata only. Static linking needs its full closure.
if(WIN32 AND NOT PKG_CONFIG_EXECUTABLE)
    find_program(PKG_CONFIG_EXECUTABLE pkgconf
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/pkgconf" NO_DEFAULT_PATH REQUIRED)
endif()
find_package(PkgConfig REQUIRED)
set(PKG_CONFIG_ARGN --static)
pkg_check_modules(HIKARI_LIBASS REQUIRED IMPORTED_TARGET GLOBAL libass=0.17.5)
if(NOT TARGET Hikari::libass)
    add_library(Hikari::libass ALIAS PkgConfig::HIKARI_LIBASS)
endif()

# The media stack links statically. Qt loads the system FreeType, HarfBuzz and
# fontconfig, so our static copies must not be exported from the executable or
# Qt's font code would bind to them instead.
add_library(hikari_media_deps INTERFACE)
target_include_directories(hikari_media_deps INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(hikari_media_deps INTERFACE HikariFFMS2::ffms2 ${FFMPEG_LIBRARIES} Hikari::libass)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(hikari_media_deps INTERFACE "LINKER:--exclude-libs,ALL")
endif()
add_library(Hikari::media_deps ALIAS hikari_media_deps)

# Editor audio output (ADR 0010): the owned PortAudio overlay with pinned host
# APIs. Static; ALSA is the system library on Linux.
find_package(portaudio CONFIG REQUIRED)
add_library(hikari_portaudio INTERFACE)
target_link_libraries(hikari_portaudio INTERFACE portaudio_static)
add_library(Hikari::portaudio ALIAS hikari_portaudio)

# LuaJIT for the isolated Lua helper only (ADR 0006). Its flags export the
# executable's symbols (-Wl,-E), which Lua native modules need, so it must not
# be linked into the application.
pkg_check_modules(HIKARI_LUAJIT REQUIRED IMPORTED_TARGET GLOBAL luajit)
if(NOT TARGET Hikari::luajit)
    add_library(Hikari::luajit ALIAS PkgConfig::HIKARI_LUAJIT)
endif()

# Where each dependency actually resolved from, for build evidence.
file(WRITE "${CMAKE_BINARY_DIR}/provision-evidence/dependency-origins.txt"
    "Qt6_DIR=${Qt6_DIR}\nQt6_VERSION=${Qt6_VERSION}\n"
    "HikariFFMS2_DIR=${HikariFFMS2_DIR}\n"
    "FFMPEG_LIBRARIES=${FFMPEG_LIBRARIES}\n"
    "libass=${HIKARI_LIBASS_VERSION} ${HIKARI_LIBASS_LINK_LIBRARIES}\n"
    "portaudio_DIR=${portaudio_DIR} ${portaudio_VERSION}\n"
    "luajit=${HIKARI_LUAJIT_VERSION} ${HIKARI_LUAJIT_LINK_LIBRARIES}\n"
    "toolchain=${CMAKE_TOOLCHAIN_FILE}\ntriplet=${VCPKG_TARGET_TRIPLET}\n"
    "compiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n")

# D1: the docking engine (ports/kddockwidgets: QtQuick frontend, built against the provisioned Qt).
find_package(KDDockWidgets-qt6 CONFIG REQUIRED)
