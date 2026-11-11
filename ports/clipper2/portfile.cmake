# ILL's ffi-packages/build.sh pins this commit; it reports CLIPPER2_VERSION 1.2.2 but is 27 commits past that tag.
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AngusJohnson/Clipper2
    REF 866e40201476b9af9fec5ce80de16bf0b03eff7a
    SHA512 25f4f74e985d5eb84027e8ede69f3f6fdc9d8c916785617a56bbc4f7a038d9ce62923e1f02aac1d779998db2e0e8e79b5adb99f3292d17125da43a73a6195af7
    HEAD_REF main
)
# Upstream builds with -Werror and /WX; GCC 16 warns on its headers (template-id-cdtor).
vcpkg_replace_string("${SOURCE_PATH}/CPP/CMakeLists.txt" " /WX)" ")")
vcpkg_replace_string("${SOURCE_PATH}/CPP/CMakeLists.txt" " -Werror)" ")")
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/CPP"
    OPTIONS
        -DCLIPPER2_EXAMPLES=OFF
        -DCLIPPER2_TESTS=OFF
        -DCLIPPER2_UTILS=OFF
        -DCLIPPER2_USINGZ=OFF
)
vcpkg_cmake_install()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib/pkgconfig" "${CURRENT_PACKAGES_DIR}/debug")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
