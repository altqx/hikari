if(NOT DEFINED HIKARISUB_EXE OR NOT DEFINED RUNTIME_DIR)
    message(FATAL_ERROR "HIKARISUB_EXE and RUNTIME_DIR are required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/LinuxHostLibs.cmake")
hikarisub_rpath_dirs("${HIKARISUB_EXE}" exe_rpath_dirs)

# Remove previously manifested libraries before rescanning $ORIGIN.
set(runtime_manifest "${RUNTIME_DIR}/.hikarisub-runtime-dependencies")
if(EXISTS "${runtime_manifest}")
    file(STRINGS "${runtime_manifest}" previous_runtime_deps)
    foreach(dep_name IN LISTS previous_runtime_deps)
        if(dep_name MATCHES "^lib[^/\\\\]+\\.so(\\..*)?$")
            file(REMOVE "${RUNTIME_DIR}/${dep_name}")
        else()
            message(WARNING "Ignoring invalid runtime manifest entry: ${dep_name}")
        endif()
    endforeach()
elseif(DEFINED HIKARISUB_BUILD_DIR)
    # Clean pre-manifest files only in the verified CMake build directory.
    file(REAL_PATH "${RUNTIME_DIR}" runtime_dir_real)
    file(REAL_PATH "${HIKARISUB_BUILD_DIR}" build_dir_real)
    if(runtime_dir_real STREQUAL build_dir_real AND
       EXISTS "${build_dir_real}/CMakeCache.txt")
        # RUNTIME_DIR is the build root, so a bare lib*.so glob would also delete
        # hand-staged libraries. Probe the stale $ORIGIN copies and remove only
        # the ones the executable actually links.
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "${HIKARISUB_EXE}"
            DIRECTORIES ${exe_rpath_dirs}
            RESOLVED_DEPENDENCIES_VAR probe_runtime_deps
        )
        set(legacy_runtime_deps)
        foreach(probe_dep IN LISTS probe_runtime_deps)
            get_filename_component(probe_name "${probe_dep}" NAME)
            get_filename_component(probe_dir "${probe_dep}" DIRECTORY)
            file(REAL_PATH "${probe_dir}" probe_dir_real)
            if(probe_dir_real STREQUAL runtime_dir_real AND
               probe_name MATCHES "^lib[^/\\\\]+\\.so(\\..*)?$")
                list(APPEND legacy_runtime_deps "${probe_dep}")
            endif()
        endforeach()
        if(legacy_runtime_deps)
            list(REMOVE_DUPLICATES legacy_runtime_deps)
            list(LENGTH legacy_runtime_deps legacy_count)
            file(REMOVE ${legacy_runtime_deps})
            message(STATUS "Removed ${legacy_count} legacy bundled runtime dependencies before dependency discovery")
        endif()
    endif()
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${HIKARISUB_EXE}"
    DIRECTORIES ${exe_rpath_dirs}
    RESOLVED_DEPENDENCIES_VAR resolved_deps
    UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
)

if(unresolved_deps)
    message(FATAL_ERROR "Unresolved runtime dependencies: ${unresolved_deps}")
endif()

# Bundle only the application's own version-sensitive libraries (wxWidgets,
# ICU, FFMS2 and FFmpeg with the codec libraries it links, libass, Hunspell,
# uchardet, curl and their private transitive helpers) next to the binary.
# Everything else comes from the host; cmake/LinuxHostLibs.cmake has the list
# (packaging/linux/appimage/host-libs.txt plus the GTK, GLib and GStreamer
# desktop stack) and applies the closure rule: whatever a host library needs
# is host-side too.
hikarisub_split_runtime_deps(host bundled_deps host_dep_names ${resolved_deps})

set(copied_count 0)
set(copied_names)
foreach(dep IN LISTS bundled_deps)
    get_filename_component(dep_name "${dep}" NAME)
    set(copied_dep "${RUNTIME_DIR}/${dep_name}")
    # A library already in RUNTIME_DIR resolves to itself; nothing to copy.
    get_filename_component(dep_dir "${dep}" DIRECTORY)
    file(REAL_PATH "${dep_dir}" dep_dir_real)
    file(REAL_PATH "${RUNTIME_DIR}" runtime_dir_real_now)
    if(NOT dep_dir_real STREQUAL runtime_dir_real_now)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${dep}" "${copied_dep}"
            RESULT_VARIABLE copy_result
        )
        if(NOT copy_result EQUAL 0)
            message(FATAL_ERROR "Failed to copy runtime dependency ${dep}")
        endif()
    endif()
    list(APPEND copied_names "${dep_name}")
    math(EXPR copied_count "${copied_count} + 1")
endforeach()

list(REMOVE_DUPLICATES copied_names)
list(SORT copied_names)
string(REPLACE ";" "\n" runtime_manifest_contents "${copied_names}")
file(WRITE "${runtime_manifest}.tmp" "${runtime_manifest_contents}\n")
file(RENAME "${runtime_manifest}.tmp" "${runtime_manifest}")

message(STATUS "Copied ${copied_count} bundled runtime dependencies to ${RUNTIME_DIR}")
