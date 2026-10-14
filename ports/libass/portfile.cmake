# Owned libass port: the altqx/libass fork (upstream libass plus the perf
# batch, the frame atlas, the event-index API and the font diagnostics the
# collector uses), pinned at a master commit. The fork carries what used to be
# this overlay's patches (the arm64 Windows assembly and the font diagnostics),
# so there are none here. Optional dependencies stay explicit.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/altqx/libass.git
    REF 4f5ea43630bb89c9caacaf4742a8af4f3e26edf7
)

vcpkg_find_acquire_program(PKGCONFIG)
get_filename_component(PKGCONFIG_EXE_PATH ${PKGCONFIG} DIRECTORY)
vcpkg_add_to_path(${PKGCONFIG_EXE_PATH})

list(APPEND options
    -Dcheckasm=disabled
    -Dcompare=disabled
    -Dfuzz=disabled
    -Dprofile=disabled
    -Dtest=disabled
    # Hikari-owned: no host auto-detection. libunibreak is not in the locked
    # graph, so a system copy must never be picked up.
    -Dlibunibreak=disabled
    -Dcoretext=disabled
    # The rasterizer's 16x16 tiles, as upstream's default and the golden
    # frames the fork is checked against.
    -Dlarge-tiles=false
)
if(VCPKG_TARGET_IS_WINDOWS)
    list(APPEND options -Ddirectwrite=enabled -Dfontconfig=disabled)
else()
    list(APPEND options -Ddirectwrite=disabled -Dfontconfig=enabled)
endif()

if(NOT VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_OSX AND NOT VCPKG_TARGET_IS_LINUX)
    list(APPEND options -Drequire-system-font-provider=false)
endif()

set(asm_option disabled)
set(additional_binaries "")
if(VCPKG_TARGET_ARCHITECTURE MATCHES "^(x86|x64|arm64)$")
    set(asm_option enabled)
    if(VCPKG_TARGET_ARCHITECTURE MATCHES "^(x86|x64)$")
        vcpkg_find_acquire_program(NASM)
        get_filename_component(NASM_EXE_PATH "${NASM}" DIRECTORY)
        vcpkg_add_to_path("${NASM_EXE_PATH}")
    elseif(VCPKG_TARGET_IS_WINDOWS AND VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        vcpkg_find_acquire_program(CLANG)
        list(APPEND additional_binaries "clang = ['${CLANG}']")
    endif()
else()
    message(WARNING "Assembly optimizations are not supported on ${VCPKG_TARGET_ARCHITECTURE}; disabling assembly optimizations.")
endif()
list(APPEND options -Dasm=${asm_option})

vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${options}
    ADDITIONAL_BINARIES
        ${additional_binaries}
)

vcpkg_install_meson()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

# Handle copyright
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
