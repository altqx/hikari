# B4: a changed pinned input fails provisioning explicitly. Copies one port,
# alters one digit of its pinned SHA512, puts the genuine cached archive where
# vcpkg looks for that digest, and requires the install to stop with vcpkg's
# hash error. Runs offline (origin blocked) with private install, build and
# package trees; only the downloads root (and its vcpkg tools) is shared, and
# the one file staged there is removed afterwards.
#
#   cmake -DVCPKG=<vcpkg executable> -DPORT_DIR=<port> -DDOWNLOADS=<downloads>
#         -DTRIPLETS=<overlay triplets> -DTRIPLET=<triplet> -DWORK=<dir>
#         -P changed_input.cmake
foreach(var VCPKG PORT_DIR DOWNLOADS TRIPLETS TRIPLET WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "changed_input: ${var} is required")
    endif()
endforeach()

get_filename_component(port "${PORT_DIR}" NAME)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/ports" "${WORK}/cwd")
file(COPY "${PORT_DIR}" DESTINATION "${WORK}/ports")

set(portfile "${WORK}/ports/${port}/portfile.cmake")
file(READ "${portfile}" text)
if(NOT text MATCHES "REPO[ \t]+([^ \t\r\n]+)")
    message(FATAL_ERROR "changed_input: no REPO in ${port}'s portfile")
endif()
string(REPLACE "/" "-" archive_stem "${CMAKE_MATCH_1}")
if(NOT text MATCHES "REF[ \t]+([0-9a-f]+)")
    message(FATAL_ERROR "changed_input: no REF in ${port}'s portfile")
endif()
string(APPEND archive_stem "-${CMAKE_MATCH_1}")
if(NOT text MATCHES "SHA512[ \t]+([0-9a-f]+)")
    message(FATAL_ERROR "changed_input: no SHA512 in ${port}'s portfile")
endif()
set(pinned "${CMAKE_MATCH_1}")
string(SUBSTRING "${pinned}" 0 1 first)
string(SUBSTRING "${pinned}" 1 -1 rest)
if(first STREQUAL "0")
    set(altered "1${rest}")
else()
    set(altered "0${rest}")
endif()
string(REPLACE "${pinned}" "${altered}" text "${text}")
file(WRITE "${portfile}" "${text}")

set(genuine "${DOWNLOADS}/${archive_stem}.tar.gz")
if(NOT EXISTS "${genuine}")
    message(FATAL_ERROR "changed_input: the provisioned archive ${genuine} is missing")
endif()
# vcpkg checks the plain name first; when that file has the wrong digest it
# uses the digest-suffixed name, where the genuine archive is staged.
string(SUBSTRING "${altered}" 0 8 digest8)
set(staged "${DOWNLOADS}/${archive_stem}-${digest8}.tar.gz")
file(COPY_FILE "${genuine}" "${staged}")

execute_process(
    COMMAND "${VCPKG}" install "${port}" --classic --x-asset-sources=x-block-origin --binarysource=clear
        "--overlay-ports=${WORK}/ports" "--overlay-triplets=${TRIPLETS}" "--triplet=${TRIPLET}"
        "--downloads-root=${DOWNLOADS}" "--x-install-root=${WORK}/installed"
        "--x-buildtrees-root=${WORK}/buildtrees" "--x-packages-root=${WORK}/packages"
    WORKING_DIRECTORY "${WORK}/cwd"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out)
file(REMOVE "${staged}")
if(result EQUAL 0)
    message(FATAL_ERROR "FAIL: ${port} installed with an altered SHA512\n${out}")
endif()
if(NOT out MATCHES "unexpected hash")
    message(FATAL_ERROR "FAIL: ${port} failed, but not on the hash check\n${out}")
endif()
message(STATUS "PASS changed-input ${port}: vcpkg refused the archive (expected ${altered}, actual ${pinned})")
file(REMOVE_RECURSE "${WORK}")
