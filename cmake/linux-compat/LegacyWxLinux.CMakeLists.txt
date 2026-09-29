cmake_minimum_required(VERSION 3.21)
project(HikariSubLinux LANGUAGES C CXX)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)
find_package(PkgConfig REQUIRED)
# Windows builds the pinned Thirdparty/wxWidgets 3.3.3 submodule. Use the same
# source/version on Linux instead of silently accepting the host's wxGTK 3.2.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(HIKARISUB_WX_DEFAULT_CONFIG "${CMAKE_SOURCE_DIR}/build-wx-linux-debug/prefix/bin/wx-config")
else()
    set(HIKARISUB_WX_DEFAULT_CONFIG "${CMAKE_SOURCE_DIR}/build-wx-linux/prefix/bin/wx-config")
endif()
set(HIKARISUB_WX_CONFIG "${HIKARISUB_WX_DEFAULT_CONFIG}"
    CACHE FILEPATH "wx-config from the pinned wxWidgets 3.3.3 Linux build")
if(NOT EXISTS "${HIKARISUB_WX_CONFIG}")
    message(FATAL_ERROR
        "Pinned wxWidgets 3.3.3 is missing at ${HIKARISUB_WX_CONFIG}. "
        "Run Thirdparty/build-wxwidgets-linux.sh (with --debug for Debug) first, or set HIKARISUB_WX_CONFIG "
        "to a wx-config for wxWidgets 3.3.3.")
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
        "${HIKARISUB_WX_CONFIG} does not point to the pinned wxWidgets 3.3.3 ${HIKARISUB_WX_EXPORT_CONFIG} build. "
        "Run Thirdparty/build-wxwidgets-linux.sh (with --debug for Debug) first.")
endif()
set(wxWidgets_CONFIG_EXECUTABLE "${HIKARISUB_WX_CONFIG}" CACHE FILEPATH
    "wx-config selected for HikariSub" FORCE)
find_package(wxWidgets 3.3.3 EXACT REQUIRED COMPONENTS core base adv aui html xml gl stc net)
include(${wxWidgets_USE_FILE})
pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)
pkg_check_modules(FFMS2 REQUIRED IMPORTED_TARGET ffms2)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET luajit)
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

add_compile_definitions(_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS HAVE_DLL_EXPORT LINUX_BUILD HIKARISUB_LINUX_BUILD)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-include> $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/HikariSub/platform.h> -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
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
    HikariSub/LineParse.cpp
    HikariSub/AssBlend.cpp
    HikariSub/FrameQueue.cpp
    HikariSub/Playback.cpp
    HikariSub/WaveformPeaks.cpp
    HikariSub/Provider.cpp
    HikariSub/ProviderDummy.cpp
    HikariSub/ProviderFFMS2.cpp
    HikariSub/RendererFFMS2.cpp
    HikariSub/RendererGStreamer.cpp
    HikariSub/FileTypes.cpp
    HikariSub/JsonValue.cpp
    HikariSub/UpdateChecker.cpp
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
    HikariSub/SubtitlesLibass.cpp
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
add_dependencies(hikarisub hikarisub_gitparams)
target_link_libraries(hikarisub PRIVATE ${wxWidgets_LIBRARIES} PkgConfig::LIBASS PkgConfig::FFMS2 PkgConfig::LUA PkgConfig::HUNSPELL PkgConfig::UCHARDET PkgConfig::LIBCURL PkgConfig::ICU_UC PkgConfig::ICU_I18N PkgConfig::FFMPEG PkgConfig::FONTCONFIG PkgConfig::GSTREAMER Boost::filesystem Boost::locale Boost::regex $<$<BOOL:${Boost_SYSTEM_FOUND}>:Boost::system> $<$<BOOL:${Boost_THREAD_FOUND}>:Boost::thread> GL ${CMAKE_DL_LIBS})
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
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageLinuxRuntime.cmake"
    DEPENDS hikarisub
    BYPRODUCTS
        "${HIKARISUB_PACKAGE_ARCHIVE}"
        "${HIKARISUB_PACKAGE_ARCHIVE}.sha256"
    WORKING_DIRECTORY "${HIKARISUB_PACKAGE_STAGE_PARENT}"
    COMMENT "Creating clean HikariSub Linux runtime archive"
    VERBATIM
)
