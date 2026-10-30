# Qt comes from the frozen official installation, everything else from the pinned vcpkg graph.
include_guard(GLOBAL)

# FFmpeg before Qt: on a case-insensitive filesystem find_package(FFMPEG) would load Qt's FindFFmpeg.cmake.
find_package(HikariFFMS2 5.1.0 EXACT CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS Core Gui Qml Quick Multimedia Network Svg)
# Linux "show in folder" uses org.freedesktop.FileManager1 over D-Bus; Qt's platform plugins load QtDBus anyway.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 ${HIKARI_QT_VERSION} EXACT REQUIRED COMPONENTS DBus)
endif()
cmake_path(IS_PREFIX HIKARI_QT_PREFIX "${Qt6_DIR}" NORMALIZE _hikari_qt_owned)
if(NOT _hikari_qt_owned)
    message(FATAL_ERROR "Qt6_DIR ${Qt6_DIR} is not the provisioned Qt at ${HIKARI_QT_PREFIX}")
endif()

# libass ships pkg-config metadata only; static linking needs its full closure.
if(WIN32 AND NOT PKG_CONFIG_EXECUTABLE)
    find_program(PKG_CONFIG_EXECUTABLE pkgconf
        PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/pkgconf" NO_DEFAULT_PATH REQUIRED)
endif()
find_package(PkgConfig REQUIRED)
set(PKG_CONFIG_ARGN --static)
# The altqx/libass fork reports upstream's pkg-config version, so the prefix and the fork's feature macros prove it.
pkg_check_modules(HIKARI_LIBASS REQUIRED IMPORTED_TARGET GLOBAL libass>=0.17.5)
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    file(TO_CMAKE_PATH "${VCPKG_INSTALLED_DIR}" _hikari_vcpkg_installed)
    file(TO_CMAKE_PATH "${HIKARI_LIBASS_PREFIX}" _hikari_libass_prefix)
    if(WIN32)
        string(TOLOWER "${_hikari_vcpkg_installed}" _hikari_vcpkg_installed)
        string(TOLOWER "${_hikari_libass_prefix}" _hikari_libass_prefix)
    endif()
    cmake_path(IS_PREFIX _hikari_vcpkg_installed "${_hikari_libass_prefix}" NORMALIZE _hikari_libass_owned)
    if(NOT _hikari_libass_owned)
        message(FATAL_ERROR "libass resolved from ${HIKARI_LIBASS_PREFIX}, not the vcpkg fork in ${VCPKG_INSTALLED_DIR}")
    endif()
endif()
include(CheckSymbolExists)
set(CMAKE_REQUIRED_INCLUDES ${HIKARI_LIBASS_INCLUDE_DIRS})
foreach(_hikari_libass_macro LIBASS_HAS_TRACK_EVENTS_MODIFIED LIBASS_HAS_FRAME_ATLAS LIBASS_HAS_FONT_DIAGNOSTICS)
    check_symbol_exists(${_hikari_libass_macro} "stdarg.h;ass/ass.h" HIKARI_${_hikari_libass_macro})
    if(NOT HIKARI_${_hikari_libass_macro})
        message(FATAL_ERROR "libass at ${HIKARI_LIBASS_PREFIX} is not the altqx fork: ass.h lacks ${_hikari_libass_macro}")
    endif()
endforeach()
unset(CMAKE_REQUIRED_INCLUDES)
if(NOT TARGET Hikari::libass)
    add_library(Hikari::libass ALIAS PkgConfig::HIKARI_LIBASS)
endif()

# Qt loads the system FreeType, HarfBuzz and fontconfig, so our static copies must not be exported from the executable.
add_library(hikari_media_deps INTERFACE)
target_include_directories(hikari_media_deps INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(hikari_media_deps INTERFACE HikariFFMS2::ffms2 ${FFMPEG_LIBRARIES} Hikari::libass)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(hikari_media_deps INTERFACE "LINKER:--exclude-libs,ALL")
endif()
add_library(Hikari::media_deps ALIAS hikari_media_deps)

# ALSA is the system library on Linux.
find_package(portaudio CONFIG REQUIRED)
find_package(PCRE2 CONFIG REQUIRED COMPONENTS 16BIT)
find_package(uchardet CONFIG REQUIRED)
if(NOT TARGET Hikari::uchardet)
    add_library(Hikari::uchardet ALIAS uchardet::libuchardet)
endif()
# The hunspell port ships pkg-config metadata only.
pkg_check_modules(HIKARI_HUNSPELL REQUIRED IMPORTED_TARGET GLOBAL hunspell=1.7.3)
if(NOT TARGET Hikari::hunspell)
    add_library(Hikari::hunspell ALIAS PkgConfig::HIKARI_HUNSPELL)
endif()
find_package(Boost CONFIG REQUIRED COMPONENTS locale)
find_package(ICU REQUIRED COMPONENTS uc i18n data)
add_library(hikari_portaudio INTERFACE)
target_link_libraries(hikari_portaudio INTERFACE portaudio_static)
add_library(Hikari::portaudio ALIAS hikari_portaudio)
find_package(SoundTouch CONFIG REQUIRED)
add_library(hikari_soundtouch INTERFACE)
target_link_libraries(hikari_soundtouch INTERFACE SoundTouch::SoundTouch)
add_library(Hikari::soundtouch ALIAS hikari_soundtouch)
find_package(mecab CONFIG REQUIRED)
add_library(hikari_mecab INTERFACE)
target_link_libraries(hikari_mecab INTERFACE mecab::mecab)
add_library(Hikari::mecab ALIAS hikari_mecab)
find_path(HIKARI_UNIDIC_DIR sys.dic PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/hikari-unidic-lite/dicdir"
    NO_DEFAULT_PATH NO_CACHE REQUIRED)

find_package(tomlplusplus CONFIG REQUIRED)
add_library(hikari_tomlplusplus INTERFACE)
target_link_libraries(hikari_tomlplusplus INTERFACE tomlplusplus::tomlplusplus)
add_library(Hikari::tomlplusplus ALIAS hikari_tomlplusplus)

# LuaJIT's flags export the executable's symbols (-Wl,-E) for Lua native modules: link it into the Lua helper only.
pkg_check_modules(HIKARI_LUAJIT REQUIRED IMPORTED_TARGET GLOBAL luajit)
if(NOT TARGET Hikari::luajit)
    add_library(Hikari::luajit ALIAS PkgConfig::HIKARI_LUAJIT)
endif()

file(WRITE "${CMAKE_BINARY_DIR}/provision-evidence/dependency-origins.txt"
    "Qt6_DIR=${Qt6_DIR}\nQt6_VERSION=${Qt6_VERSION}\n"
    "HikariFFMS2_DIR=${HikariFFMS2_DIR}\n"
    "FFMPEG_LIBRARIES=${FFMPEG_LIBRARIES}\n"
    "libass=${HIKARI_LIBASS_VERSION} ${HIKARI_LIBASS_LINK_LIBRARIES}\n"
    "portaudio_DIR=${portaudio_DIR} ${portaudio_VERSION}\n"
    "SoundTouch_DIR=${SoundTouch_DIR} ${SoundTouch_VERSION}\n"
    "mecab_DIR=${mecab_DIR} ${mecab_VERSION}\nHIKARI_UNIDIC_DIR=${HIKARI_UNIDIC_DIR}\n"
    "tomlplusplus_DIR=${tomlplusplus_DIR} ${tomlplusplus_VERSION}\n"
    "luajit=${HIKARI_LUAJIT_VERSION} ${HIKARI_LUAJIT_LINK_LIBRARIES}\n"
    "hunspell=${HIKARI_HUNSPELL_VERSION} ${HIKARI_HUNSPELL_LINK_LIBRARIES}\n"
    "toolchain=${CMAKE_TOOLCHAIN_FILE}\ntriplet=${VCPKG_TARGET_TRIPLET}\n"
    "compiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n")

find_package(KDDockWidgets-qt6 CONFIG REQUIRED)
