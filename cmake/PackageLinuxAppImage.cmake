# Builds HikariSub-<version>-linux-x86_64.AppImage from the staged host-profile
# tree (cmake/PackageLinuxRuntime.cmake) plus the full-profile extras: GTK3,
# GLib and GStreamer with its plugins, all from the build environment, so the
# AppImage runs on any distribution whose glibc is new enough. Only the
# libraries in packaging/linux/appimage/host-libs.txt, and whatever those
# need, come from the host. Run in script mode by the hikarisub_appimage
# target.
#
# AppDir layout (installers spec, section 5.3):
#   AppRun, io.github.altqx.HikariSub.desktop, hikarisub.png, .DirIcon
#   usr/share/metainfo/io.github.altqx.HikariSub.appdata.xml
#   usr/share/applications/io.github.altqx.HikariSub.desktop
#   usr/lib/hikarisub/          the app tree and package-kind.txt = appimage
#   usr/lib/hikarisub-runtime/  lib/, lib-fallback/, gstreamer-1.0/, libexec/,
#                               gtk-3.0/, gdk-pixbuf-2.0/, gio/modules (empty),
#                               share/
cmake_minimum_required(VERSION 3.21)
foreach(required_var IN ITEMS
        SOURCE_DIR BINARY_DIR PACKAGE_ROOT VERSION APPIMAGETOOL
        APPIMAGE_RUNTIME OUTPUT PKG_CONFIG_EXECUTABLE)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()
foreach(tool IN ITEMS "${APPIMAGETOOL}" "${APPIMAGE_RUNTIME}")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR "Missing AppImage tool ${tool}; fetch it with packaging/linux/fetch-tool.sh")
    endif()
endforeach()
if(NOT EXISTS "${PACKAGE_ROOT}/hikarisub")
    message(FATAL_ERROR "The staged package tree is missing: ${PACKAGE_ROOT}")
endif()
if(NOT DEFINED READELF_EXECUTABLE OR READELF_EXECUTABLE STREQUAL "")
    find_program(READELF_EXECUTABLE readelf REQUIRED)
endif()
include("${SOURCE_DIR}/cmake/LinuxHostLibs.cmake")

set(app_id io.github.altqx.HikariSub)
set(work "${BINARY_DIR}/appimage")
set(appdir "${work}/AppDir")
set(app "${appdir}/usr/lib/hikarisub")
set(rt "${appdir}/usr/lib/hikarisub-runtime")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${app}" "${rt}/lib" "${rt}/libexec" "${rt}/gstreamer-1.0"
     "${rt}/gtk-3.0/3.0.0/immodules" "${rt}/gdk-pixbuf-2.0/2.10.0/loaders"
     "${rt}/gio/modules" "${rt}/share/glib-2.0/schemas" "${rt}/share/icons"
     "${appdir}/usr/share/applications" "${appdir}/usr/share/metainfo")

# The app tree, as the native packages install it under /opt/hikarisub: no
# desktop files (the AppDir carries its own), no portable installer, and no
# SHA256SUMS of files that are not there.
file(COPY "${PACKAGE_ROOT}/" DESTINATION "${app}")
file(REMOVE_RECURSE "${app}/share" "${app}/install-desktop-integration.sh"
     "${app}/SHA256SUMS")
file(WRITE "${app}/package-kind.txt" "appimage\n")

file(COPY_FILE "${PACKAGE_ROOT}/share/applications/${app_id}.desktop"
               "${appdir}/${app_id}.desktop")
file(COPY_FILE "${PACKAGE_ROOT}/share/applications/${app_id}.desktop"
               "${appdir}/usr/share/applications/${app_id}.desktop")
# appimagetool looks for <desktop id>.appdata.xml; the content is the same.
file(COPY_FILE "${PACKAGE_ROOT}/share/metainfo/${app_id}.metainfo.xml"
               "${appdir}/usr/share/metainfo/${app_id}.appdata.xml")
file(COPY_FILE "${PACKAGE_ROOT}/share/icons/hicolor/256x256/apps/hikarisub.png"
               "${appdir}/hikarisub.png")
file(CREATE_LINK "hikarisub.png" "${appdir}/.DirIcon" SYMBOLIC)

function(pkg_variable module variable out_var)
    execute_process(
        COMMAND "${PKG_CONFIG_EXECUTABLE}" "--variable=${variable}" "${module}"
        OUTPUT_VARIABLE value OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0 OR value STREQUAL "")
        message(FATAL_ERROR "pkg-config has no ${variable} for ${module}")
    endif()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()
pkg_variable(gstreamer-1.0 pluginsdir gst_plugins_dir)
pkg_variable(gstreamer-1.0 pluginscannerdir gst_scanner_dir)
pkg_variable(gdk-pixbuf-2.0 gdk_pixbuf_moduledir pixbuf_loaders_dir)
pkg_variable(gdk-pixbuf-2.0 gdk_pixbuf_query_loaders pixbuf_query_loaders)
pkg_variable(gtk+-3.0 libdir gtk_libdir)
pkg_variable(gtk+-3.0 gtk_binary_version gtk_binary_version)
pkg_variable(gio-2.0 glib_compile_schemas glib_compile_schemas)

# GStreamer: the elements legacy uses (playbin, appsrc, appsink,
# audioconvert, audioresample, autoaudiosink, videoconvert) and what playback
# needs for common files: demuxers, parsers and the libav decoders.
set(gst_plugins
    coreelements app audioconvert audioresample videoconvert videoscale
    playback typefindfunctions autodetect alsa pulseaudio volume
    matroska isomp4 avi wavparse audioparsers videoparsersbad
    libav ogg vorbis opus pbtypes)
set(runtime_modules)
foreach(plugin IN LISTS gst_plugins)
    set(plugin_file "${gst_plugins_dir}/libgst${plugin}.so")
    if(NOT EXISTS "${plugin_file}")
        message(FATAL_ERROR "GStreamer plugin ${plugin} is not installed: ${plugin_file}")
    endif()
    file(COPY_FILE "${plugin_file}" "${rt}/gstreamer-1.0/libgst${plugin}.so")
    list(APPEND runtime_modules "${rt}/gstreamer-1.0/libgst${plugin}.so")
endforeach()
# Debian and Ubuntu move the scanner out of the pkg-config pluginscannerdir.
find_file(GST_PLUGIN_SCANNER gst-plugin-scanner
    PATHS "${gst_scanner_dir}" "${gst_plugins_dir}/../gstreamer1.0/gstreamer-1.0"
    NO_DEFAULT_PATH REQUIRED)
file(COPY_FILE "${GST_PLUGIN_SCANNER}" "${rt}/libexec/gst-plugin-scanner")

# GTK im-modules (GTK's own, ibus and fcitx5) and the gdk-pixbuf loaders.
file(GLOB immodules "${gtk_libdir}/gtk-3.0/${gtk_binary_version}/immodules/*.so")
foreach(name IN ITEMS im-ibus.so im-fcitx5.so)
    if(NOT EXISTS "${gtk_libdir}/gtk-3.0/${gtk_binary_version}/immodules/${name}")
        message(FATAL_ERROR "The GTK input method module ${name} is not installed")
    endif()
endforeach()
foreach(module IN LISTS immodules)
    get_filename_component(name "${module}" NAME)
    file(COPY_FILE "${module}" "${rt}/gtk-3.0/3.0.0/immodules/${name}")
    list(APPEND runtime_modules "${rt}/gtk-3.0/3.0.0/immodules/${name}")
endforeach()
file(GLOB loaders "${pixbuf_loaders_dir}/*.so")
foreach(loader IN LISTS loaders)
    get_filename_component(name "${loader}" NAME)
    file(COPY_FILE "${loader}" "${rt}/gdk-pixbuf-2.0/2.10.0/loaders/${name}")
    list(APPEND runtime_modules "${rt}/gdk-pixbuf-2.0/2.10.0/loaders/${name}")
endforeach()

# Every library the app, the scanner and the modules need, split by the full
# profile: host-libs.txt and its closure stay with the host, the rest is
# bundled in usr/lib/hikarisub-runtime/lib. A library the app tree already
# carries is linked there rather than copied twice.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${app}/hikarisub" "${rt}/libexec/gst-plugin-scanner"
    MODULES ${runtime_modules}
    DIRECTORIES "${app}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflict)
if(unresolved)
    message(FATAL_ERROR "Unresolved AppImage runtime dependencies: ${unresolved}")
endif()
# A library the app tree carries resolves there for the app and to the build
# system's copy for a module (jammy's FFmpeg 4.4 for gst-libav needs libass
# too). They are the same library; the app tree's copy is used.
foreach(name IN LISTS conflict_FILENAMES)
    set(chosen "")
    foreach(candidate IN LISTS conflict_${name})
        if(candidate MATCHES "^${app}/")
            set(chosen "${candidate}")
        endif()
    endforeach()
    if(chosen STREQUAL "")
        message(FATAL_ERROR "Conflicting copies of ${name}: ${conflict_${name}}")
    endif()
    list(APPEND resolved "${chosen}")
endforeach()
hikarisub_split_runtime_deps(full bundle host_names ${resolved})
file(REAL_PATH "${app}" app_real)
set(copied 0)
set(linked 0)
foreach(dep IN LISTS bundle)
    get_filename_component(name "${dep}" NAME)
    get_filename_component(dir "${dep}" DIRECTORY)
    file(REAL_PATH "${dir}" dir_real)
    if(dir_real STREQUAL app_real OR EXISTS "${app}/${name}")
        if(NOT EXISTS "${rt}/lib/${name}")
            file(CREATE_LINK "../../hikarisub/${name}" "${rt}/lib/${name}" SYMBOLIC)
            math(EXPR linked "${linked} + 1")
        endif()
    elseif(NOT EXISTS "${rt}/lib/${name}")
        file(COPY_FILE "${dep}" "${rt}/lib/${name}")
        math(EXPR copied "${copied} + 1")
    endif()
endforeach()
message(STATUS "AppImage runtime: ${copied} libraries bundled, ${linked} shared with the app tree")

# Fallbacks for the closure rule. libXdmcp needs libbsd (and libbsd libmd)
# on Debian and Ubuntu only, so the rule leaves them to the host, but
# Fedora, openSUSE and Arch do not have them, and jammy's gst-libav needs
# them through libavfilter and libzmq. They go to lib-fallback, which is not
# on LD_LIBRARY_PATH: only the bundled files that need them name it in their
# RUNPATH. The host's copy still wins wherever a host library has loaded it
# first, and nothing else can pick up the bundled one.
set(fallback_regex "^libbsd\\.so\\.[0-9]+$|^libmd\\.so\\.[0-9]+$")
set(fallback_names)
file(MAKE_DIRECTORY "${rt}/lib-fallback")
foreach(dep IN LISTS resolved)
    get_filename_component(name "${dep}" NAME)
    if(name MATCHES "${fallback_regex}" AND name IN_LIST host_names
       AND NOT EXISTS "${rt}/lib-fallback/${name}")
        file(COPY_FILE "${dep}" "${rt}/lib-fallback/${name}")
        list(APPEND fallback_names "${name}")
    endif()
endforeach()
if(fallback_names)
    find_program(PATCHELF_EXECUTABLE patchelf REQUIRED)
    file(GLOB_RECURSE rt_files LIST_DIRECTORIES FALSE "${rt}/*")
    foreach(candidate IN LISTS rt_files)
        hikarisub_is_elf("${candidate}" candidate_is_elf)
        if(NOT candidate_is_elf)
            continue()
        endif()
        execute_process(COMMAND "${READELF_EXECUTABLE}" -d "${candidate}"
            OUTPUT_VARIABLE dynamic ERROR_QUIET)
        set(needs_fallback FALSE)
        foreach(name IN LISTS fallback_names)
            string(FIND "${dynamic}" "[${name}]" at)
            if(NOT at EQUAL -1)
                set(needs_fallback TRUE)
            endif()
        endforeach()
        if(needs_fallback)
            get_filename_component(candidate_dir "${candidate}" DIRECTORY)
            file(RELATIVE_PATH rel "${candidate_dir}" "${rt}/lib-fallback")
            if(rel STREQUAL "")
                set(entry "$ORIGIN")
            else()
                set(entry "$ORIGIN/${rel}")
            endif()
            execute_process(
                COMMAND "${PATCHELF_EXECUTABLE}" --add-rpath "${entry}" "${candidate}"
                RESULT_VARIABLE rc)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "patchelf could not add ${entry} to ${candidate}")
            endif()
        endif()
    endforeach()
    message(STATUS "AppImage runtime: fallbacks ${fallback_names}")
endif()

# GSettings schemas (the GTK file chooser needs them) and icons.
file(GLOB schema_files "/usr/share/glib-2.0/schemas/*.gschema.xml"
                       "/usr/share/glib-2.0/schemas/*.enums.xml"
                       "/usr/share/glib-2.0/schemas/*.gschema.override")
file(MAKE_DIRECTORY "${work}/schemas")
foreach(schema IN LISTS schema_files)
    get_filename_component(name "${schema}" NAME)
    file(COPY_FILE "${schema}" "${work}/schemas/${name}")
endforeach()
execute_process(
    COMMAND "${glib_compile_schemas}" --strict
            --targetdir "${rt}/share/glib-2.0/schemas" "${work}/schemas"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS "${rt}/share/glib-2.0/schemas/gschemas.compiled")
    message(FATAL_ERROR "glib-compile-schemas failed")
endif()
foreach(theme IN ITEMS Adwaita hicolor)
    if(NOT IS_DIRECTORY "/usr/share/icons/${theme}")
        message(FATAL_ERROR "The ${theme} icon theme is not installed")
    endif()
endforeach()
file(COPY "/usr/share/icons/Adwaita" DESTINATION "${rt}/share/icons")
file(MAKE_DIRECTORY "${rt}/share/icons/hicolor")
file(COPY_FILE "/usr/share/icons/hicolor/index.theme" "${rt}/share/icons/hicolor/index.theme")

# The loader and im-module caches, with @APPDIR@ for AppRun to fill in.
function(write_cache_template out)
    set(command ${ARGN})
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
                "GDK_PIXBUF_MODULEDIR=${rt}/gdk-pixbuf-2.0/2.10.0/loaders"
                "LD_LIBRARY_PATH=${rt}/lib"
                ${command}
        OUTPUT_VARIABLE cache RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0 OR cache STREQUAL "")
        message(FATAL_ERROR "Could not generate ${out}")
    endif()
    string(REPLACE "${appdir}" "@APPDIR@" cache "${cache}")
    if(cache MATCHES "\"/usr/lib")
        message(FATAL_ERROR "${out} names a module outside the AppDir")
    endif()
    file(WRITE "${out}" "${cache}")
endfunction()
write_cache_template("${rt}/loaders.cache.in" "${pixbuf_query_loaders}")
file(GLOB bundled_immodules "${rt}/gtk-3.0/3.0.0/immodules/*.so")
find_program(GTK_QUERY_IMMODULES
    NAMES gtk-query-immodules-3.0
    PATHS "${gtk_libdir}/libgtk-3-0" REQUIRED)
write_cache_template("${rt}/immodules.cache.in" "${GTK_QUERY_IMMODULES}" ${bundled_immodules})

# No build path and no host library in the AppDir.
hikarisub_host_regex(full host_list_regex)
file(GLOB_RECURSE appdir_files LIST_DIRECTORIES FALSE "${appdir}/*")
foreach(candidate IN LISTS appdir_files)
    get_filename_component(candidate_name "${candidate}" NAME)
    if(candidate_name MATCHES "${host_list_regex}")
        message(FATAL_ERROR "A host library is in the AppDir: ${candidate}")
    endif()
    hikarisub_is_elf("${candidate}" candidate_is_elf)
    if(candidate_is_elf)
        hikarisub_normalize_rpath("${candidate}" "${READELF_EXECUTABLE}")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/packaging/linux/appimage/AppRun" apprun)
string(REPLACE "@VERSION@" "${VERSION}" apprun "${apprun}")
file(WRITE "${appdir}/AppRun" "${apprun}")
file(CHMOD "${appdir}/AppRun"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
get_filename_component(output_name "${OUTPUT}" NAME)
file(MAKE_DIRECTORY "${output_dir}")
file(REMOVE "${OUTPUT}" "${OUTPUT}.zsync")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
            ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "VERSION=${VERSION}"
            "${APPIMAGETOOL}"
            --runtime-file "${APPIMAGE_RUNTIME}"
            --updateinformation "gh-releases-zsync|altqx|hikari|latest|HikariSub-*-linux-x86_64.AppImage.zsync"
            "${appdir}" "${output_name}"
    WORKING_DIRECTORY "${output_dir}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "appimagetool failed")
endif()
if(NOT EXISTS "${OUTPUT}.zsync")
    message(FATAL_ERROR "appimagetool wrote no ${output_name}.zsync; is zsyncmake installed?")
endif()
file(SHA256 "${OUTPUT}" hash)
message(STATUS "Created ${OUTPUT}")
message(STATUS "SHA-256 ${hash}")
