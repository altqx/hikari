# W2: xy-VSFilter as the legacy solution built it, Release|x64 (20d647c4,
# HikariSub.sln:46-84). Each target below is one MSBuild project; its file list,
# defines and include directories are that project's Release|x64 evaluation
# (configuration.props, common.props, YASM.props, VSFilter.props and the
# .vcxproj). Include
# directories that resolved to nothing in the legacy tree ($(SolutionDir) is
# the HikariSub root there, so the fork-relative IncludePath entries and
# L:\Kainote paths did not exist) are left out. Precompiled headers are not
# used: every source includes its stdafx.h itself.
cmake_minimum_required(VERSION 3.28)
project(HikariXyVSFilter VERSION 3.2 LANGUAGES C CXX RC)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "xy-VSFilter builds with MSVC for x64 only")
endif()
foreach(var VSFILTER_SOURCE_DIR BASECLASSES_SOURCE_DIR YASM_EXECUTABLE)
    if(NOT EXISTS "${${var}}")
        message(FATAL_ERROR "${var} is missing: '${${var}}'")
    endif()
endforeach()
find_package(Boost CONFIG REQUIRED COMPONENTS flyweight smart_ptr)

set(V "${VSFILTER_SOURCE_DIR}")
set(SRC "${V}/src")
set(BC "${BASECLASSES_SOURCE_DIR}")
set(VD "${SRC}/thirdparty/VirtualDub")
set(VSF "${SRC}/filters/transform/vsfilter")

# RuntimeLibrary MultiThreaded everywhere, with MFC linked statically.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
# MSVC's default standard (C++14), as the projects leave LanguageStandard unset
# (BaseClasses sets stdcpp14).
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS ON)
# MSBuild's base flags in place of the vcpkg toolchain's: no /utf-8 (the
# VirtualDub stdafx.h uses #pragma setlocale, which /utf-8 refuses), no
# WIN32/_WINDOWS/NDEBUG beyond what each project defines, no /MD next to
# the static runtime, and resources in their own code page (no /c65001).
# /GR and /EHsc are MSBuild's defaults (RuntimeTypeInfo, ExceptionHandling Sync).
set(CMAKE_CXX_FLAGS "/nologo /GR /EHsc")
set(CMAKE_C_FLAGS "/nologo")
set(CMAKE_CXX_FLAGS_RELEASE "/Z7")
set(CMAKE_C_FLAGS_RELEASE "/Z7")
set(CMAKE_RC_FLAGS "")

# common.props, Release|x64.
set(common_defines WINVER=0x0601 _WIN32_WINNT=0x0601 PSAPI_VERSION=1 _WINDOWS WINDOWS _USE_MATH_DEFINES
    _WIN64 WIN64 NDEBUG _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
set(common_options /W3 /w34706 /GS /Gy /O2 /Ob2 /Oi /Ot /GF /MP)

function(vsfilter_library name)
    cmake_parse_arguments(L "MULTIBYTE;NO_COMMON" "DIR" "SOURCES;DEFINES;INCLUDES;OPTIONS;ASM" ${ARGN})
    set(sources)
    foreach(s IN LISTS L_SOURCES)
        list(APPEND sources "${L_DIR}/${s}")
    endforeach()
    foreach(a IN LISTS L_ASM)
        get_filename_component(stem "${a}" NAME_WE)
        set(obj "${CMAKE_CURRENT_BINARY_DIR}/${name}-asm/${stem}.obj")
        add_custom_command(OUTPUT "${obj}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${name}-asm"
            COMMAND "${YASM_EXECUTABLE}" -X vc -f win64 -o "${obj}" "${L_DIR}/${a}"
            DEPENDS "${L_DIR}/${a}" VERBATIM)
        list(APPEND sources "${obj}")
    endforeach()
    add_library(${name} STATIC ${sources})
    if(NOT L_NO_COMMON)
        target_compile_definitions(${name} PRIVATE ${common_defines})
        target_compile_options(${name} PRIVATE ${common_options})
    endif()
    if(L_MULTIBYTE)
        target_compile_definitions(${name} PRIVATE _MBCS)
    else()
        target_compile_definitions(${name} PRIVATE UNICODE _UNICODE)
    endif()
    target_compile_definitions(${name} PRIVATE ${L_DEFINES})
    target_compile_options(${name} PRIVATE ${L_OPTIONS})
    target_include_directories(${name} PRIVATE ${L_INCLUDES})
endfunction()

# Thirdparty/Build/BaseClasses/BaseClassesStatic.vcxproj (BaseClasses.vcxproj
# with BaseClassesStaticCrt): no common.props.
vsfilter_library(BaseClassesStatic NO_COMMON DIR "${BC}"
    SOURCES amextra.cpp amfilter.cpp amvideo.cpp arithutil.cpp combase.cpp cprop.cpp ctlutil.cpp ddmm.cpp
        dllentry.cpp dllsetup.cpp mtype.cpp outputq.cpp perflog.cpp pstream.cpp pullpin.cpp refclock.cpp
        renbase.cpp schedule.cpp seekpt.cpp source.cpp strmctl.cpp sysclock.cpp transfrm.cpp transip.cpp
        videoctl.cpp vtrans.cpp winctrl.cpp winutil.cpp wxdebug.cpp wxlist.cpp wxutil.cpp
    DEFINES WIN32 NDEBUG _LIB
    OPTIONS /W3 /O2
    INCLUDES "${BC}")
target_link_libraries(BaseClassesStatic PUBLIC strmiids.lib)

vsfilter_library(system DIR "${VD}/system"
    SOURCES ../h/vd2/system/source/registrymemory.cpp source/bitmath.cpp source/cache.cpp source/cmdline.cpp
        source/cpuaccel.cpp source/date.cpp source/debug.cpp source/debugx86.cpp source/Error.cpp
        source/error_win32.cpp source/event.cpp source/file.cpp source/fileasync.cpp source/filestream.cpp
        source/filesys.cpp source/filewatcher.cpp source/Fraction.cpp source/halffloat.cpp source/hash.cpp
        source/int128.cpp source/linearalloc.cpp source/list.cpp source/log.cpp source/math.cpp
        source/memory.cpp source/process.cpp source/profile.cpp source/progress.cpp source/protscope.cpp
        source/refcount.cpp source/registry.cpp source/strutil.cpp source/text.cpp source/thread.cpp
        source/thunk.cpp source/time.cpp source/tls.cpp source/vdalloc.cpp source/VDNamespace.cpp
        source/VDScheduler.cpp source/vdstl.cpp source/vdstl_hash.cpp source/vdstl_hashtable.cpp
        source/VDString.cpp source/vectors.cpp source/w32assist.cpp source/zip.cpp source/stdafx.cpp
    ASM source/a64_cpuaccel.asm source/a64_fraction.asm source/a64_int128.asm source/a64_thunk.asm
    DEFINES _LIB NOMINMAX WIN32_LEAN_AND_MEAN
    OPTIONS /w
    INCLUDES "${VD}/h" "${VD}/system/h")
target_link_libraries(system PUBLIC winmm.lib)

# Kasumi.vcxproj without the sources it excludes for x64 (the *_x86.cpp files).
vsfilter_library(Kasumi DIR "${VD}/Kasumi"
    SOURCES source/alphablt.cpp source/blitter.cpp source/blt.cpp source/blt_reference.cpp
        source/blt_reference_pal.cpp source/blt_reference_rgb.cpp source/blt_reference_yuv.cpp
        source/blt_reference_yuv2yuv.cpp source/blt_reference_yuvrev.cpp source/blt_setup.cpp
        source/blt_spanutils.cpp source/blt_uberblit.cpp source/pixel.cpp source/pixmaputils.cpp
        source/region.cpp source/resample.cpp source/resample_kernels.cpp source/resample_stages.cpp
        source/resample_stages_reference.cpp source/stdafx.cpp source/stretchblt_reference.cpp source/tables.cpp
        source/text.cpp source/triblt.cpp source/uberblit.cpp source/uberblit_16f.cpp source/uberblit_gen.cpp
        source/uberblit_resample.cpp source/uberblit_resample_special.cpp source/uberblit_swizzle.cpp
        source/uberblit_v210.cpp source/uberblit_ycbcr_generic.cpp source/resample_stages_x64.cpp
    ASM source/a64_resample.asm64
    DEFINES WIN32_LEAN_AND_MEAN NOMINMAX
    OPTIONS /w
    INCLUDES "${VD}/Kasumi/h" "${VD}/h")
target_link_libraries(Kasumi PUBLIC system)

vsfilter_library(dsutil DIR "${SRC}/dsutil"
    SOURCES DSMPropertyBag.cpp DSUtil.cpp FontInstaller.cpp GolombBuffer.cpp H264Nalu.cpp HdmvClipInfo.cpp
        MediaTypeEx.cpp MediaTypes.cpp NullRenderers.cpp stdafx.cpp text.cpp vd.cpp vd_asm.cpp xy_utils.cpp
    DEFINES _LIB
    INCLUDES "${BC}" "${V}/include" "${VD}/h")
target_link_libraries(dsutil PUBLIC vfw32.lib Version.lib)

vsfilter_library(subpic DIR "${SRC}/subpic"
    SOURCES alpha_blender.cpp color_conv_table.cpp CoordGeom.cpp CRect2.cpp DX9SubPic.cpp MemSubPic.cpp
        PooledSubPic.cpp SimpleSubpicImpl.cpp SimpleSubPicProviderImpl.cpp SimpleSubPicWrapper.cpp stdafx.cpp
        SubPicAllocatorPresenterImpl.cpp SubPicImpl.cpp SubPicProviderExWrapper.cpp SubPicProviderImpl.cpp
        SubPicQueueImpl.cpp XySubRenderFrameWrapper.cpp XySubRenderProviderWrapper.cpp
    DEFINES _LIB
    INCLUDES "${SRC}/thirdparty/log4cplus/include" "${VSF}" "${V}/include" "${BC}")
target_link_libraries(subpic PUBLIC d3d9.lib Boost::smart_ptr Boost::flyweight)

vsfilter_library(libssf DIR "${SRC}/subtitles/libssf"
    SOURCES Arabic.cpp Array.cpp Exception.cpp File.cpp FontWrapper.cpp Glyph.cpp GlyphPath.cpp Node.cpp
        NodeFactory.cpp Rasterizer.cpp Renderer.cpp Split.cpp stdafx.cpp Stream.cpp StringMap.cpp Subtitle.cpp
        SubtitleFile.cpp
    OPTIONS /Ob1 /Os
    INCLUDES "${BC}")

vsfilter_library(subtitles DIR "${SRC}/subtitles"
    SOURCES BaseSub.cpp cache_manager.cpp CCDecoder.cpp CompositionObject.cpp draw_item.cpp DVBSub.cpp GFN.cpp
        HdmvSub.cpp hdmv_subtitle_provider.cpp Rasterizer.cpp RealTextParser.cpp RenderedHdmvSubtitle.cpp RTS.cpp
        SSF.cpp stdafx.cpp StdioFile64.cpp STS.cpp subpixel_position_controler.cpp SubtitleInputPin.cpp
        TextFile.cpp USFSubtitles.cpp VobSubFile.cpp VobSubFileRipper.cpp VobSubImage.cpp
        xy_clipper_paint_machine.cpp xy_bitmap.cpp xy_filter.cpp xy_malloc.cpp xy_overlay_paint_machine.cpp
        xy_widen_region.cpp
    DEFINES _LIB
    INCLUDES "${SRC}/thirdparty" "${VSF}" "${SRC}/thirdparty/log4cplus/include" "${V}/include" "${BC}")
# lib/libacof64.lib is the fork's prebuilt library, pinned with its archive.
target_link_libraries(subtitles PUBLIC "${V}/lib/libacof64.lib" libssf Boost::smart_ptr Boost::flyweight)

vsfilter_library(basevideofilter DIR "${SRC}/filters/transform/basevideofilter"
    SOURCES BaseVideoFilter.cpp stdafx.cpp
    INCLUDES "${SRC}/thirdparty/log4cplus/include" "${BC}" "${V}/include" "${VSF}")

vsfilter_library(unrar MULTIBYTE DIR "${SRC}/thirdparty/unrar"
    SOURCES archive.cpp arcread.cpp blake2s.cpp cmddata.cpp consio.cpp crc.cpp crypt.cpp dll.cpp encname.cpp
        errhnd.cpp extinfo.cpp extract.cpp filcreat.cpp file.cpp filefn.cpp filestr.cpp find.cpp getbits.cpp
        global.cpp hash.cpp headers.cpp isnt.cpp match.cpp options.cpp pathfn.cpp qopen.cpp rar.cpp rarpch.cpp
        rarvm.cpp rawread.cpp rdwrfn.cpp recvol.cpp rijndael.cpp rs.cpp rs16.cpp scantree.cpp secpassword.cpp
        sha1.cpp sha256.cpp smallfn.cpp strfn.cpp strlist.cpp system.cpp threadpool.cpp timefn.cpp ui.cpp
        unicode.cpp unpack.cpp volume.cpp
    DEFINES _LIB RARDLL SILENT _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES _CRT_SECURE_NO_WARNINGS
    OPTIONS /w /Ob1 /Os)

# VSFilter.vcxproj: the DLL (TargetName xy-Vsfilter, renamed to
# Csri/xy-VSFilter_hikarisub.dll by the legacy packaging, .github/scripts/package.py:36).
# Its thirdparty/mfc sources are excluded for Visual Studio newer than 10.0.
set(vsfilter_sources Copy.cpp
    csriapi.cpp DirectVobSub.cpp DirectVobSubFilter.cpp DirectVobSubFilterDll.cpp DirectVobSubPropPage.cpp
    once_logger.cpp plugins.cpp StdAfx.cpp StyleEditorDialog.cpp SubRenderOptionsImpl.cpp Systray.cpp
    TextInputPin.cpp vfr.cpp VSFilter.cpp XyOptionsImpl.cpp xy_logger.cpp)
list(TRANSFORM vsfilter_sources PREPEND "${VSF}/")
add_library(xy_vsfilter MODULE ${vsfilter_sources} "${VSF}/VSFilter.rc" "${VSF}/VSFilter.def")
set_target_properties(xy_vsfilter PROPERTIES OUTPUT_NAME xy-VSFilter_hikarisub PREFIX "")
# A DynamicLibrary configuration adds _WINDLL; Unicode adds UNICODE/_UNICODE.
target_compile_definitions(xy_vsfilter PRIVATE ${common_defines} UNICODE _UNICODE _WINDLL
    _AFX_NO_MFC_CONTROLS_IN_DIALOGS _LIB)
# VSFilter.props, Release, after common.props: IntrinsicFunctions false and
# BufferSecurityCheck false (the later flag wins).
target_compile_options(xy_vsfilter PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${common_options} /Oi- /GS->)
target_include_directories(xy_vsfilter PRIVATE "${V}/include" "${BC}" "${VSF}" "${SRC}/thirdparty/log4cplus/include"
    "${SRC}/subpic")
# ResourceCompile: _AFXDLL as the legacy project defines it, AFX_RESOURCE_DLL
# and AFX_TARG_ENU (VSFilter.props), _WIN64, NDEBUG, culture 0x0409.
set_source_files_properties("${VSF}/VSFilter.rc" PROPERTIES COMPILE_DEFINITIONS
    "_AFXDLL;AFX_RESOURCE_DLL;AFX_TARG_ENU;_WIN64;NDEBUG;UNICODE;_UNICODE")
target_compile_options(xy_vsfilter PRIVATE $<$<COMPILE_LANGUAGE:RC>:/l0x0409>)
target_link_libraries(xy_vsfilter PRIVATE BaseClassesStatic dsutil subtitles subpic Kasumi system libssf unrar
    basevideofilter atls.lib delayimp.lib winmm.lib dwmapi.lib Mfuuid.lib Boost::smart_ptr Boost::flyweight)
target_link_options(xy_vsfilter PRIVATE /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF
    /MERGE:.rdata=.text /RELEASE /DELAYLOAD:oleacc.dll) # DelayLoadDLLs (VSFilter.props)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS xy_vsfilter LIBRARY DESTINATION bin)
configure_package_config_file(HikariXyVSFilterConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/HikariXyVSFilterConfig.cmake"
    INSTALL_DESTINATION share/HikariXyVSFilter)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/HikariXyVSFilterConfigVersion.cmake"
    VERSION 3.2 COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/HikariXyVSFilterConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/HikariXyVSFilterConfigVersion.cmake" DESTINATION share/HikariXyVSFilter)
