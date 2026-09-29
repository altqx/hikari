# Resolve the provisioned dependencies. Each has exactly one owner: Qt from
# the frozen official installation, everything else from the pinned vcpkg graph.
include_guard(GLOBAL)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Gui Qml Quick)
cmake_path(IS_PREFIX HIKARI_QT_PREFIX "${Qt6_DIR}" NORMALIZE _hikari_qt_owned)
if(NOT _hikari_qt_owned)
    message(FATAL_ERROR "Qt6_DIR ${Qt6_DIR} is not the provisioned Qt at ${HIKARI_QT_PREFIX}")
endif()

find_package(HikariFFMS2 5.1.0 EXACT CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

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
