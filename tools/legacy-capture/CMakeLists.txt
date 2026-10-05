# Legacy characterization probes. Built against a checkout of the legacy wx
# source at the pinned baseline (LEGACY_SOURCE) and its wxWidgets build; never
# part of the Qt application build.
cmake_minimum_required(VERSION 3.21)
project(HikariLegacyCapture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(LEGACY_SOURCE "" CACHE PATH "Legacy wx source checkout at the pinned baseline")
set(WX_CONFIG "" CACHE FILEPATH "wx-config from that checkout's wxWidgets build")
if(NOT EXISTS "${LEGACY_SOURCE}/HikariSub/SubsTime.cpp" OR NOT EXISTS "${WX_CONFIG}")
    message(FATAL_ERROR "Set LEGACY_SOURCE and WX_CONFIG")
endif()
execute_process(COMMAND "${WX_CONFIG}" --cxxflags base,core OUTPUT_VARIABLE wx_cflags OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${WX_CONFIG}" --libs base,core OUTPUT_VARIABLE wx_libs OUTPUT_STRIP_TRAILING_WHITESPACE)
separate_arguments(wx_cflags UNIX_COMMAND "${wx_cflags}")
separate_arguments(wx_libs UNIX_COMMAND "${wx_libs}")

# Timebase is wx-free; SubsTime needs wx strings only for the operators used.
add_executable(legacy_time_capture time_capture.cpp log_stub.cpp
    "${LEGACY_SOURCE}/HikariSub/Timebase.cpp"
    "${LEGACY_SOURCE}/HikariSub/SubsTime.cpp")
# Same Linux compatibility layer the legacy build uses (its root CMakeLists).
target_include_directories(legacy_time_capture PRIVATE
    "${LEGACY_SOURCE}/HikariSub" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_time_capture PRIVATE
    _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS HAVE_DLL_EXPORT
    LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_time_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_time_capture PRIVATE ${wx_libs})

# A5: the legacy Karaoke class (KaraokeSplitting.cpp, unchanged) against
# stand-ins for the GUI headers it includes. The copy sits beside nothing, so
# its quoted includes resolve to karaoke/ instead of the legacy GUI headers.
configure_file("${LEGACY_SOURCE}/HikariSub/KaraokeSplitting.cpp" "${CMAKE_CURRENT_BINARY_DIR}/karaoke/KaraokeSplitting.cpp" COPYONLY)
configure_file("${LEGACY_SOURCE}/HikariSub/KaraokeSplitting.h" "${CMAKE_CURRENT_BINARY_DIR}/karaoke/KaraokeSplitting.h" COPYONLY)
add_executable(legacy_karaoke_capture karaoke_capture.cpp "${CMAKE_CURRENT_BINARY_DIR}/karaoke/KaraokeSplitting.cpp")
target_include_directories(legacy_karaoke_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/karaoke" "${CMAKE_CURRENT_BINARY_DIR}/karaoke" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_karaoke_capture PRIVATE LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_karaoke_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_karaoke_capture PRIVATE ${wx_libs})

# T1: the legacy video rectangle, zoom and pan, visual-tool coordinate conversions,
# the crosshair (VisualCross.cpp, unchanged) and VIDEO_COPY_COORDS. The other
# legacy definitions are copied out of their files unchanged
# (extract_functions.py) into the probe's build tree; the GUI they touch is
# stood in for by visual/standins.h.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(visual_gen "${CMAKE_CURRENT_BINARY_DIR}/visual")
set(legacy_hs "${LEGACY_SOURCE}/HikariSub")
set(extract "${CMAKE_CURRENT_SOURCE_DIR}/extract_functions.py")
file(MAKE_DIRECTORY "${visual_gen}")
configure_file("${legacy_hs}/VisualCross.cpp" "${visual_gen}/copy/VisualCross.cpp" COPYONLY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.h" "${visual_gen}/visuals_inline.inc"
    "inline float GetCalculatedInPosX(float posx)" "inline float GetCalculatedInPosY(float posy)"
    "inline float GetCalculatedOutPosX(float posx)" "inline float GetCalculatedOutPosY(float posy)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.h" "${visual_gen}/cross_class.inc"
    "class Cross : public Visuals" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/RendererVideo.h" "${visual_gen}/float_rect.inc"
    "class FloatRect" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/RendererVideo.cpp" "${visual_gen}/renderer.inc"
    "bool RendererVideo::UpdateRects(bool changeZoom)"
    "void RendererVideo::SetZoom(float percent, const wxPoint& mousePos)"
    "void RendererVideo::ResetZoom()"
    "void RendererVideo::SetVisualZoom()"
    "void RendererVideo::Zoom(const wxSize &size)"
    "void RendererVideo::ZoomMouseHandle(wxMouseEvent &evt)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.cpp" "${visual_gen}/visuals.inc"
    "void Visuals::SizeChanged(wxRect wsize, LPD3DXLINE _line, LPD3DXFONT _font, LPDIRECT3DDEVICE9 _device)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/VideoBox.cpp" "${visual_gen}/videobox.inc"
    "void VideoBox::OnCopyCoords(const wxPoint &pos)" "RECT VideoBox::GetVideoRect()"
    "void VideoBox::GetWindowSize(int* x, int* y, bool withTabPanel)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/config.cpp" "${visual_gen}/config.inc"
    "wxString getfloat(float num, const wxString& format, bool Truncate)" COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${visual_gen}/extracted.inc"
    "#include \"renderer.inc\"\n#include \"visuals.inc\"\n#include \"videobox.inc\"\n#include \"config.inc\"\n")
add_executable(legacy_visual_capture visual_capture.cpp "${visual_gen}/copy/VisualCross.cpp")
# The stand-ins come first, so the copy's quoted includes find them; the
# legacy directory supplies only the Linux D3DX stand-ins (d3dx9.h, d3d9.h).
target_include_directories(legacy_visual_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/visual" "${visual_gen}" "${legacy_hs}" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_visual_capture PRIVATE LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_visual_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_visual_capture PRIVATE ${wx_libs})

# R1: the legacy subtitle comparison. SubsGrid::SubsComparison and
# SubsGrid::CompareTexts (SubsGridBase.cpp), compareData (SubsGrid.h) and the
# Line's StoreTextHelper and Visibility (SubsDialogue.h) are copied out of their
# files unchanged; SubsTime is the legacy SubsTime.cpp; the grid, its file and
# the option are stood in for by comparison/standins.h.
set(comparison_gen "${CMAKE_CURRENT_BINARY_DIR}/comparison")
file(MAKE_DIRECTORY "${comparison_gen}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsGridBase.cpp" "${comparison_gen}/comparison.inc"
    "void SubsGrid::SubsComparison()"
    "void SubsGrid::CompareTexts(compareData &firstCompare, compareData &secondCompare, const wxString &first, const wxString &second)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsGrid.h" "${comparison_gen}/compare_data.inc"
    "class compareData" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsDialogue.h" "${comparison_gen}/dialogue_helpers.inc"
    "class StoreTextHelper" "class Visibility" COMMAND_ERROR_IS_FATAL ANY)
add_executable(legacy_comparison_capture comparison_capture.cpp log_stub.cpp
    "${legacy_hs}/Timebase.cpp" "${legacy_hs}/SubsTime.cpp")
target_include_directories(legacy_comparison_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/comparison" "${comparison_gen}" "${legacy_hs}" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_comparison_capture PRIVATE
    _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS HAVE_DLL_EXPORT
    LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_comparison_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result -Wno-return-type)
target_link_libraries(legacy_comparison_capture PRIVATE ${wx_libs})
