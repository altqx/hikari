cmake_minimum_required(VERSION 3.21)
project(HikariSubLinux LANGUAGES C CXX)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)
find_package(PkgConfig REQUIRED)
# Windows builds the pinned Thirdparty/wxWidgets 3.3.4 submodule. Use the same
# source/version on Linux instead of silently accepting the host's wxGTK 3.2.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(HIKARISUB_WX_DEFAULT_CONFIG "${CMAKE_SOURCE_DIR}/build-wx-linux-debug/prefix/bin/wx-config")
else()
    set(HIKARISUB_WX_DEFAULT_CONFIG "${CMAKE_SOURCE_DIR}/build-wx-linux/prefix/bin/wx-config")
endif()
set(HIKARISUB_WX_CONFIG "${HIKARISUB_WX_DEFAULT_CONFIG}"
    CACHE FILEPATH "wx-config from the pinned wxWidgets 3.3.4 Linux build")
if(NOT EXISTS "${HIKARISUB_WX_CONFIG}")
    message(FATAL_ERROR
        "Pinned wxWidgets 3.3.4 is missing at ${HIKARISUB_WX_CONFIG}. "
        "Run Thirdparty/build-wxwidgets-linux.sh (with --debug for Debug) first, or set HIKARISUB_WX_CONFIG "
        "to a wx-config for wxWidgets 3.3.4.")
endif()
execute_process(
    COMMAND "${HIKARISUB_WX_CONFIG}" --prefix
    OUTPUT_VARIABLE HIKARISUB_WX_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE HIKARISUB_WX_CONFIG_RESULT)
if(NOT HIKARISUB_WX_CONFIG_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not query ${HIKARISUB_WX_CONFIG} for its wxWidgets prefix.")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(HIKARISUB_WX_EXPORT_CONFIG debug)
else()
    set(HIKARISUB_WX_EXPORT_CONFIG release)
endif()
if(NOT EXISTS "${HIKARISUB_WX_PREFIX}/lib/cmake/wxWidgets-3.3/wxWidgetsTargets-${HIKARISUB_WX_EXPORT_CONFIG}.cmake")
    message(FATAL_ERROR
        "${HIKARISUB_WX_CONFIG} does not point to the pinned wxWidgets 3.3.4 ${HIKARISUB_WX_EXPORT_CONFIG} build. "
        "Run Thirdparty/build-wxwidgets-linux.sh (with --debug for Debug) first.")
endif()
set(wxWidgets_CONFIG_EXECUTABLE "${HIKARISUB_WX_CONFIG}" CACHE FILEPATH
    "wx-config selected for HikariSub" FORCE)
find_package(wxWidgets 3.3.4 EXACT REQUIRED COMPONENTS core base adv aui html xml gl stc net)
include(${wxWidgets_USE_FILE})
# FFmpeg and FFMS2 from a private prefix win over pkg-config's system paths.
# The release build (packaging/linux/floor) builds FFmpeg 7.1 and the
# Thirdparty/ffms2 fork there, so the library matches the fork's headers that
# Thirdparty/Build/FFMS2/indexing_additional.cpp is compiled against.
set(HIKARISUB_MEDIA_PREFIX "" CACHE PATH
    "Prefix holding FFmpeg and FFMS2 builds that win over the system's (lib/pkgconfig)")
if(HIKARISUB_MEDIA_PREFIX)
    if(NOT EXISTS "${HIKARISUB_MEDIA_PREFIX}/lib/pkgconfig/ffms2.pc")
        message(FATAL_ERROR "HIKARISUB_MEDIA_PREFIX has no lib/pkgconfig/ffms2.pc: ${HIKARISUB_MEDIA_PREFIX}")
    endif()
    if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
        set(ENV{PKG_CONFIG_PATH} "${HIKARISUB_MEDIA_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "${HIKARISUB_MEDIA_PREFIX}/lib/pkgconfig")
    endif()
endif()
pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)
pkg_check_modules(FFMS2 REQUIRED IMPORTED_TARGET ffms2)
pkg_check_modules(HUNSPELL REQUIRED IMPORTED_TARGET hunspell)
pkg_check_modules(UCHARDET REQUIRED IMPORTED_TARGET uchardet)
pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(ICU_UC REQUIRED IMPORTED_TARGET icu-uc)
pkg_check_modules(ICU_I18N REQUIRED IMPORTED_TARGET icu-i18n)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
# GStreamer backs the Linux general-playback renderer (RendererGStreamer): playbin
# decodes/plays the file (video + audio in one pipeline), an appsink delivers BGRA
# frames the app composites (libass subtitles + progress bar) and presents through
# the shared wx paint path, and gstreamer-audio provides linear volume.
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0 gstreamer-app-1.0)
# The video window paints frames with cairo, which GTK brings and the package
# takes from the host like the rest of the GTK stack.
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
# GDK says whether the colour picker's pointer grab was granted, which wxGTK's
# CaptureMouse does not, and Xlib reads the screen for its eyedropper
# (DropperPointer.cpp).
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0 x11)
# LuaJIT is the vendored one, built as the Windows build builds it (Lua 5.2
# extensions on, string.buffer off), not the distribution's: see the module.
include(${CMAKE_SOURCE_DIR}/cmake/VendoredLuaJIT.cmake)
# DependencyControl's native modules, written where requireffi looks for them
# next to the executable (as the Windows build writes its DLLs) and packaged.
include(${CMAKE_SOURCE_DIR}/cmake/DependencyControlModules.cmake)
set(HIKARISUB_DEPCTRL_DIR "${CMAKE_BINARY_DIR}/Automation/automation/Include")
hikarisub_add_dependencycontrol_modules("${HIKARISUB_DEPCTRL_DIR}")
string(REPLACE ";" "|" HIKARISUB_DEPCTRL_MODULE_LIST "${HIKARISUB_DEPCTRL_MODULES}")

# Link Boost statically on Linux so the built binary does not depend on the
# exact distro Boost shared-library SONAME, e.g. libboost_filesystem.so.1.83.0.
# Other system libraries remain dynamically linked through pkg-config/wxWidgets.
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS filesystem locale regex OPTIONAL_COMPONENTS system thread)
find_program(MSGFMT_EXECUTABLE msgfmt REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(TAR_EXECUTABLE tar REQUIRED)
find_program(GZIP_EXECUTABLE gzip REQUIRED)

# Thirdparty/ffms2 is a git submodule (HikariSub's ffms2 fork).  The Linux build
# does not link it -- it links the distribution's libffms2 -- but it does
# compile Thirdparty/Build/FFMS2/indexing_additional.cpp against ffms2's private
# headers, so the checkout has to be there.  Fail with something actionable
# rather than a pile of missing-include errors.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/Thirdparty/ffms2/src/core/indexing.h")
    message(FATAL_ERROR
        "Thirdparty/ffms2 is empty -- the git submodules have not been checked out.\n"
        "Run:  git submodule update --init --recursive")
endif()

# ASSFoundation measures rendered lines through SubInspector, which
# Automation/automation/Include/SubInspector/Inspector.moon loads with requireffi
# from Include/SubInspector/Inspector/.  The automation tree carries only the
# Windows SubInspector.dll, so Linux builds libSubInspector.so from the pinned
# Thirdparty/SubInspector submodule against the libass the app links, and
# cmake/PackageLinuxRuntime.cmake installs it beside that DLL.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/Thirdparty/SubInspector/src/SubInspector.c")
    message(FATAL_ERROR
        "Thirdparty/SubInspector is empty -- the git submodules have not been checked out.\n"
        "Run:  git submodule update --init --recursive")
endif()
add_library(subinspector SHARED Thirdparty/SubInspector/src/SubInspector.c)
# NO_ZLIB selects SubInspector's own crc32, as the shipped Windows DLL was built.
target_compile_definitions(subinspector PRIVATE NO_ZLIB)
target_link_libraries(subinspector PRIVATE PkgConfig::LIBASS)
# The package copies this build-tree file to
# Automation/automation/Include/SubInspector/Inspector/, five levels below the
# executable and the libass it bundles there; old-style DT_RPATH, as the
# executable uses, so that bundled libass is the one it resolves.
set_target_properties(subinspector PROPERTIES
    OUTPUT_NAME SubInspector
    C_STANDARD 99
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/subinspector"
    BUILD_RPATH "$ORIGIN/../../../../.."
    # Not wxWidgets' link directory, which would otherwise land in its RPATH.
    LINK_DIRECTORIES ""
)
target_link_options(subinspector PRIVATE "-Wl,--disable-new-dtags")

add_compile_definitions(_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS HAVE_DLL_EXPORT LINUX_BUILD HIKARISUB_LINUX_BUILD)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-include> $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/HikariSub/platform.h> -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
# Test builds only: the update feed and package kind from the environment.
option(HIKARISUB_UPDATE_TEST_FEED "Read HIKARISUB_UPDATE_FEED and HIKARISUB_PACKAGE_KIND" OFF)
if(HIKARISUB_UPDATE_TEST_FEED)
    add_compile_definitions(HIKARISUB_UPDATE_TEST_FEED)
endif()
add_executable(hikarisub
    HikariSub/Demux.cpp
    HikariSub/DialogueTextEditor.cpp
    HikariSub/ModificationChecker.cpp
    HikariSub/HikariSubFrame.cpp
    HikariSub/Notebook.cpp
    HikariSub/AutomationDummyLoader.cpp
    HikariSub/TagFindReplace.cpp
    HikariSub/VisualAllTags.cpp
    HikariSub/AudioBox.cpp
    HikariSub/AudioDisplay.cpp
    HikariSub/AudioDeviceEnumeration.cpp
    HikariSub/AudioPlayerDSound.cpp
    HikariSub/AudioSpectrum.cpp
    HikariSub/Automation.cpp
    HikariSub/AutomationDialog.cpp
    HikariSub/AutomationFileSystem.cpp
    HikariSub/AutomationHotkeysDialog.cpp
    HikariSub/AutomationLPeg.c
    Thirdparty/luabins/src/fwrite.c
    Thirdparty/luabins/src/load.c
    Thirdparty/luabins/src/luabins.c
    Thirdparty/luabins/src/luainternals.c
    Thirdparty/luabins/src/save.c
    Thirdparty/luabins/src/savebuffer.c
    Thirdparty/luabins/src/write.c
    HikariSub/AutomationProgress.cpp
    HikariSub/AutomationScriptReader.cpp
    HikariSub/AutomationToFile.cpp
    HikariSub/AutomationUtils.cpp
    HikariSub/AutoSavesRemoving.cpp
    HikariSub/BidiConversion.cpp
    HikariSub/BitmapButton.cpp
    HikariSub/CsriMod.cpp
    HikariSub/ConfigConverter.cpp
    HikariSub/Context.cpp
    HikariSub/ContextVulkan.cpp
    HikariSub/ContextDX9.cpp
    HikariSub/DshowRenderer.cpp
    HikariSub/DummyVideo.cpp
    HikariSub/FontCatalogList.cpp
    HikariSub/HikariGauge.cpp
    HikariSub/HikariPanel.cpp
    HikariSub/LineGroups.cpp
    HikariSub/LineParse.cpp
    HikariSub/AssBlend.cpp
    HikariSub/CsriOverlay.cpp
    HikariSub/FrameExchange.cpp
    HikariSub/FrameQueue.cpp
    HikariSub/Playback.cpp
    HikariSub/WaveformPeaks.cpp
    HikariSub/TextComparison.cpp
    HikariSub/NaturalOrder.cpp
    HikariSub/Provider.cpp
    HikariSub/ProviderDummy.cpp
    HikariSub/ProviderFFMS2.cpp
    HikariSub/RendererFFMS2.cpp
    HikariSub/RendererGStreamer.cpp
    HikariSub/FileTypes.cpp
    HikariSub/JsonValue.cpp
    HikariSub/UpdateChecker.cpp
    HikariSub/UpdateApply.cpp
    HikariSub/UpdateInstaller.cpp
    HikariSub/Registry.cpp
    HikariSub/RendererDirectShow.cpp
    HikariSub/RendererVideo.cpp
    HikariSub/AutoSaveOpen.cpp
    HikariSub/ShiftTimes.cpp
    HikariSub/colorspace.cpp
    HikariSub/config.cpp
    HikariSub/findreplace.cpp
    HikariSub/FindReplaceDialog.cpp
    HikariSub/FindReplaceResultsDialog.cpp
    HikariSub/FontEnumerator.cpp
    HikariSub/GFFT/GFFT.cpp
    HikariSub/GraphicsD2D.cpp
    HikariSub/HotkeysNaming.cpp
    HikariSub/HikariCheckBox.cpp
    HikariSub/HikariDialog.cpp
    HikariSub/HikariFrame.cpp
    HikariSub/HikariListCtrl.cpp
    HikariSub/HikariMessageBox.cpp
    HikariSub/hikarisubApp.cpp
    HikariSub/HikariRadioButton.cpp
    HikariSub/HikariScrollbar.cpp
    HikariSub/HikariSlider.cpp
    HikariSub/HikariStaticBoxSizer.cpp
    HikariSub/HikariStaticText.cpp
    HikariSub/HikariStatusBar.cpp
    HikariSub/HikariTabBar.cpp
    HikariSub/HikariTextCtrl.cpp
    HikariSub/HikariTextValidator.cpp
    HikariSub/HikariTreeBook.cpp
    HikariSub/HikariWindowResizer.cpp
    HikariSub/KeyframesLoader.cpp
    HikariSub/ListControls.cpp
    HikariSub/LogHandler.cpp
    HikariSub/MappedButton.cpp
    HikariSub/Menu.cpp
    HikariSub/MenuButton.cpp
    HikariSub/MispellReplacerDialog.cpp
    HikariSub/OptionsPanels.cpp
    HikariSub/SelectLines.cpp
    HikariSub/SpellCheckerDialog.cpp
    HikariSub/MisspellReplacer.cpp
    HikariSub/styles.cpp
    HikariSub/stylestore.cpp
    HikariSub/SubsGridDialogs.cpp
    HikariSub/SubsGridFiltering.cpp
    HikariSub/SubsGridPreview.cpp
    HikariSub/SubsGridWindow.cpp
    HikariSub/SubsLoader.cpp
    HikariSub/SubsResampleDialog.cpp
    HikariSub/ScriptAttachments.cpp
    HikariSub/SubtitlesLibass.cpp
    HikariSub/SubtitlesProviderChoice.cpp
    HikariSub/SubtitlesProviderManager.cpp
    HikariSub/SubtitlesVSFilter.cpp
    HikariSub/TextEditorTagList.cpp
    HikariSub/VideoToolbar.cpp
    HikariSub/VisualAllTagsControls.cpp
    HikariSub/VisualAllTagsEdition.cpp
    HikariSub/VisualCross.cpp
    HikariSub/VisualDrawingShapes.cpp
    HikariSub/VisualMoveAll.cpp
    HikariSub/VisualClipRect.cpp
    HikariSub/StyleChange.cpp
    HikariSub/ColorPicker.cpp
    HikariSub/DropperPointer.cpp
    HikariSub/DropFiles.cpp
    HikariSub/EditBox.cpp
    HikariSub/FontCollector.cpp
    HikariSub/FontDialog.cpp
    HikariSub/SubsGrid.cpp
    HikariSub/Hotkeys.cpp
    HikariSub/KaraokeSplitting.cpp
    HikariSub/NewCatalog.cpp
    HikariSub/NumCtrl.cpp
    HikariSub/OpennWrite.cpp
    HikariSub/SaveFile.cpp
    HikariSub/PrivateFolder.cpp
    HikariSub/OptionsDialog.cpp
    HikariSub/ProgressDialog.cpp
    HikariSub/ScriptInfo.cpp
    HikariSub/SpellChecker.cpp
    HikariSub/StyleList.cpp
    HikariSub/Stylelistbox.cpp
    HikariSub/StylePreview.cpp
    HikariSub/SubsDialogue.cpp
    HikariSub/SubsFile.cpp
    HikariSub/SubsGridBase.cpp
    HikariSub/TabPanel.cpp
    HikariSub/SubsTime.cpp
    HikariSub/Timebase.cpp
    HikariSub/TimeCtrl.cpp
    HikariSub/TLDialog.cpp
    HikariSub/Toolbar.cpp
    HikariSub/VideoBox.cpp
    HikariSub/VideoFullscreen.cpp
    HikariSub/VideoSlider.cpp
    HikariSub/VisualClips.cpp
    HikariSub/VisualMove.cpp
    HikariSub/VisualPosition.cpp
    HikariSub/VisualRotationXY.cpp
    HikariSub/VisualRotationZ.cpp
    HikariSub/Visuals.cpp
    HikariSub/VisualScale.cpp
    HikariSub/UtilsWindows.cpp
    Thirdparty/Build/FFMS2/indexing_additional.cpp
)
target_include_directories(hikarisub PRIVATE
    ${CMAKE_SOURCE_DIR}/HikariSub
    # wx/msw/*.h shims for wxGTK; kept out of HikariSub/ for the Windows build.
    ${CMAKE_SOURCE_DIR}/cmake/linux-compat
    ${CMAKE_SOURCE_DIR}/HikariSub/GFFT
    ${CMAKE_SOURCE_DIR}/Thirdparty/luabins/include
    ${CMAKE_SOURCE_DIR}/Thirdparty/DependencyControl/include
    ${CMAKE_SOURCE_DIR}/Thirdparty/ffms2/src
)

# Generate HikariSub/gitparams.h from the current git HEAD before building.  Runs
# every build but only rewrites the header when the commit/branch changes, so
# config.cpp recompiles only when the version actually moves.
add_custom_target(hikarisub_gitparams ALL
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/GenGitParams.cmake
    BYPRODUCTS ${CMAKE_SOURCE_DIR}/HikariSub/gitparams.h
    COMMENT "Generating gitparams.h from git HEAD"
    VERBATIM
)
add_dependencies(hikarisub hikarisub_gitparams
    hikarisub_depctrl_badmutex hikarisub_depctrl_precisetimer hikarisub_depctrl_downloadmanager)
target_link_libraries(hikarisub PRIVATE ${wxWidgets_LIBRARIES} PkgConfig::LIBASS PkgConfig::FFMS2 hikarisub_luajit PkgConfig::HUNSPELL PkgConfig::UCHARDET PkgConfig::LIBCURL PkgConfig::ICU_UC PkgConfig::ICU_I18N PkgConfig::FFMPEG PkgConfig::FONTCONFIG PkgConfig::GSTREAMER PkgConfig::CAIRO PkgConfig::GTK3 Boost::filesystem Boost::locale Boost::regex $<$<BOOL:${Boost_SYSTEM_FOUND}>:Boost::system> $<$<BOOL:${Boost_THREAD_FOUND}>:Boost::thread> GL ${CMAKE_DL_LIBS})
set_target_properties(hikarisub PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)
# Prefer old-style DT_RPATH over DT_RUNPATH for the portable build tree.  RPATH
# is applied transitively, so the bundled app-private libraries (e.g. libicuuc)
# can resolve their own versioned dependencies (for example libicudata) from
# $ORIGIN too.  Host-coupled libraries (GL drivers, the GTK/X11 stack and
# libstdc++) are intentionally NOT bundled (see cmake/CopyRuntimeDependencies),
# so this RPATH only ever points the loader at our own libraries.
target_link_options(hikarisub PRIVATE "-Wl,--disable-new-dtags")

set(HIKARISUB_RUNTIME_DIR $<TARGET_FILE_DIR:hikarisub>)
set(HIKARISUB_DICTIONARY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Dictionary")
find_path(HIKARISUB_SYSTEM_DICTIONARY_DIR
    NAMES en_US.aff
    PATHS /usr/share/hunspell /usr/share/myspell/dicts
)
set(HIKARISUB_DICTIONARY_COPY_COMMAND
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HIKARISUB_RUNTIME_DIR}/Dictionary"
)
if(EXISTS "${HIKARISUB_DICTIONARY_SOURCE_DIR}")
    set(HIKARISUB_DICTIONARY_COPY_COMMAND
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${HIKARISUB_DICTIONARY_SOURCE_DIR}"
                "${HIKARISUB_RUNTIME_DIR}/Dictionary"
    )
elseif(HIKARISUB_SYSTEM_DICTIONARY_DIR AND EXISTS "${HIKARISUB_SYSTEM_DICTIONARY_DIR}/en_US.dic")
    set(HIKARISUB_DICTIONARY_COPY_COMMAND
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HIKARISUB_RUNTIME_DIR}/Dictionary"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HIKARISUB_SYSTEM_DICTIONARY_DIR}/en_US.aff"
                "${HIKARISUB_SYSTEM_DICTIONARY_DIR}/en_US.dic"
                "${HIKARISUB_RUNTIME_DIR}/Dictionary"
    )
else()
    message(WARNING
        "No en_US Hunspell dictionary was found. HikariSub will build, but its "
        "spell checker will have no default dictionary; install the distribution's "
        "English Hunspell dictionary package and reconfigure.")
endif()

# Keep the build tree runnable by copying runtime data next to the executable.
# Windows embeds PNG RCDATA through resource.rc, but the Linux build loads those
# resources from files. Translation catalogs are not committed: compile
# Locale/*.po into Locale/<lang>/LC_MESSAGES/hikarisub.mo, the layout wx loads.
add_custom_command(TARGET hikarisub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HIKARISUB_RUNTIME_DIR}/Locale"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/compile_catalogs.py"
            --po-dir "${CMAKE_SOURCE_DIR}/Locale"
            --out-dir "${HIKARISUB_RUNTIME_DIR}/Locale"
            --msgfmt "${MSGFMT_EXECUTABLE}"
    ${HIKARISUB_DICTIONARY_COPY_COMMAND}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HIKARISUB_RUNTIME_DIR}/HikariSub"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/HikariSub/resource.rc"
            "${HIKARISUB_RUNTIME_DIR}/HikariSub/resource.rc"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/HikariSub/Bitmaps"
            "${HIKARISUB_RUNTIME_DIR}/HikariSub/Bitmaps"
    COMMAND ${CMAKE_COMMAND}
            -D HIKARISUB_EXE=$<TARGET_FILE:hikarisub>
            -D RUNTIME_DIR=${HIKARISUB_RUNTIME_DIR}
            -D HIKARISUB_BUILD_DIR=${CMAKE_BINARY_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyRuntimeDependencies.cmake"
    COMMENT "Copying HikariSub runtime resources and bundled libraries"
)

# Package only explicitly selected runtime files.
set(HIKARISUB_PACKAGE_BASENAME "hikarisub-linux-${CMAKE_SYSTEM_PROCESSOR}")
set(HIKARISUB_PACKAGE_STAGE_PARENT "${CMAKE_BINARY_DIR}/package-stage")
set(HIKARISUB_PACKAGE_ARCHIVE
    "${CMAKE_BINARY_DIR}/dist/${HIKARISUB_PACKAGE_BASENAME}.tar.gz")
file(MAKE_DIRECTORY "${HIKARISUB_PACKAGE_STAGE_PARENT}")
include(${CMAKE_SOURCE_DIR}/cmake/DesktopIntegration.cmake)

add_custom_target(hikarisub_linux_package
    # Refresh the dependency manifest before every package.
    COMMAND ${CMAKE_COMMAND}
            -D HIKARISUB_EXE=$<TARGET_FILE:hikarisub>
            -D RUNTIME_DIR=$<TARGET_FILE_DIR:hikarisub>
            -D HIKARISUB_BUILD_DIR=${CMAKE_BINARY_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyRuntimeDependencies.cmake"
    COMMAND ${CMAKE_COMMAND}
            -D HIKARISUB_EXE=$<TARGET_FILE:hikarisub>
            -D RUNTIME_DIR=$<TARGET_FILE_DIR:hikarisub>
            -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -D BINARY_DIR=${CMAKE_BINARY_DIR}
            -D STAGING_PARENT=${HIKARISUB_PACKAGE_STAGE_PARENT}
            -D PACKAGE_BASENAME=${HIKARISUB_PACKAGE_BASENAME}
            -D ARCHIVE_PATH=${HIKARISUB_PACKAGE_ARCHIVE}
            -D MSGFMT_EXECUTABLE=${MSGFMT_EXECUTABLE}
            -D Python3_EXECUTABLE=${Python3_EXECUTABLE}
            -D TAR_EXECUTABLE=${TAR_EXECUTABLE}
            -D GZIP_EXECUTABLE=${GZIP_EXECUTABLE}
            -D PACKAGING_DIR=${CMAKE_SOURCE_DIR}/packaging/linux
            -D DESKTOP_GEN_DIR=${CMAKE_BINARY_DIR}/packaging
            -D SYSTEM_DICTIONARY_DIR=${HIKARISUB_SYSTEM_DICTIONARY_DIR}
            -D DEPCTRL_MODULE_DIR=${HIKARISUB_DEPCTRL_DIR}
            -D DEPCTRL_MODULES=${HIKARISUB_DEPCTRL_MODULE_LIST}
            -D SUBINSPECTOR_LIBRARY=$<TARGET_FILE:subinspector>
            -D READELF_EXECUTABLE=${CMAKE_READELF}
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageLinuxRuntime.cmake"
    DEPENDS hikarisub subinspector
            hikarisub_depctrl_badmutex hikarisub_depctrl_precisetimer hikarisub_depctrl_downloadmanager
    BYPRODUCTS
        "${HIKARISUB_PACKAGE_ARCHIVE}"
        "${HIKARISUB_PACKAGE_ARCHIVE}.sha256"
    WORKING_DIRECTORY "${HIKARISUB_PACKAGE_STAGE_PARENT}"
    COMMENT "Creating clean HikariSub Linux runtime archive"
    VERBATIM
)

# Release packages made from the staged tar.gz tree: the AppImage (everything
# bundled, cmake/PackageLinuxAppImage.cmake) and the deb and rpm (nFPM,
# cmake/PackageLinuxNative.cmake). packaging/linux/floor/build.sh builds them
# in the ubuntu:22.04 floor container; packaging/linux/fetch-tool.sh fetches
# the pinned tools named below.
file(STRINGS "${CMAKE_SOURCE_DIR}/HikariSub/VersionHikariSub.h" HIKARISUB_VERSION_LINE
     REGEX "^#define[ \t]+VersionHikariSub[ \t]+\"")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" HIKARISUB_PACKAGE_VERSION "${HIKARISUB_VERSION_LINE}")
set(HIKARISUB_APPIMAGETOOL "" CACHE FILEPATH "appimagetool, as pinned in packaging/linux/tools.lock")
set(HIKARISUB_APPIMAGE_RUNTIME "" CACHE FILEPATH "The static type2-runtime, as pinned in packaging/linux/tools.lock")
set(HIKARISUB_NFPM "" CACHE FILEPATH "nfpm, as pinned in packaging/linux/tools.lock")
set(HIKARISUB_MIN_GLIBC "2.35" CACHE STRING "The lowest glibc the packages declare (the manifest's min_glibc)")
set(HIKARISUB_PACKAGE_ROOT "${HIKARISUB_PACKAGE_STAGE_PARENT}/${HIKARISUB_PACKAGE_BASENAME}")

add_custom_target(hikarisub_appimage
    COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -D BINARY_DIR=${CMAKE_BINARY_DIR}
            -D PACKAGE_ROOT=${HIKARISUB_PACKAGE_ROOT}
            -D VERSION=${HIKARISUB_PACKAGE_VERSION}
            -D APPIMAGETOOL=${HIKARISUB_APPIMAGETOOL}
            -D APPIMAGE_RUNTIME=${HIKARISUB_APPIMAGE_RUNTIME}
            -D OUTPUT=${CMAKE_BINARY_DIR}/dist/HikariSub-${HIKARISUB_PACKAGE_VERSION}-linux-x86_64.AppImage
            -D PKG_CONFIG_EXECUTABLE=${PKG_CONFIG_EXECUTABLE}
            -D READELF_EXECUTABLE=${CMAKE_READELF}
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageLinuxAppImage.cmake"
    COMMENT "Creating the HikariSub AppImage"
    VERBATIM
)
add_dependencies(hikarisub_appimage hikarisub_linux_package)

add_custom_target(hikarisub_native_packages
    COMMAND ${CMAKE_COMMAND}
            -D SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -D BINARY_DIR=${CMAKE_BINARY_DIR}
            -D PACKAGE_ROOT=${HIKARISUB_PACKAGE_ROOT}
            -D VERSION=${HIKARISUB_PACKAGE_VERSION}
            -D NFPM=${HIKARISUB_NFPM}
            -D OUTPUT_DIR=${CMAKE_BINARY_DIR}/dist
            -D MIN_GLIBC=${HIKARISUB_MIN_GLIBC}
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageLinuxNative.cmake"
    COMMENT "Creating the HikariSub deb and rpm"
    VERBATIM
)
add_dependencies(hikarisub_native_packages hikarisub_linux_package)
