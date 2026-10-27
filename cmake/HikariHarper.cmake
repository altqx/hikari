# GR1 (#364): harper-ls, the grammar checker's helper process (LSP over
# stdio), upstream's release build pinned by SHA-256: the archive and the
# binary in it. Linux takes the static musl build (no host libraries, so it
# passes the package's glibc ceiling); Windows the MSVC build, whose
# VCRUNTIME140 the package already ships. Apache-2.0: its LICENSE (pinned
# too) and the crates' notices (packaging/licenses/harper) go in licenses/harper.
#
#   hikari_fetch_harper(<downloads> <out-var>)
#
# fetches the archive and the LICENSE into <downloads> (each kept while its
# hash matches, so an offline configure works from a seeded folder), extracts
# the binary beside them and checks it; <out-var> is its path and
# <out-var>_LICENSE the licence's. Script mode:
#
#   cmake -DDOWNLOADS=<dir> -P cmake/HikariHarper.cmake
include_guard(GLOBAL)

set(HIKARI_HARPER_VERSION "3.0.0")
set(HIKARI_HARPER_RELEASE_URL "https://github.com/Automattic/harper/releases/download/v${HIKARI_HARPER_VERSION}")
set(HIKARI_HARPER_LICENSE_URL "https://raw.githubusercontent.com/Automattic/harper/v${HIKARI_HARPER_VERSION}/LICENSE")
set(HIKARI_HARPER_LICENSE_SHA256 "fbc8f1bffe04ab962340fabb1324d57d8b875070f67759d606516144ee53ac19")
if(WIN32)
    set(HIKARI_HARPER_ARCHIVE "harper-ls-x86_64-pc-windows-msvc.zip")
    set(HIKARI_HARPER_ARCHIVE_SHA256 "5300b63178909ea9fb126c6357d44a8a09a9e1418152c4225b0b1ca0ec403281")
    set(HIKARI_HARPER_MEMBER "harper-ls.exe")
    set(HIKARI_HARPER_SHA256 "b515bfae4582a66af6f9fe2dd6201cb1be97f97d8b240eaf93a3a47005c9a80d")
else()
    set(HIKARI_HARPER_ARCHIVE "harper-ls-x86_64-unknown-linux-musl.tar.gz")
    set(HIKARI_HARPER_ARCHIVE_SHA256 "ce62e5a37d006e1d9b56c36140cb7abf0cfb8c72793c82b3e5d24646cde85bac")
    set(HIKARI_HARPER_MEMBER "harper-ls")
    set(HIKARI_HARPER_SHA256 "f8e8d7514518c5d66437a455fbd9a8b2114412bd21b7c23cbd57341514c28626")
endif()

# hikari_harper_download(<url> <file> <sha256>): <file>, downloaded unless a
# copy with the pinned hash is there.
function(hikari_harper_download url file sha256)
    if(EXISTS "${file}")
        file(SHA256 "${file}" have)
        if(NOT have STREQUAL sha256)
            file(REMOVE "${file}")
        endif()
    endif()
    if(NOT EXISTS "${file}")
        file(DOWNLOAD "${url}" "${file}" EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${file}")
            message(FATAL_ERROR "Harper: downloading ${url} failed: ${status}")
        endif()
    endif()
endfunction()

function(hikari_fetch_harper downloads out)
    if(CMAKE_HOST_SYSTEM_PROCESSOR AND NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        message(FATAL_ERROR "Harper: the pinned harper-ls is for x86-64, not ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    file(MAKE_DIRECTORY "${downloads}")
    set(archive "${downloads}/${HIKARI_HARPER_VERSION}-${HIKARI_HARPER_ARCHIVE}")
    hikari_harper_download("${HIKARI_HARPER_RELEASE_URL}/${HIKARI_HARPER_ARCHIVE}" "${archive}"
        ${HIKARI_HARPER_ARCHIVE_SHA256})
    hikari_harper_download("${HIKARI_HARPER_LICENSE_URL}" "${downloads}/${HIKARI_HARPER_VERSION}-LICENSE"
        ${HIKARI_HARPER_LICENSE_SHA256})
    set(dir "${downloads}/harper-ls-${HIKARI_HARPER_VERSION}")
    set(binary "${dir}/${HIKARI_HARPER_MEMBER}")
    set(have "")
    if(EXISTS "${binary}")
        file(SHA256 "${binary}" have)
    endif()
    if(NOT have STREQUAL HIKARI_HARPER_SHA256)
        file(REMOVE_RECURSE "${dir}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dir}" PATTERNS "${HIKARI_HARPER_MEMBER}")
        if(NOT EXISTS "${binary}")
            message(FATAL_ERROR "Harper: ${archive} has no ${HIKARI_HARPER_MEMBER}")
        endif()
        file(SHA256 "${binary}" have)
        if(NOT have STREQUAL HIKARI_HARPER_SHA256)
            message(FATAL_ERROR "Harper: ${HIKARI_HARPER_MEMBER} in ${archive} has SHA-256 ${have}, "
                "not ${HIKARI_HARPER_SHA256}")
        endif()
        file(CHMOD "${binary}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    endif()
    set(${out} "${binary}" PARENT_SCOPE)
    set(${out}_LICENSE "${downloads}/${HIKARI_HARPER_VERSION}-LICENSE" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DOWNLOADS)
        message(FATAL_ERROR "usage: cmake -DDOWNLOADS=<dir> -P ${CMAKE_CURRENT_LIST_FILE}")
    endif()
    hikari_fetch_harper("${DOWNLOADS}" binary)
    message("${binary}")
endif()
