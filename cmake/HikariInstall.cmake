# Install rules: the one relocatable app tree that every package is made of
# (installers spec 5.2). The Qt build otherwise runs from the build tree.
#
#   <prefix>/hikarisub                    the app (HikariSub.exe on Windows)
#   <prefix>/hikari-media-helper          the helpers, beside the app, where
#   <prefix>/hikari-lua-helper            application.cpp looks for them
#   <prefix>/harper-ls                    GR1: the grammar checker (HikariHarper.cmake)
#   <prefix>/Automation/automation/...    the bundled scripts and native modules
#   <prefix>/Automation/{log,autosave,temp}
#   <prefix>/Dictionary/                  the bundled spell-check dictionaries
#   <prefix>/Dictionary/unidic-lite/      the furigana suggestions' MeCab dictionary
#   <prefix>/licenses/                    the bundled libraries' and data's licences
#   <prefix>/lib/ plugins/ qml/ translations/ qt.conf   (Qt's deploy API)
#   <prefix>/LICENSE
#
# Every path is relative to the executable, which the app finds through
# QCoreApplication::applicationDirPath (spec 1.6), so the tree works from
# /opt/hikarisub, an AppImage or an extracted archive alike. The interface
# translations are compiled into the executable (i18n/), so no catalog files
# are installed.
include_guard(GLOBAL)

set(HIKARI_INSTALL_LIBDIR lib)
set(HIKARI_INSTALL_PLUGINSDIR plugins)
set(HIKARI_INSTALL_QMLDIR qml)

# Only $ORIGIN-relative run paths, never a build path (spec 5.2, RPATH).
if(NOT WIN32)
    set_target_properties(hikarisub hikari_media_helper hikari_lua_helper PROPERTIES
        INSTALL_RPATH "$ORIGIN/${HIKARI_INSTALL_LIBDIR}" INSTALL_RPATH_USE_LINK_PATH OFF)
endif()

install(TARGETS hikarisub hikari_media_helper hikari_lua_helper
    RUNTIME DESTINATION . COMPONENT hikari_app)

# The automation tree as hikari_automation_stage lays it out in the build:
# the sources plus DependencyControl's native modules (and SubInspector on
# Linux), each installed as a target so CMake drops its build run path.
install(DIRECTORY "${PROJECT_SOURCE_DIR}/Automation/automation" DESTINATION Automation COMPONENT hikari_app)
install(DIRECTORY DESTINATION Automation/log COMPONENT hikari_app)
install(DIRECTORY DESTINATION Automation/autosave COMPONENT hikari_app)
install(DIRECTORY DESTINATION Automation/temp COMPONENT hikari_app)
foreach(_hikari_module IN ITEMS badmutex:BM/BadMutex precisetimer:PT/PreciseTimer downloadmanager:DM/DownloadManager)
    string(REPLACE ":" ";" _hikari_module "${_hikari_module}")
    list(GET _hikari_module 0 _hikari_name)
    list(GET _hikari_module 1 _hikari_dir)
    install(TARGETS hikari_depctrl_${_hikari_name}
        LIBRARY DESTINATION "Automation/automation/Include/${_hikari_dir}" COMPONENT hikari_app
        RUNTIME DESTINATION "Automation/automation/Include/${_hikari_dir}" COMPONENT hikari_app)
endforeach()
if(TARGET hikari_subinspector)
    install(TARGETS hikari_subinspector
        LIBRARY DESTINATION Automation/automation/Include/SubInspector/Inspector COMPONENT hikari_app)
endif()

# Extra dictionaries: the folder's *.aff/*.dic pairs, when one is given (the
# packaging floor passes the distribution's). Installed first, so a pinned
# file below replaces one of the same name.
set(HIKARI_INSTALL_DICTIONARY_DIR "" CACHE PATH "Folder whose *.aff and *.dic files are installed into Dictionary/")
if(HIKARI_INSTALL_DICTIONARY_DIR)
    install(DIRECTORY "${HIKARI_INSTALL_DICTIONARY_DIR}/" DESTINATION Dictionary COMPONENT hikari_app
        FILES_MATCHING PATTERN "*.aff" PATTERN "*.dic")
endif()
# DK1 (#473): the pinned dictionaries (HikariDictionaries.cmake), the same on
# every platform, and their notices. The app reads no system dictionaries.
install(DIRECTORY "${HIKARI_BUNDLED_DICTIONARIES}/Dictionary/" DESTINATION Dictionary COMPONENT hikari_app)
install(DIRECTORY "${HIKARI_BUNDLED_DICTIONARIES}/licenses/dictionaries/" DESTINATION licenses/dictionaries
    COMPONENT hikari_app)

# KA4 (#279): UniDic 2.1.2 for MeCab, with its licence texts. MeCab and
# UniDic are BSD-licensed here, whose notice every binary copy carries.
install(DIRECTORY "${HIKARI_UNIDIC_DIR}/" DESTINATION Dictionary/unidic-lite COMPONENT hikari_app)
install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/mecab/copyright" DESTINATION licenses/mecab
    RENAME LICENSE COMPONENT hikari_app)
install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/hikari-unidic-lite/copyright"
    DESTINATION licenses/unidic RENAME LICENSE COMPONENT hikari_app)

# GR1 (#364): harper-ls, upstream's build, with Harper's Apache-2.0 licence and
# the notices of the crates linked into it.
install(PROGRAMS "${HIKARI_HARPER_LS}" DESTINATION . COMPONENT hikari_app)
install(FILES "${HIKARI_HARPER_LS_LICENSE}" DESTINATION licenses/harper RENAME LICENSE COMPONENT hikari_app)
install(FILES "${PROJECT_SOURCE_DIR}/packaging/licenses/harper/THIRD-PARTY.txt" DESTINATION licenses/harper
    COMPONENT hikari_app)

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION . COMPONENT hikari_app)

# UD1 (#362): the manifest of the bundled scripts, modules and dictionaries
# installed above, which each start seeds into the user folder.
install(CODE "include(\"${PROJECT_SOURCE_DIR}/cmake/HikariBundledManifest.cmake\")
hikari_write_bundled_manifest(\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")" COMPONENT hikari_app)

# Qt's deploy API: the Qt libraries, plugins and QML modules the app uses,
# with a qt.conf, into lib/, plugins/ and qml/. The platforms are xcb (Qt's
# default), wayland and offscreen. Plugins that would pull a library the
# packages do not provide or need are left out: the GTK theme, the EGLFS
# device integrations, the evdev and TUIO input plugins, print support, the
# GLib network monitor, and the QML debugging services. The plugin options
# take file name stems (qgtk3 for libqgtk3.so) and plugin folder names.
set(_hikari_included_plugins qwayland qoffscreen)
set(_hikari_excluded_plugins qgtk3 qglib)
set(_hikari_excluded_plugin_types egldeviceintegrations generic printsupport qmltooling)
string(JOIN " " _hikari_included_plugins ${_hikari_included_plugins})
string(JOIN " " _hikari_excluded_plugins ${_hikari_excluded_plugins})
string(JOIN " " _hikari_excluded_plugin_types ${_hikari_excluded_plugin_types})
# Qt's Linux deploy tool finds the Qt libraries through the installed
# executable's run path, which is only $ORIGIN/lib by then: the script lends
# a copy of it the Qt library folder for the deploy and puts the installed
# file back afterwards. CR1 (#452): patchelf's longer run path relays the
# file out, which objcopy then breaks when make-packages.sh splits the debug
# info out of it.
set(_hikari_lend_qt_runpath "")
set(_hikari_restore_runpath "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(HIKARI_PATCHELF patchelf)
    if(NOT HIKARI_PATCHELF)
        set(HIKARI_PATCHELF patchelf) # the install fails with a clear message without it
    endif()
    set(_hikari_installed "\${QT_DEPLOY_PREFIX}/$<TARGET_FILE_NAME:hikarisub>")
    set(_hikari_lend_qt_runpath "file(RENAME \"${_hikari_installed}\" \"${_hikari_installed}.installed\")
file(COPY_FILE \"${_hikari_installed}.installed\" \"${_hikari_installed}\")
execute_process(COMMAND \"${HIKARI_PATCHELF}\" --set-rpath
    \"$ORIGIN/${HIKARI_INSTALL_LIBDIR}:$<TARGET_FILE_DIR:Qt6::Core>\" \"${_hikari_installed}\"
    COMMAND_ERROR_IS_FATAL ANY)")
    set(_hikari_restore_runpath "file(RENAME \"${_hikari_installed}.installed\" \"${_hikari_installed}\")")
endif()
# The qt6_ function, not the qt_ macro: the macro would expand the script's
# own variable references before writing it.
qt6_generate_deploy_script(TARGET hikarisub OUTPUT_SCRIPT _hikari_deploy_script CONTENT "
set(QT_DEPLOY_BIN_DIR .)
set(QT_DEPLOY_LIB_DIR ${HIKARI_INSTALL_LIBDIR})
set(QT_DEPLOY_PLUGINS_DIR ${HIKARI_INSTALL_PLUGINSDIR})
set(QT_DEPLOY_QML_DIR ${HIKARI_INSTALL_QMLDIR})
set(QT_DEPLOY_TRANSLATIONS_DIR translations)
${_hikari_lend_qt_runpath}
qt_deploy_qml_imports(TARGET hikarisub PLUGINS_FOUND plugins_found)
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE_NAME:hikarisub>\"
    ADDITIONAL_MODULES \${plugins_found}
    BIN_DIR .
    LIB_DIR ${HIKARI_INSTALL_LIBDIR}
    PLUGINS_DIR ${HIKARI_INSTALL_PLUGINSDIR}
    QML_DIR ${HIKARI_INSTALL_QMLDIR}
    GENERATE_QT_CONF
    INCLUDE_PLUGINS ${_hikari_included_plugins}
    EXCLUDE_PLUGINS ${_hikari_excluded_plugins}
    EXCLUDE_PLUGIN_TYPES ${_hikari_excluded_plugin_types})
${_hikari_restore_runpath}
")
install(SCRIPT "${_hikari_deploy_script}" COMPONENT hikari_app)

# The GCC runtime pieces the app links that Debian-family desktops do not
# always install (spec 5.1): bundled in every Linux profile.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(CODE "
foreach(_soname libatomic.so.1 libquadmath.so.0)
    execute_process(COMMAND \"${CMAKE_C_COMPILER}\" -print-file-name=\${_soname}
        OUTPUT_VARIABLE _path OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT IS_ABSOLUTE \"\${_path}\" OR NOT EXISTS \"\${_path}\")
        message(FATAL_ERROR \"Cannot find \${_soname} to bundle (\${_path})\")
    endif()
    file(INSTALL \"\${_path}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${HIKARI_INSTALL_LIBDIR}\" FOLLOW_SYMLINK_CHAIN)
endforeach()
" COMPONENT hikari_app)
endif()

# The Linux packages made from this tree (the linux-floor-x64-release preset).
option(HIKARI_PACKAGE_LINUX "Add the hikari_linux_packages target: tar.gz, AppImage, deb and rpm" OFF)
if(HIKARI_PACKAGE_LINUX)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "HIKARI_PACKAGE_LINUX: the Linux packages build on Linux only")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/HikariPackageLinux.cmake")
endif()
