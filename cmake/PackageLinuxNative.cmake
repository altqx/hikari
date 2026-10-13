# Builds the hikarisub deb and rpm with nFPM from the staged host-profile tree
# (cmake/PackageLinuxRuntime.cmake). Run in script mode by the
# hikarisub_native_packages target. Writes
#   <OUTPUT_DIR>/hikarisub-<V>-linux-amd64.deb
#   <OUTPUT_DIR>/hikarisub-<V>-linux-x86_64.rpm
# The file names use the SemVer <V>; the packages' own versions use the "~"
# form (0.0.2-rc.1 is deb 0.0.2~rc.1-1 and rpm 0.0.2~rc.1-1), which sorts a
# prerelease below its release.
#
# Layout (installers spec, 1.6 and 5.4): the app tree in /opt/hikarisub with
# package-kind.txt = deb or rpm, /usr/bin/hikarisub linking to it, the
# desktop, metainfo, MIME and icon files under /usr/share, and, once the
# update helper exists in packaging/linux/update-helper, the helper in
# /opt/hikarisub/libexec and its polkit action.
cmake_minimum_required(VERSION 3.21)
foreach(required_var IN ITEMS
        SOURCE_DIR BINARY_DIR PACKAGE_ROOT VERSION NFPM OUTPUT_DIR MIN_GLIBC)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()
if(NOT EXISTS "${NFPM}")
    message(FATAL_ERROR "nFPM is missing: ${NFPM}; fetch it with packaging/linux/fetch-tool.sh")
endif()
if(NOT EXISTS "${PACKAGE_ROOT}/hikarisub")
    message(FATAL_ERROR "The staged package tree is missing: ${PACKAGE_ROOT}")
endif()
if(NOT VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "Not a release version (no build metadata allowed): ${VERSION}")
endif()

set(app_id io.github.altqx.HikariSub)
set(pkgdir "${SOURCE_DIR}/packaging/linux")
set(work "${BINARY_DIR}/native")
set(tree "${work}/hikarisub")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${tree}" "${OUTPUT_DIR}")

# The app tree as /opt/hikarisub holds it: the desktop files go to
# /usr/share instead, and there is no portable installer or SHA256SUMS.
file(COPY "${PACKAGE_ROOT}/" DESTINATION "${tree}")
file(REMOVE_RECURSE "${tree}/share" "${tree}/install-desktop-integration.sh"
     "${tree}/SHA256SUMS")

set(helper "${pkgdir}/update-helper/hikarisub-update-helper")
set(policy "${pkgdir}/update-helper/${app_id}.update.policy")
set(extra_contents "")
if(EXISTS "${helper}")
    if(NOT EXISTS "${policy}")
        message(FATAL_ERROR "The update helper has no polkit policy: ${policy}")
    endif()
    file(MAKE_DIRECTORY "${tree}/libexec")
    file(COPY_FILE "${helper}" "${tree}/libexec/hikarisub-update-helper")
    file(CHMOD "${tree}/libexec/hikarisub-update-helper"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    string(APPEND extra_contents
        "  - src: \"${policy}\"\n"
        "    dst: /usr/share/polkit-1/actions/${app_id}.update.policy\n"
        "    file_info:\n      mode: 0644\n")
    message(STATUS "Packaging the update helper and its polkit action")
else()
    message(WARNING
        "packaging/linux/update-helper has no hikarisub-update-helper yet; "
        "the deb and rpm are built without the pkexec update path")
endif()

# /usr/share: every file the tar.gz carries under share/.
file(GLOB_RECURSE share_files LIST_DIRECTORIES FALSE RELATIVE "${PACKAGE_ROOT}/share"
     "${PACKAGE_ROOT}/share/*")
list(SORT share_files)
foreach(rel IN LISTS share_files)
    string(APPEND extra_contents
        "  - src: \"${PACKAGE_ROOT}/share/${rel}\"\n"
        "    dst: /usr/share/${rel}\n"
        "    file_info:\n      mode: 0644\n")
endforeach()

file(WRITE "${work}/kind-deb" "deb\n")
file(WRITE "${work}/kind-rpm" "rpm\n")
file(WRITE "${work}/copyright"
"Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: HikariSub
Upstream-Contact: https://github.com/altqx/hikari/issues
Source: https://github.com/altqx/hikari

Files: *
Copyright: altqx and the HikariSub contributors
License: GPL-3.0-or-later
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 On Debian systems, the complete text of the GNU General Public License
 version 3 can be found in /usr/share/common-licenses/GPL-3.
")

# Dependencies, from the sonames the tree needs and the maps.
function(resolve_deps family out_depends out_recommends)
    execute_process(
        COMMAND sh "${pkgdir}/resolve-deps.sh" "${family}" "${pkgdir}/deps-${family}.map"
                "${MIN_GLIBC}" "${tree}"
        OUTPUT_VARIABLE lines RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Could not resolve the ${family} dependencies")
    endif()
    string(REPLACE "\n" ";" lines "${lines}")
    set(depends "")
    set(recommends "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^(depends|recommends)\t(.+)$")
            # Copied first: the next MATCHES resets CMAKE_MATCH_<n>.
            set(kind "${CMAKE_MATCH_1}")
            set(dep "${CMAKE_MATCH_2}")
            if(dep MATCHES "\"")
                message(FATAL_ERROR "A dependency may not contain a quote: ${dep}")
            endif()
            set(item "      - \"${dep}\"\n")
            if(kind STREQUAL "depends")
                string(APPEND depends "${item}")
            else()
                string(APPEND recommends "${item}")
            endif()
        elseif(NOT line STREQUAL "")
            message(FATAL_ERROR "Unexpected ${family} dependency line: ${line}")
        endif()
    endforeach()
    message(STATUS "${family} depends:\n${depends}${family} recommends:\n${recommends}")
    string(REGEX REPLACE "\n$" "" depends "${depends}")
    string(REGEX REPLACE "\n$" "" recommends "${recommends}")
    set(${out_depends} "${depends}" PARENT_SCOPE)
    set(${out_recommends} "${recommends}" PARENT_SCOPE)
endfunction()
resolve_deps(deb DEB_DEPENDS DEB_RECOMMENDS)
resolve_deps(rpm RPM_DEPENDS RPM_RECOMMENDS)

string(FIND "${VERSION}" "-" dash)
if(dash EQUAL -1)
    set(PKG_VERSION "${VERSION}")
else()
    string(SUBSTRING "${VERSION}" 0 ${dash} release_part)
    math(EXPR after "${dash} + 1")
    string(SUBSTRING "${VERSION}" ${after} -1 pre_part)
    set(PKG_VERSION "${release_part}~${pre_part}")
endif()
set(APP_TREE "${tree}")
set(KIND_DEB "${work}/kind-deb")
set(KIND_RPM "${work}/kind-rpm")
set(COPYRIGHT "${work}/copyright")
set(LICENSE "${SOURCE_DIR}/LICENSE")
set(CONTENTS "${extra_contents}")
configure_file("${pkgdir}/nfpm.yaml.in" "${work}/nfpm.yaml" @ONLY)

foreach(format IN ITEMS deb rpm)
    if(format STREQUAL "deb")
        set(target "${OUTPUT_DIR}/hikarisub-${VERSION}-linux-amd64.deb")
    else()
        set(target "${OUTPUT_DIR}/hikarisub-${VERSION}-linux-x86_64.rpm")
    endif()
    file(REMOVE "${target}")
    execute_process(
        COMMAND "${NFPM}" package --config "${work}/nfpm.yaml"
                --packager "${format}" --target "${target}"
        WORKING_DIRECTORY "${work}"
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0 OR NOT EXISTS "${target}")
        message(FATAL_ERROR "nFPM failed to build the ${format}")
    endif()
    file(SHA256 "${target}" hash)
    message(STATUS "Created ${target} (${format} version ${PKG_VERSION}-1)")
    message(STATUS "SHA-256 ${hash}")
endforeach()
