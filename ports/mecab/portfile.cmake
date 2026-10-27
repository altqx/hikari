# KA4 (#279): MeCab, the furigana suggestions' analyser. vcpkg's port at the
# baseline, owned here (its own CI skips it on every triplet): libmecab only,
# Ninja instead of MSBuild, the dictionary memory-mapped on Linux, wide paths
# for every file MSVC opens and a static build without dllimport. MeCab is
# GPL / LGPL / BSD at the user's choice; HikariSub takes the BSD licence.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO taku910/mecab
    REF 3a07c4eefaffb4e7a0690a7f4e5e0263d3ddb8a3
    SHA512 d3288cca7207daf66df4349819b64fc9cc069c775512cf0607ca855e9e5512509c36b0e2bb0e955478acae13ff0c35df7442f18a8458fab0ed664d62854c0b25
    HEAD_REF master
    PATCHES
        fix_wpath_unsigned.patch
        fix-missing-functional.patch
        hikari-windows-static.patch
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}/mecab/src")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/mecab/src"
    OPTIONS
        -DCMAKE_CXX_STANDARD=11 # C++17 does not allow 'register'
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
vcpkg_copy_pdbs()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/mecab/COPYING" "${SOURCE_PATH}/mecab/BSD")
