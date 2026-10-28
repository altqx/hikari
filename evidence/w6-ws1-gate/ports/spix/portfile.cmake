# Owned overlay of the vcpkg spix port at the locked baseline. Qt comes from the
# provisioned official installation (HIKARI_QT_PREFIX, passed through by the
# Hikari triplets and part of the package ABI), never from vcpkg.
if(NOT DEFINED ENV{HIKARI_QT_PREFIX} OR NOT EXISTS "$ENV{HIKARI_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "spix needs HIKARI_QT_PREFIX pointing at the provisioned Qt")
endif()
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO faaxm/spix
    REF "v${VERSION}"
    SHA512 5b66ca35e122f933eb73d9f6cc4ea4ad8f49f9dd29a9345b746b41e918634332e45699cd1a335b1a3e960b6c018913beda4ee02fb54803841ea10a57d0288330
    HEAD_REF master
)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSPIX_BUILD_QTQUICK=ON
        -DSPIX_BUILD_QTWIDGETS=OFF
        -DSPIX_BUILD_EXAMPLES=OFF
        -DSPIX_BUILD_TESTS=OFF
        -DSPIX_QT_MAJOR=6
        "-DCMAKE_PREFIX_PATH=$ENV{HIKARI_QT_PREFIX}"
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
        -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
)
vcpkg_cmake_install()
# SpixCoreConfig calls find_dependency(AnyRPC), but the anyrpc port installs no
# CMake package. Ship Spix's own find-module beside the config and use it.
file(INSTALL "${SOURCE_PATH}/cmake/modules/FindAnyRPC.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/SpixCore/cmake")
set(core_config "${CURRENT_PACKAGES_DIR}/share/SpixCore/cmake/SpixCoreConfig.cmake")
file(READ "${core_config}" config_text)
string(REPLACE "find_dependency(AnyRPC)"
    "list(PREPEND CMAKE_MODULE_PATH \"\${CMAKE_CURRENT_LIST_DIR}\")\nfind_dependency(AnyRPC)"
    patched_text "${config_text}")
if(patched_text STREQUAL config_text)
    message(FATAL_ERROR "SpixCoreConfig.cmake no longer calls find_dependency(AnyRPC); review the overlay")
endif()
file(WRITE "${core_config}" "${patched_text}")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
