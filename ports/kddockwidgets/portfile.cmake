# Owned overlay of the vcpkg kddockwidgets port (docs/docking.md, ADR 0017):
# release v2.5.1 at commit 674f28453219dddedafd4b8f33cf094e8e546be3, the QtQuick
# frontend only, built against the provisioned official Qt (HIKARI_QT_PREFIX,
# passed through by the Hikari triplets and part of the package ABI). Used under
# its GPL-3.0 option with the repository's GPLv3 distribution.
if(NOT DEFINED ENV{HIKARI_QT_PREFIX} OR NOT EXISTS "$ENV{HIKARI_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "kddockwidgets needs HIKARI_QT_PREFIX pointing at the provisioned Qt")
endif()
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KDAB/KDDockWidgets
    REF 674f28453219dddedafd4b8f33cf094e8e546be3 # v2.5.1
    SHA512 d332afb5b23072457df58c2df91982df5c6dbefb51d9d0766993d2607425f949eff66f3a6885063eae9a4397da07353fff18296387add43f44f508a48fdb9fa5
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
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
file(GLOB license_files "${SOURCE_PATH}/LICENSES/*")
vcpkg_install_copyright(FILE_LIST ${license_files})
