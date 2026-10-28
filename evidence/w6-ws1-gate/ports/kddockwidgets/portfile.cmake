# Owned overlay of the vcpkg kddockwidgets port (docs/qt/docking.md, ADR 0017):
# release v2.4.1 at commit c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8, the QtQuick
# frontend only, built against the provisioned official Qt (HIKARI_QT_PREFIX,
# passed through by the Hikari triplets and part of the package ABI). Used under
# its GPL-3.0 option with the repository's GPLv3 distribution.
if(NOT DEFINED ENV{HIKARI_QT_PREFIX} OR NOT EXISTS "$ENV{HIKARI_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "kddockwidgets needs HIKARI_QT_PREFIX pointing at the provisioned Qt")
endif()
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KDAB/KDDockWidgets
    REF c1d28d25ef5ba077915bcb2b6fa9e14df2a361f8 # v2.4.1
    SHA512 66a3b5c041ac3b1366eba89ae3f8eac2d75ac4e15c84e5de688312db39ba836e86dbee298764f55662bdb18c2f883bb58fae85056ec20c5757e0125c7f88ae2a
    HEAD_REF main
)
# The bundled third-party copies (kdbindings, nlohmann) come from vcpkg instead.
file(REMOVE_RECURSE "${SOURCE_PATH}/src/3rdparty")
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" KD_STATIC)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DKDDockWidgets_QT6=ON
        -DKDDockWidgets_FRONTENDS=qtquick
        -DKDDockWidgets_STATIC=${KD_STATIC}
        -DKDDockWidgets_PYTHON_BINDINGS=OFF
        -DKDDockWidgets_TESTS=OFF
        -DKDDockWidgets_EXAMPLES=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_spdlog=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON
        -DCMAKE_REQUIRE_FIND_PACKAGE_nlohmann_json=ON
        "-DCMAKE_PREFIX_PATH=$ENV{HIKARI_QT_PREFIX}"
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
        -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/KDDockWidgets-qt6" PACKAGE_NAME kddockwidgets-qt6)
# A static build links Qt's private modules, KDBindings and nlohmann_json, but
# the upstream package config finds none of them for its consumers.
set(kddw_config "${CURRENT_PACKAGES_DIR}/share/kddockwidgets-qt6/KDDockWidgets-qt6Config.cmake")
file(READ "${kddw_config}" config_text)
string(REPLACE "include(\"\${CMAKE_CURRENT_LIST_DIR}/KDDockWidgets-qt6Targets.cmake\")"
    "find_dependency(Qt6 COMPONENTS GuiPrivate QuickPrivate)\nfind_dependency(KDBindings)\nfind_dependency(nlohmann_json)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/KDDockWidgets-qt6Targets.cmake\")"
    patched_text "${config_text}")
if(patched_text STREQUAL config_text)
    message(FATAL_ERROR "KDDockWidgets-qt6Config.cmake no longer includes its targets file as expected; review the overlay")
endif()
file(WRITE "${kddw_config}" "${patched_text}")
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
file(GLOB license_files "${SOURCE_PATH}/LICENSES/*")
vcpkg_install_copyright(FILE_LIST ${license_files})
