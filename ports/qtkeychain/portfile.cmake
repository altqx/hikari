# Built against the provisioned official Qt (HIKARI_QT_PREFIX, part of the package ABI). Linux links the host's
# libsecret-1 (libsecret-1-dev, libsecret-devel); no keyring daemon is an error, never a plain-text fallback.
if(NOT DEFINED ENV{HIKARI_QT_PREFIX} OR NOT EXISTS "$ENV{HIKARI_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "qtkeychain needs HIKARI_QT_PREFIX pointing at the provisioned Qt")
endif()
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO frankosterfeld/qtkeychain
    REF 875f77d9f61bd97fd84cca47ce3bc71186dfbd09 # 0.17.0
    SHA512 025b56fb1bd1f793d5688fab51fbc47766c35fe2a40bcad60d19fe52308fd35109b4b339c4e14aa9c108c81ece5497c2b24bdbdb859a0bb7ecbfb05cea98ed70
    HEAD_REF main
)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" QTKEYCHAIN_SHARED)
set(platform_options "")
if(VCPKG_TARGET_IS_LINUX)
    list(APPEND platform_options -DLIBSECRET_SUPPORT=ON)
endif()
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_WITH_QT5=OFF
        -DBUILD_SHARED_LIBS=${QTKEYCHAIN_SHARED}
        -DBUILD_TRANSLATIONS=OFF
        -DBUILD_TEST_APPLICATION=OFF
        -DBUILD_QTQUICK_DEMO=OFF
        ${platform_options}
        "-DCMAKE_PREFIX_PATH=$ENV{HIKARI_QT_PREFIX}"
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
        -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/Qt6Keychain" PACKAGE_NAME qt6keychain)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/mkspecs" "${CURRENT_PACKAGES_DIR}/debug/mkspecs")
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
