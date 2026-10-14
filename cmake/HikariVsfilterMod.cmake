# VSFilterMod, the CSRI renderer the Windows payload ships as
# Csri/VSFiltermod.dll: one prebuilt release archive and the DLL inside it,
# both pinned by SHA-256. This is the only place the release is named; moving
# to another release (user 2026-10-14: the altqx/VSFilterMod fork's release)
# changes only the five HIKARI_VSFILTERMOD_* lines below.
#
# Now: sorayuki/VSFilterMod r5.2.6, VSFilterMod_bin.zip, member
# x64/VSFilterMod.dll, the same bytes as the Csri/VSFiltermod.dll legacy 0.0.1
# shipped (legacy's .github/scripts/runtime-assets.json fetches it unverified).
# Its imports are Windows libraries only.
#
# Users: HikariPackageWindows.cmake (the payload), the windows-x64-vsfiltermod
# test build (HIKARI_TEST_VSFILTERMOD, tests/CMakeLists.txt), and
#   cmake -DDOWNLOADS=<dir> -P cmake/HikariVsfilterMod.cmake
# which fetches and checks it into <dir> and prints the DLL's path.

include_guard(GLOBAL)

set(HIKARI_VSFILTERMOD_RELEASE "sorayuki-VSFilterMod-r5.2.6")
set(HIKARI_VSFILTERMOD_URL "https://github.com/sorayuki/VSFilterMod/releases/download/r5.2.6/VSFilterMod_bin.zip")
set(HIKARI_VSFILTERMOD_ZIP_SHA256 "a256dcac5d776d8ab3a592f7ccae7d9151feafb203196fea7836f71b00b48ddd")
set(HIKARI_VSFILTERMOD_MEMBER "x64/VSFilterMod.dll")
set(HIKARI_VSFILTERMOD_DLL_SHA256 "87a9c3c1ab7614eda22b8fb367840a87fd34c0c7fb92d7c4d3aa6bb0b2a6ee6b")

# hikari_fetch_vsfiltermod(<downloads> <out-var>): the pinned archive in
# <downloads> (downloaded unless a copy with the pinned hash is there), its
# DLL extracted beside it and checked; <out-var> is the DLL's path.
function(hikari_fetch_vsfiltermod downloads out)
    file(MAKE_DIRECTORY "${downloads}")
    set(zip "${downloads}/${HIKARI_VSFILTERMOD_RELEASE}.zip")
    if(EXISTS "${zip}")
        file(SHA256 "${zip}" have)
        if(NOT have STREQUAL HIKARI_VSFILTERMOD_ZIP_SHA256)
            file(REMOVE "${zip}")
        endif()
    endif()
    if(NOT EXISTS "${zip}")
        file(DOWNLOAD "${HIKARI_VSFILTERMOD_URL}" "${zip}" EXPECTED_HASH SHA256=${HIKARI_VSFILTERMOD_ZIP_SHA256}
            TLS_VERIFY ON STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${zip}")
            message(FATAL_ERROR "VSFilterMod: downloading ${HIKARI_VSFILTERMOD_URL} failed: ${status}")
        endif()
    endif()
    set(dir "${downloads}/${HIKARI_VSFILTERMOD_RELEASE}")
    file(REMOVE_RECURSE "${dir}")
    file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${dir}" PATTERNS "${HIKARI_VSFILTERMOD_MEMBER}")
    set(dll "${dir}/${HIKARI_VSFILTERMOD_MEMBER}")
    if(NOT EXISTS "${dll}")
        message(FATAL_ERROR "VSFilterMod: ${zip} has no ${HIKARI_VSFILTERMOD_MEMBER}")
    endif()
    file(SHA256 "${dll}" have)
    if(NOT have STREQUAL HIKARI_VSFILTERMOD_DLL_SHA256)
        message(FATAL_ERROR "VSFilterMod: ${HIKARI_VSFILTERMOD_MEMBER} in ${zip} has SHA-256 ${have}, "
            "not ${HIKARI_VSFILTERMOD_DLL_SHA256}")
    endif()
    set(${out} "${dll}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DOWNLOADS)
        message(FATAL_ERROR "usage: cmake -DDOWNLOADS=<dir> -P ${CMAKE_CURRENT_LIST_FILE}")
    endif()
    hikari_fetch_vsfiltermod("${DOWNLOADS}" dll)
    message("${dll}")
endif()
