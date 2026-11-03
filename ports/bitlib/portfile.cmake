# Upstream has no install rules: the headers, a CMake config and the test sources are installed by hand.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bkille/BitLib
    REF e7a50c7bffda9183754a2f4d773e3909a7d4d325
    SHA512 8437bafca425c56e2e860fc4660f9dc0edb568d37972a79dad3b32e1b5164199b3250c9757a0ab05f3dfc54c50aa2fb4647d78f239e0beaed1801dfaf5994ecb
    HEAD_REF master
)
file(INSTALL "${SOURCE_PATH}/include/bitlib" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(GLOB tests "${SOURCE_PATH}/test/src/*")
file(INSTALL ${tests} "${SOURCE_PATH}/utils/test_utils.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/share/bitlib/tests")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/bitlib-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/bitlib")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
