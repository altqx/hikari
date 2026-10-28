# Owned FFMS2 recipe: fork sources plus the Hikari private API. Never link the old bundled FFMS2/FFmpeg binaries.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/altqx/ffms2.git
    REF 45d5f72100d88c52acdd54bfedcc0315a44c735d
)
vcpkg_download_distfile(ADDITIONAL_SOURCE
    URLS https://raw.githubusercontent.com/altqx/hikari/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/Thirdparty/Build/FFMS2/indexing_additional.cpp
    FILENAME hikari-indexing_additional-20d647c4.cpp
    SHA512 881b43761897a1c75204de2249b91fa744e72a6e88dc6d7c94ccf6d472eba6869cea34700e469443f2de92c0ba472a64594ece974201e9cbbec4f4f290ca3e4f
)
vcpkg_cmake_configure(
    SOURCE_PATH "${CURRENT_PORT_DIR}"
    OPTIONS
        "-DFFMS2_SOURCE_DIR=${SOURCE_PATH}"
        "-DFFMS2_ADDITIONAL_SOURCE=${ADDITIONAL_SOURCE}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME HikariFFMS2 CONFIG_PATH lib/cmake/HikariFFMS2)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING" "${ADDITIONAL_SOURCE}")
