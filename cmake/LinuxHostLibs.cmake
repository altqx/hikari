# Which runtime libraries a Linux package bundles and which it takes from the
# host. Used in script mode (cmake -P) by CopyRuntimeDependencies.cmake and
# PackageLinuxAppImage.cmake.
#
# Two profiles (the installers spec, section 5.2):
#
#   host  the tar.gz, deb, rpm and AUR trees. The host provides the libraries
#         in packaging/linux/appimage/host-libs.txt and, for legacy, the
#         desktop stack below (GTK3, GLib, GStreamer); the package declares
#         them as dependencies.
#   full  the AppImage. Only the host-libs.txt libraries come from the host;
#         the desktop stack is bundled.
#
# The closure rule: a library that any host-side library needs, directly or
# through other libraries, is host-side too and never bundled. A bundled copy
# would win over the host's on the RPATH or LD_LIBRARY_PATH and could be older
# than the host library expects (the jammy libsystemd breaks Arch's libmount:
# "version 'LIBSYSTEMD_251' not found").
include_guard(GLOBAL)
cmake_policy(VERSION 3.21)

set(HIKARISUB_HOST_LIBS_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../packaging/linux/appimage/host-libs.txt")

# The legacy desktop stack, host-side in the host profile only. GStreamer
# loads its plugins from the host, and those need the host's core and GLib;
# GTK loads the host's im-modules, pixbuf loaders and theme engines. The X11,
# xkbcommon, GLU, VA-API and VDPAU entries are the rest of the stack the
# packages have always taken from the host.
set(HIKARISUB_HOST_DESKTOP_GLOBS
    "libgtk-3.so*" "libgdk-3.so*" "libgdk_pixbuf-2.0.so*"
    "libpango-1.0.so*" "libpangocairo-1.0.so*" "libpangoft2-1.0.so*"
    "libcairo.so*" "libcairo-gobject.so*"
    "libatk-1.0.so*" "libatk-bridge-2.0.so*" "libharfbuzz.so*"
    "libglib-2.0.so*" "libgobject-2.0.so*" "libgio-2.0.so*"
    "libgmodule-2.0.so*" "libgthread-2.0.so*"
    "libmount.so*" "libblkid.so*" "libpcre2-8.so*"
    "libgst*.so*"
    "libX*.so*" "libxcb*.so*" "libxkbcommon*.so*" "libGLU.so*"
    "libva.so*" "libva-*.so*" "libvdpau.so*")

# Turns one shell glob (*, ? and [...]) into an anchored regular expression.
function(hikarisub_glob_to_regex glob out_var)
    set(regex "")
    string(LENGTH "${glob}" len)
    set(i 0)
    set(in_class FALSE)
    set(specials "." "+" "^" "$" "|" "(" ")" "{" "}" "]" "\\")
    while(i LESS len)
        string(SUBSTRING "${glob}" ${i} 1 c)
        if(in_class)
            string(APPEND regex "${c}")
            if(c STREQUAL "]")
                set(in_class FALSE)
            endif()
        elseif(c STREQUAL "*")
            string(APPEND regex ".*")
        elseif(c STREQUAL "?")
            string(APPEND regex ".")
        elseif(c STREQUAL "[")
            string(APPEND regex "[")
            set(in_class TRUE)
        elseif(c IN_LIST specials)
            string(APPEND regex "\\${c}")
        else()
            string(APPEND regex "${c}")
        endif()
        math(EXPR i "${i} + 1")
    endwhile()
    set(${out_var} "^${regex}$" PARENT_SCOPE)
endfunction()

# Reads host-libs.txt into a list of globs.
function(hikarisub_read_host_globs out_var)
    if(NOT EXISTS "${HIKARISUB_HOST_LIBS_FILE}")
        message(FATAL_ERROR "The host library list is missing: ${HIKARISUB_HOST_LIBS_FILE}")
    endif()
    file(STRINGS "${HIKARISUB_HOST_LIBS_FILE}" lines)
    set(globs)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif()
        list(APPEND globs "${line}")
    endforeach()
    if(NOT globs)
        message(FATAL_ERROR "The host library list is empty: ${HIKARISUB_HOST_LIBS_FILE}")
    endif()
    set(${out_var} "${globs}" PARENT_SCOPE)
endfunction()

# One regex, an alternation of every host-side glob of <profile>.
function(hikarisub_host_regex profile out_var)
    hikarisub_read_host_globs(globs)
    if(profile STREQUAL "host")
        list(APPEND globs ${HIKARISUB_HOST_DESKTOP_GLOBS})
    elseif(NOT profile STREQUAL "full")
        message(FATAL_ERROR "Unknown package profile: ${profile}")
    endif()
    # CMake regular expressions allow few groups, so this is a flat
    # alternation of anchored branches.
    set(parts)
    foreach(glob IN LISTS globs)
        hikarisub_glob_to_regex("${glob}" part)
        list(APPEND parts "${part}")
    endforeach()
    list(JOIN parts "|" joined)
    set(${out_var} "${joined}" PARENT_SCOPE)
endfunction()

# Splits resolved runtime dependencies (absolute paths) into the ones a
# package of <profile> bundles and the host-side ones, applying the closure
# rule: everything the host-side libraries need, resolved here in the build
# environment, is host-side too.
#
#   hikarisub_split_runtime_deps(<profile> <bundle var> <host names var> <path>...)
#
# <bundle var> receives absolute paths, <host names var> file names.
function(hikarisub_split_runtime_deps profile bundle_var host_var)
    hikarisub_host_regex("${profile}" host_regex)
    set(host_paths)
    set(host_names)
    foreach(dep IN LISTS ARGN)
        get_filename_component(name "${dep}" NAME)
        if(name MATCHES "${host_regex}")
            list(APPEND host_paths "${dep}")
            list(APPEND host_names "${name}")
        endif()
    endforeach()
    if(host_paths)
        list(REMOVE_DUPLICATES host_paths)
        file(GET_RUNTIME_DEPENDENCIES
            LIBRARIES ${host_paths}
            RESOLVED_DEPENDENCIES_VAR closure
            UNRESOLVED_DEPENDENCIES_VAR closure_unresolved)
        foreach(dep IN LISTS closure)
            get_filename_component(name "${dep}" NAME)
            list(APPEND host_names "${name}")
        endforeach()
    endif()
    list(REMOVE_DUPLICATES host_names)
    set(bundle)
    foreach(dep IN LISTS ARGN)
        get_filename_component(name "${dep}" NAME)
        if(NOT name IN_LIST host_names)
            list(APPEND bundle "${dep}")
        endif()
    endforeach()
    set(${bundle_var} "${bundle}" PARENT_SCOPE)
    set(${host_var} "${host_names}" PARENT_SCOPE)
endfunction()

# Keeps only the $ORIGIN-relative entries of an ELF file's RPATH or RUNPATH
# and drops the tag when none is left, so no build path ships.
function(hikarisub_normalize_rpath file readelf)
    execute_process(COMMAND "${readelf}" -d "${file}"
        OUTPUT_VARIABLE dynamic RESULT_VARIABLE rc ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "readelf failed on ${file}")
    endif()
    string(REGEX MATCH "\\((RPATH|RUNPATH)\\)[^[]*\\[([^]]*)\\]" found "${dynamic}")
    if(NOT found)
        return()
    endif()
    set(old "${CMAKE_MATCH_2}")
    string(REPLACE ":" ";" entries "${old}")
    set(kept)
    foreach(entry IN LISTS entries)
        if(entry MATCHES "^\\$ORIGIN(/.*)?$" OR entry MATCHES "^\\$\\{ORIGIN\\}(/.*)?$")
            list(APPEND kept "${entry}")
        endif()
    endforeach()
    list(JOIN kept ":" new)
    if(new STREQUAL old)
        return()
    endif()
    if(new STREQUAL "")
        file(RPATH_REMOVE FILE "${file}")
    else()
        file(RPATH_SET FILE "${file}" NEW_RPATH "${new}")
    endif()
endfunction()

# TRUE when <file> starts with the ELF magic.
function(hikarisub_is_elf file out_var)
    set(${out_var} FALSE PARENT_SCOPE)
    if(IS_SYMLINK "${file}" OR IS_DIRECTORY "${file}")
        return()
    endif()
    file(READ "${file}" magic LIMIT 4 HEX)
    if(magic STREQUAL "7f454c46")
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

# The absolute directories of an ELF file's RPATH or RUNPATH: in the build
# tree the executable finds the private wx and media prefixes there, and
# file(GET_RUNTIME_DEPENDENCIES) does not pass a DT_RPATH on to the
# libraries it loads, so callers hand these over as DIRECTORIES.
function(hikarisub_rpath_dirs file out_var)
    find_program(HIKARISUB_READELF readelf REQUIRED)
    execute_process(COMMAND "${HIKARISUB_READELF}" -d "${file}"
        OUTPUT_VARIABLE dynamic ERROR_QUIET)
    set(dirs)
    string(REGEX MATCH "\\((RPATH|RUNPATH)\\)[^[]*\\[([^]]*)\\]" found "${dynamic}")
    if(found)
        string(REPLACE ":" ";" entries "${CMAKE_MATCH_2}")
        foreach(entry IN LISTS entries)
            if(IS_ABSOLUTE "${entry}" AND IS_DIRECTORY "${entry}")
                list(APPEND dirs "${entry}")
            endif()
        endforeach()
    endif()
    set(${out_var} "${dirs}" PARENT_SCOPE)
endfunction()
