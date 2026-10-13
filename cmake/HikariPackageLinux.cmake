# Linux release packages (installers spec 5.3 to 5.5): the tar.gz in the
# layout the AUR PKGBUILD relies on, the AppImage, and the deb and rpm.
#
#   cmake --preset linux-floor-x64-release      (HIKARI_PACKAGE_LINUX=ON)
#   cmake --build --preset linux-floor-x64-release --target hikari_linux_packages
#
# The packages land in <build>/dist. packaging/linux/make-packages.sh does
# the work from the install rules of HikariInstall.cmake; run it on the
# build floor (packaging/linux/floor/almalinux9.sh), whose glibc 2.34 is the
# packages' min_glibc.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/DesktopIntegration.cmake")

# The release version (spec 1.2): the project version, with the prerelease
# suffix when HIKARI_VERSION_SUFFIX is set.
set(HIKARI_PACKAGE_VERSION "${PROJECT_VERSION}")
if(DEFINED HIKARI_VERSION_SUFFIX AND NOT HIKARI_VERSION_SUFFIX STREQUAL "")
    string(APPEND HIKARI_PACKAGE_VERSION "-${HIKARI_VERSION_SUFFIX}")
endif()
hikari_generate_desktop_files("${HIKARI_PACKAGE_VERSION}")

set(HIKARI_PACKAGE_FORMATS "tar,appimage,native" CACHE STRING
    "Comma list of the Linux packages hikari_linux_packages builds: tar, appimage, native (deb and rpm)")
set(HIKARI_PACKAGE_TOOLS_DIR "${CMAKE_BINARY_DIR}/packaging-tools" CACHE PATH
    "Where packaging/linux/fetch-tools.sh puts the pinned packaging tools")
option(HIKARI_PACKAGE_ALLOW_MISSING_UPDATE_HELPER
    "Package a placeholder when packaging/linux/update-helper is missing (development only)" OFF)
set(_hikari_package_extra "")
if(HIKARI_PACKAGE_ALLOW_MISSING_UPDATE_HELPER)
    list(APPEND _hikari_package_extra --allow-missing-update-helper)
endif()

add_custom_target(hikari_linux_packages
    COMMAND "${PROJECT_SOURCE_DIR}/packaging/linux/make-packages.sh"
        --build-dir "${CMAKE_BINARY_DIR}" --version "${HIKARI_PACKAGE_VERSION}"
        --out "${CMAKE_BINARY_DIR}/dist" --tools "${HIKARI_PACKAGE_TOOLS_DIR}"
        --formats "${HIKARI_PACKAGE_FORMATS}" ${_hikari_package_extra}
    DEPENDS hikarisub hikari_media_helper hikari_lua_helper hikari_automation_stage
    USES_TERMINAL VERBATIM
    COMMENT "Building the Linux packages of HikariSub ${HIKARI_PACKAGE_VERSION}")
