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

# T3: the legacy Scale, RotationZ and RotationXY tools (VisualScale.cpp,
# VisualRotationZ.cpp, VisualRotationXY.cpp and Timebase.cpp, unchanged)
# with the Visuals, TagFindReplace, Styles, ParseData and ClipPoint classes
# and the definitions they call, copied unchanged; the GUI they touch is
# stood in for by visual_t3/standins.h.
set(t3_gen "${CMAKE_CURRENT_BINARY_DIR}/visual_t3")
file(MAKE_DIRECTORY "${t3_gen}")
foreach(copy VisualScale.cpp VisualRotationZ.cpp VisualRotationXY.cpp)
    configure_file("${legacy_hs}/${copy}" "${t3_gen}/copy/${copy}" COPYONLY)
endforeach()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/styles.h" "${t3_gen}/styles_classes.inc"
    "class AssColor" "class Styles" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsDialogue.h" "${t3_gen}/parse_classes.inc"
    "class TagData" "class ParseData" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/TagFindReplace.h" "${t3_gen}/tagfind_classes.inc"
    "class FindData" "class TagFindReplace" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/VisualClipPoint.h" "${t3_gen}/clippoint_class.inc"
    "class ClipPoint" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.h" "${t3_gen}/visuals_classes.inc"
    "class Visuals : public TagFindReplace" "class Scale : public Visuals" "class RotationZ : public Visuals"
    "class RotationXY : public Visuals" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/styles.cpp" "${t3_gen}/styles.inc"
    "AssColor::AssColor()" "AssColor::~AssColor()" "void AssColor::SetAss(wxString color)"
    "void AssColor::SetAlphaString(wxString alpha)" "Styles::Styles()" "Styles::~Styles()"
    "double Styles::GetOtlineDouble()" "double Styles::GetShadowDouble()" "double Styles::GetScaleXDouble()"
    "double Styles::GetScaleYDouble()" "double Styles::GetFontSizeDouble()" "Styles *Styles::Copy()"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsDialogue.cpp" "${t3_gen}/dialogue.inc"
    "TagData::TagData(const wxString &name, unsigned int _startTextPos)"
    "void TagData::PutValue(const wxString &_value, bool _multiValue)" "ParseData::ParseData()"
    "void ParseData::AddData(TagData *data)" "ParseData::~ParseData()" "void Dialogue::ClearParse()"
    "ParseData* Dialogue::ParseTags(wxString *tags, size_t ntags, bool plainText, const wxString& textToParse/* = L\"\"*/)"
    "void Dialogue::GetDefaultPosition(Styles* lineStyle, int an, const wxSize& subsSize, float* posx, float* posy)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/TagFindReplace.cpp" "${t3_gen}/tagfind.inc"
    "bool TagFindReplace::FindTag(const wxString& pattern, const wxString& text, int mode, bool toEndOfSelection)"
    "int TagFindReplace::ReplaceAll(const wxString& pattern, const wxString& tag, wxString * text, std::function< void(const FindData&, wxString*)> func, bool returnPosWhenNoTags)"
    "int TagFindReplace::Replace(const wxString& replaceTxt, wxString* text)"
    "bool TagFindReplace::TagValueToStyle(Styles* style, const wxString& tag, const wxString& value)"
    "bool TagFindReplace::GetDouble(double* retval)"
    "bool TagFindReplace::GetTwoValueDouble(double* retval, double* retval2)"
    "int TagFindReplace::ChangeText(wxString* txt, const wxString& what, bool inbracket, const wxPoint& pos)"
    "bool TagFindReplace::GetTextResult(wxString* rettext)"
    "wxPoint TagFindReplace::GetPositionInText()"
    "wxPoint FindBrackets(const wxString& text, long from)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/VisualClips.cpp" "${t3_gen}/clippoint.inc"
    "ClipPoint::ClipPoint()" "ClipPoint::ClipPoint(float _x, float _y, wxString _type, bool isstart)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/RendererVideo.cpp" "${t3_gen}/vertex.inc"
    "void CreateVERTEX(VERTEX * v, float X, float Y, D3DCOLOR colour, float Z)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/config.cpp" "${t3_gen}/config.inc"
    "wxString getfloat(float num, const wxString& format, bool Truncate)"
    "size_t FindFromEnd(const wxString& text, const wxString& whatToFind, bool ignoreCase)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.cpp" "${t3_gen}/visuals.inc"
    "Visuals::Visuals()" "Visuals::~Visuals()"
    "void Visuals::GetDialoguesWithoutPosition(Dialogue* dialogue)"
    "D3DXVECTOR2 Visuals::GetDialogueAdditionalPosition(Dialogue* dialogue)"
    "void Visuals::GetRectFromSize(D3DXVECTOR2 size, int an, Dialogue* dial, Styles* style, D3DXVECTOR2* pos, D3DXVECTOR2* pos1)"
    "bool Visuals::IsInRect(D3DXVECTOR2 pos, D3DXVECTOR2 pos1, D3DXVECTOR2 secondpos, D3DXVECTOR2 secondpos1)"
    "void Visuals::RenderSubs(wxString *subs, bool redraw /*= true*/)"
    "void Visuals::SetVisual(Dialogue* dial, int tool, bool noRefresh)"
    "void Visuals::SizeChanged(wxRect wsize, LPD3DXLINE _line, LPD3DXFONT _font, LPDIRECT3DDEVICE9 _device)"
    "void Visuals::DrawArrow(D3DXVECTOR2 from, D3DXVECTOR2 *to, int diff)"
    "void Visuals::DrawRect(D3DXVECTOR2 pos, bool sel, float rcsize)"
    "void Visuals::Draw(int time)" "void Visuals::DrawWarning(bool comment)"
    "D3DXVECTOR2 Visuals::CalcMovePos()"
    "void Visuals::GetVectorPoints(const wxString& vector, std::vector<ClipPoint>* points)"
    "void Visuals::GetMoveTimes(int *start, int *end)"
    "D3DXVECTOR2 Visuals::GetPosnScale(D3DXVECTOR2 *scale, byte *AN, double *tbl)"
    "void Visuals::SetVisual(bool dummy)"
    "D3DXVECTOR2 Visuals::GetPosition(Dialogue *Dial, bool *putinBracket, wxPoint *TextPos, double* moveTable)"
    "void Visuals::ChangeOrg(wxString *txt, Dialogue *_dial, float coordx, float coordy)"
    "void Visuals::SetModified(int action, bool dummy)"
    "D3DXVECTOR2 Visuals::GetTextSize(Dialogue* dial, D3DXVECTOR2* border, Styles* style, "
    "D3DXVECTOR2 Visuals::GetDrawingSize(const wxString& drawing, D3DXVECTOR2* position)"
    "void Visuals::RotateZ(D3DXVECTOR2* point, float sinOfAngle, float cosOfAngle, D3DXVECTOR2 orgpivot)"
    "void Visuals::Curve(int pos, std::vector<ClipPoint>* vectorPoints, "
    COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${t3_gen}/extracted_t3.inc"
    "#include \"styles.inc\"\n#include \"dialogue.inc\"\n#include \"tagfind.inc\"\n#include \"clippoint.inc\"\n"
    "#include \"vertex.inc\"\n#include \"config.inc\"\n#include \"visuals.inc\"\n")
add_executable(legacy_visual_t3_capture visual_t3_capture.cpp
    "${t3_gen}/copy/VisualScale.cpp" "${t3_gen}/copy/VisualRotationZ.cpp" "${t3_gen}/copy/VisualRotationXY.cpp"
    "${legacy_hs}/Timebase.cpp")
target_include_directories(legacy_visual_t3_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/visual_t3" "${t3_gen}" "${legacy_hs}" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_visual_t3_capture PRIVATE LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_visual_t3_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_visual_t3_capture PRIVATE ${wx_libs})
