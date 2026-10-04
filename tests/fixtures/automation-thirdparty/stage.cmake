# L6 third-party automation corpus: verify, fetch and stage (A33-compat).
#
#   cmake -D MANIFEST=<manifest.json> -D FETCH_DIR=<cache> -D OUT=<staged tree>
#         [-D STAGE_INCLUDE=<bundled Include>] -P stage.cmake
#
# Projects whose licence allows it are committed next to the manifest
# (<directory>/ with their licence file); the others are fetched here from the
# pinned commit's raw URLs into FETCH_DIR. Every file, committed or fetched,
# must match the manifest's sha256: a mismatch is an error, not a skip. When a
# fetch fails (offline, no TLS), that project is marked unavailable in
# OUT/status.json with the reason and the run continues; the tests then skip
# its scripts with that message.
#
# OUT receives the corpus as a user installs it: OUT/Autoload/<script> and
# OUT/Include = the bundled Include (STAGE_INCLUDE, with DependencyControl's
# native modules) plus the projects' modules. Bundled modules are never
# replaced by a project's copy. OUT/Modules holds the projects' modules alone.
cmake_minimum_required(VERSION 3.25)

foreach(var MANIFEST FETCH_DIR OUT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "stage.cmake: -D ${var}=... is required")
    endif()
endforeach()

get_filename_component(SOURCE_DIR "${MANIFEST}" DIRECTORY)
file(READ "${MANIFEST}" manifest)
string(JSON project_count LENGTH "${manifest}" projects)
math(EXPR last "${project_count} - 1")

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/Autoload" "${OUT}/Include" "${OUT}/Modules")
if(DEFINED STAGE_INCLUDE AND NOT STAGE_INCLUDE STREQUAL "")
    file(COPY "${STAGE_INCLUDE}/" DESTINATION "${OUT}/Include")
endif()

set(status "")
foreach(i RANGE ${last})
    string(JSON id GET "${manifest}" projects ${i} id)
    string(JSON included GET "${manifest}" projects ${i} included)
    string(JSON directory GET "${manifest}" projects ${i} directory)
    string(JSON raw_url GET "${manifest}" projects ${i} raw_url)
    string(JSON file_count LENGTH "${manifest}" projects ${i} files)
    math(EXPR file_last "${file_count} - 1")
    if(included STREQUAL "committed")
        set(root "${SOURCE_DIR}/${directory}")
    else()
        set(root "${FETCH_DIR}/${directory}")
    endif()
    set(reason "")
    foreach(j RANGE ${file_last})
        string(JSON path GET "${manifest}" projects ${i} files ${j} path)
        string(JSON expected GET "${manifest}" projects ${i} files ${j} sha256)
        set(file "${root}/${path}")
        if(EXISTS "${file}")
            file(SHA256 "${file}" actual)
            if(NOT actual STREQUAL expected AND included STREQUAL "fetched")
                file(REMOVE "${file}") # a stale cache entry: fetch it again
            endif()
        endif()
        if(NOT EXISTS "${file}")
            if(NOT included STREQUAL "fetched")
                message(FATAL_ERROR "${id}: committed file missing: ${file}")
            endif()
            file(DOWNLOAD "${raw_url}${path}" "${file}.part" TIMEOUT 60 INACTIVITY_TIMEOUT 20 TLS_VERIFY ON
                 STATUS download)
            list(GET download 0 code)
            if(NOT code EQUAL 0)
                list(GET download 1 why)
                file(REMOVE "${file}.part")
                string(REPLACE "\"" "'" why "${why}")
                set(reason "could not fetch ${raw_url}${path} (${why}); offline?")
                break()
            endif()
            file(RENAME "${file}.part" "${file}")
        endif()
        file(SHA256 "${file}" actual)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${id}: ${path} has sha256 ${actual}, the manifest pins ${expected}")
        endif()
    endforeach()
    if(NOT reason STREQUAL "")
        message(STATUS "automation-thirdparty: ${id} unavailable, its scripts are skipped: ${reason}")
        string(APPEND status "{\"id\": \"${id}\", \"available\": false, \"reason\": \"${reason}\"},\n")
        continue()
    endif()
    foreach(j RANGE ${file_last})
        string(JSON role GET "${manifest}" projects ${i} files ${j} role)
        if(role STREQUAL "licence")
            continue()
        endif()
        string(JSON path GET "${manifest}" projects ${i} files ${j} path)
        string(JSON install GET "${manifest}" projects ${i} files ${j} install)
        if(EXISTS "${OUT}/${install}")
            message(FATAL_ERROR "${id}: ${install} is already staged (bundled or another project)")
        endif()
        get_filename_component(dest_dir "${OUT}/${install}" DIRECTORY)
        file(MAKE_DIRECTORY "${dest_dir}")
        file(COPY_FILE "${root}/${path}" "${OUT}/${install}")
        if(role STREQUAL "module")
            # The projects' modules alone, for a legacy package's Include
            # (tools/legacy-capture/drive.py --include-dir).
            string(REGEX REPLACE "^Include/" "Modules/" module "${install}")
            get_filename_component(dest_dir "${OUT}/${module}" DIRECTORY)
            file(MAKE_DIRECTORY "${dest_dir}")
            file(COPY_FILE "${root}/${path}" "${OUT}/${module}")
        endif()
    endforeach()
    message(STATUS "automation-thirdparty: ${id} staged (${included})")
    string(APPEND status "{\"id\": \"${id}\", \"available\": true},\n")
endforeach()
string(REGEX REPLACE ",\n$" "\n" status "${status}")
file(WRITE "${OUT}/status.json" "[\n${status}]\n")
