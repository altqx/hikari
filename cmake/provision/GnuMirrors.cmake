# vcpkg's asset source (X_VCPKG_ASSET_SOURCES x-script, set by Vcpkg.cmake):
# a GNU download is asked of gnu.org first and, when it does not answer, of
# other mirrors of the same tree (ftp.gnu.org and ftpmirror.gnu.org were down
# for hours on 2026-10-06, and vcpkg waits minutes on each of the port's
# URLs). Any other download is fetched as given. Every file must have the
# SHA512 vcpkg expects; when no source serves it, vcpkg falls back to the
# port's own URLs.
#
#   cmake -DHIKARI_ASSET_URL=<url> -DHIKARI_ASSET_SHA512=<sha512>
#         -DHIKARI_ASSET_DST=<file> -P GnuMirrors.cmake
cmake_minimum_required(VERSION 3.25)

foreach(var HIKARI_ASSET_URL HIKARI_ASSET_SHA512 HIKARI_ASSET_DST)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "GnuMirrors.cmake needs -D${var}")
    endif()
endforeach()

set(urls "${HIKARI_ASSET_URL}")
set(first_timeout 600)
if(HIKARI_ASSET_URL MATCHES "^https?://(ftp\\.gnu\\.org/(pub/)?gnu|ftpmirror\\.gnu\\.org(/gnu)?|www\\.mirrorservice\\.org/sites/ftp\\.gnu\\.org/gnu)/(.+)$")
    set(path "${CMAKE_MATCH_4}")
    get_filename_component(file "${path}" NAME)
    # gnu.org gets a short time: the mirrors serve the same bytes.
    set(first_timeout 30)
    list(APPEND urls
        "https://mirrors.kernel.org/gnu/${path}"
        "https://www.mirrorservice.org/sites/ftp.gnu.org/gnu/${path}"
        "https://mirrors.ocf.berkeley.edu/gnu/${path}"
        "https://mirror.csclub.uwaterloo.ca/gnu/${path}"
        "https://fossies.org/linux/privat/${file}") # some packages only
    list(REMOVE_DUPLICATES urls)
endif()

string(TOLOWER "${HIKARI_ASSET_SHA512}" expected)
set(part "${HIKARI_ASSET_DST}.part")
set(timeout ${first_timeout})
foreach(url IN LISTS urls)
    file(REMOVE "${part}")
    file(DOWNLOAD "${url}" "${part}" STATUS status TIMEOUT ${timeout} INACTIVITY_TIMEOUT 30 TLS_VERIFY ON)
    set(timeout 600)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 reason)
        message(STATUS "${url}: ${reason}")
        continue()
    endif()
    file(SHA512 "${part}" actual)
    if(NOT actual STREQUAL expected)
        message(STATUS "${url}: SHA512 ${actual} is not the expected ${expected}")
        continue()
    endif()
    file(RENAME "${part}" "${HIKARI_ASSET_DST}")
    message(STATUS "Downloaded ${url}")
    return()
endforeach()
file(REMOVE "${part}")
message(FATAL_ERROR "No source served ${HIKARI_ASSET_URL}")
