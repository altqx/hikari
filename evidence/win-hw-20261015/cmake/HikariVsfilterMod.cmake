# VSFilterMod, the CSRI renderer the Windows payload ships as
# Csri/VSFiltermod.dll: one prebuilt release archive and the DLL inside it,
# both pinned by SHA-256. This is the only place the release is named; moving
# to another release changes only the five HIKARI_VSFILTERMOD_* lines below.
#
# Now (user 2026-10-14, decisions 29 and 38b): altqx/VSFilterMod
# r5.3.1-hikari.2, a fork of Masaiki/VSFilterMod r5.3.1 (effb5a7) that fixes
# the upstream issues open then (\img offsets, \img from attached images,
# VobSub named options) and draws CSRI_F_BGRA with straight alpha (255
# opaque) over the frame's, built from the fork's main e51456f by its MSVC
# workflow (run 38597504369).
# VSFilterMod_bin.zip, member x64/VSFilterMod.dll. Its imports are Windows
# libraries only. Before it the payload shipped r5.3.1-hikari.1 (d1737a93,
# zip c9e6326b..., DLL 68a16573...; BGRA drawn premultiplied with inverted
# alpha) and, before that, sorayuki/VSFilterMod r5.2.6, the DLL legacy 0.0.1
# shipped (zip a256dcac..., DLL 87a9c3c1...).
#
# Users: HikariPackageWindows.cmake (the payload), the windows-x64-vsfiltermod
# test build (HIKARI_TEST_VSFILTERMOD, tests/CMakeLists.txt), and
#   cmake -DDOWNLOADS=<dir> -P cmake/HikariVsfilterMod.cmake
# which fetches and checks it into <dir> and prints the DLL's path.

include_guard(GLOBAL)

set(HIKARI_VSFILTERMOD_RELEASE "altqx-VSFilterMod-r5.3.1-hikari.2")
set(HIKARI_VSFILTERMOD_URL "https://github.com/altqx/VSFilterMod/releases/download/r5.3.1-hikari.2/VSFilterMod_bin.zip")
set(HIKARI_VSFILTERMOD_ZIP_SHA256 "48eb70065cc41b5f034c511954b6bcda9f5fa008326860e4b31ffb2f6d16bbb8")
set(HIKARI_VSFILTERMOD_MEMBER "x64/VSFilterMod.dll")
set(HIKARI_VSFILTERMOD_DLL_SHA256 "e4386c63280bac1cdbb0ef6434643a4af797d45ad3e49b194c2db002574e2bfe")

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
