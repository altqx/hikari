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

# T2: the legacy Position and Move tools (VisualPosition.cpp and
# VisualMove.cpp, unchanged), Timebase.cpp, and the class declarations and
# definitions they call copied unchanged out of their files
# (extract_functions.py); the GUI they touch is stood in for by
# position/standins.h.
set(position_gen "${CMAKE_CURRENT_BINARY_DIR}/position")
file(MAKE_DIRECTORY "${position_gen}")
configure_file("${legacy_hs}/VisualPosition.cpp" "${position_gen}/copy/VisualPosition.cpp" COPYONLY)
configure_file("${legacy_hs}/VisualMove.cpp" "${position_gen}/copy/VisualMove.cpp" COPYONLY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.h" "${position_gen}/visuals_class.inc"
    "class Visuals : public TagFindReplace" "class PosData" "class Position : public Visuals"
    "class Move : public Visuals" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/TagFindReplace.h" "${position_gen}/tagfind_class.inc"
    "class FindData" "class TagFindReplace" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsDialogue.h" "${position_gen}/tagdata_class.inc"
    "class TagData" "class ParseData" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.cpp" "${position_gen}/visuals_defs.inc"
    "Visuals::Visuals()" "Visuals::~Visuals()"
    "void Visuals::GetDialoguesWithoutPosition(Dialogue* dialogue)"
    "D3DXVECTOR2 Visuals::GetDialogueAdditionalPosition(Dialogue* dialogue)"
    "void Visuals::GetRectFromSize(D3DXVECTOR2 size, int an, Dialogue* dial, Styles* style, D3DXVECTOR2* pos, D3DXVECTOR2* pos1)"
    "bool Visuals::IsInRect(D3DXVECTOR2 pos, D3DXVECTOR2 pos1, D3DXVECTOR2 secondpos, D3DXVECTOR2 secondpos1)"
    "void Visuals::RenderSubs(wxString *subs, bool redraw"
    "void Visuals::SetVisual(Dialogue* dial, int tool, bool noRefresh)"
    "void Visuals::SizeChanged(wxRect wsize, LPD3DXLINE _line, LPD3DXFONT _font, LPDIRECT3DDEVICE9 _device)"
    "void Visuals::DrawRectWx(wxDC& dc, D3DXVECTOR2 pos, bool sel, float rcsize)"
    "void Visuals::DrawCircleWx(wxDC& dc, D3DXVECTOR2 pos, bool sel, float crsize)"
    "void Visuals::DrawCrossWx(wxDC& dc, D3DXVECTOR2 position, const wxColour& color)"
    "void Visuals::DrawArrowWx(wxDC& dc, D3DXVECTOR2 from, D3DXVECTOR2* to, int diff)"
    "void Visuals::DrawDashedLineWx(wxDC& dc, D3DXVECTOR2* vector, size_t vectorSize, int dashLen, const wxColour& color)"
    "void Visuals::DrawWarningWx(wxDC& dc, bool comment)"
    "void Visuals::Draw(int time)"
    "void Visuals::GetVectorPoints(const wxString& vector, std::vector<ClipPoint>* points)"
    "void Visuals::GetMoveTimes(int *start, int *end)"
    "D3DXVECTOR2 Visuals::GetPosnScale(D3DXVECTOR2 *scale, byte *AN, double *tbl)"
    "void Visuals::SetVisual(bool dummy)"
    "D3DXVECTOR2 Visuals::GetPosition(Dialogue *Dial, bool *putinBracket, wxPoint *TextPos, double* moveTable)"
    "void Visuals::SetModified(int action, bool dummy)"
    "D3DXVECTOR2 Visuals::GetTextSize(Dialogue* dial, D3DXVECTOR2* border, Styles* style,"
    "D3DXVECTOR2 Visuals::GetDrawingSize(const wxString& drawing, D3DXVECTOR2* position)"
    "void Visuals::Curve(int pos, std::vector<ClipPoint>* vectorPoints,"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/TagFindReplace.cpp" "${position_gen}/tagfind_defs.inc"
    "bool TagFindReplace::FindTag(const wxString& pattern, const wxString& text, int mode, bool toEndOfSelection)"
    "int TagFindReplace::Replace(const wxString& replaceTxt, wxString* text)"
    "int TagFindReplace::ChangeText(wxString* txt, const wxString& what, bool inbracket, const wxPoint& pos)"
    "wxPoint TagFindReplace::GetPositionInText()"
    "bool TagFindReplace::GetDouble(double* retval)"
    "bool TagFindReplace::TagValueToStyle(Styles* style, const wxString& tag, const wxString& value)"
    "wxPoint FindBrackets(const wxString& text, long from)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsDialogue.cpp" "${position_gen}/dialogue_defs.inc"
    "TagData::TagData(const wxString &name, unsigned int _startTextPos)"
    "void TagData::PutValue(const wxString &_value, bool _multiValue)"
    "ParseData::ParseData()" "void ParseData::AddData(TagData *data)" "ParseData::~ParseData()"
    "void Dialogue::GetDefaultPosition(Styles* lineStyle, int an, const wxSize& subsSize, float* posx, float* posy)"
    "ParseData* Dialogue::ParseTags(wxString *tags, size_t ntags, bool plainText, const wxString& textToParse"
    "void Dialogue::ClearParse()" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/styles.cpp" "${position_gen}/styles_defs.inc"
    "Styles::Styles()" "Styles *Styles::Copy()" "double Styles::GetOtlineDouble()" "double Styles::GetShadowDouble()"
    "double Styles::GetSpacingDouble()" "double Styles::GetScaleXDouble()" "double Styles::GetScaleYDouble()"
    "double Styles::GetFontSizeDouble()" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/UtilsWindows.cpp" "${position_gen}/utils_defs.inc"
    "void CalcMovePosition(D3DXVECTOR2* point, double* moveTable, int time)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/config.cpp" "${position_gen}/config_defs.inc"
    "wxString getfloat(float num, const wxString& format, bool Truncate)"
    "size_t FindFromEnd(const wxString& text, const wxString& whatToFind, bool ignoreCase)" COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${position_gen}/extracted.inc"
    "#include \"visuals_defs.inc\"\n#include \"tagfind_defs.inc\"\n#include \"dialogue_defs.inc\"\n"
    "#include \"styles_defs.inc\"\n#include \"utils_defs.inc\"\n#include \"config_defs.inc\"\n")
add_executable(legacy_position_capture position_capture.cpp "${position_gen}/copy/VisualPosition.cpp"
    "${position_gen}/copy/VisualMove.cpp" "${legacy_hs}/Timebase.cpp")
# The stand-ins come first, so the copies' quoted includes find them; the
# legacy directory supplies the Linux D3DX stand-ins and Timebase.h.
target_include_directories(legacy_position_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/position" "${position_gen}" "${legacy_hs}" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_position_capture PRIVATE LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_position_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_position_capture PRIVATE ${wx_libs})

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

# T4: the legacy rectangle and vector clips (VisualClipRect.cpp and
# VisualClips.cpp, unchanged) with the real VisualClipPoint.h; the classes
# (VisualClips.h's among them) and definitions they use are copied out of their files
# unchanged (extract_functions.py) and the GUI stood in for by
# clip/clipstandins.h.
set(clip_gen "${CMAKE_CURRENT_BINARY_DIR}/clip")
file(MAKE_DIRECTORY "${clip_gen}")
configure_file("${legacy_hs}/VisualClipRect.cpp" "${clip_gen}/copy/VisualClipRect.cpp" COPYONLY)
configure_file("${legacy_hs}/VisualClips.cpp" "${clip_gen}/copy/VisualClips.cpp" COPYONLY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/TagFindReplace.h" "${clip_gen}/tagfind_class.inc"
    "class FindData {" "class TagFindReplace" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.h" "${clip_gen}/visuals_class.inc"
    "class Visuals : public TagFindReplace" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.h" "${clip_gen}/cliprect_class.inc"
    "class ClipRect : public Visuals" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/VisualClips.h" "${clip_gen}/drawingandclip_class.inc"
    "class DrawingAndClip : public Visuals" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/Visuals.cpp" "${clip_gen}/clip_visuals.inc"
    "Visuals::Visuals()" "Visuals::~Visuals()"
    "void Visuals::RenderSubs(wxString *subs, bool redraw /*= true*/)"
    "void Visuals::SetVisual(Dialogue* dial, int tool, bool noRefresh)"
    "void Visuals::SizeChanged(wxRect wsize, LPD3DXLINE _line, LPD3DXFONT _font, LPDIRECT3DDEVICE9 _device)"
    "D3DXVECTOR2 Visuals::CalcMovePos()"
    "void Visuals::GetVectorPoints(const wxString& vector, std::vector<ClipPoint>* points)"
    "D3DXVECTOR2 Visuals::GetPosnScale(D3DXVECTOR2 *scale, byte *AN, double *tbl)"
    "void Visuals::SetVisual(bool dummy)"
    "void Visuals::SetModified(int action, bool dummy)"
    "void Visuals::RotateDrawing("
    "D3DXVECTOR2 Visuals::CalcDrawingSize("
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/TagFindReplace.cpp" "${clip_gen}/clip_tagfind.inc"
    "bool TagFindReplace::FindTag(const wxString& pattern, const wxString& text, int mode, bool toEndOfSelection)"
    "int TagFindReplace::Replace(const wxString& replaceTxt, wxString* text)"
    "bool TagFindReplace::GetDouble(double* retval)"
    "bool TagFindReplace::GetTwoValueDouble(double* retval, double* retval2)"
    "bool TagFindReplace::GetTextResult(wxString* rettext)"
    "wxPoint TagFindReplace::GetPositionInText()"
    "wxPoint FindBrackets(const wxString& text, long from)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/EditBox.cpp" "${clip_gen}/clip_editbox.inc"
    "TextEditor * EditBox::GetEditor(const wxString &text)" "bool EditBox::IsCursorOnStart()"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/config.cpp" "${clip_gen}/clip_config.inc"
    "wxString getfloat(float num, const wxString& format, bool Truncate)"
    "size_t FindFromEnd(const wxString& text, const wxString& whatToFind, bool ignoreCase)"
    COMMAND_ERROR_IS_FATAL ANY)
# T5: the drawing's reading of a Line: SubsDialogue.h's TagData and ParseData,
# Dialogue::ParseTags, ClearParse and GetDefaultPosition, and the Style's
# scale (styles.cpp). The clips never reach them.
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsDialogue.h" "${clip_gen}/tagdata_class.inc"
    "class TagData" "class ParseData" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsDialogue.cpp" "${clip_gen}/clip_dialogue.inc"
    "TagData::TagData(const wxString &name, unsigned int _startTextPos)"
    "void TagData::PutValue(const wxString &_value, bool _multiValue)"
    "void TagData::AppendValue(const wxString& _value)"
    "ParseData::ParseData()" "void ParseData::AddData(TagData *data)" "ParseData::~ParseData()"
    "void Dialogue::ClearParse()"
    "void Dialogue::GetDefaultPosition(Styles* lineStyle, int an, const wxSize& subsSize, float* posx, float* posy)"
    "ParseData* Dialogue::ParseTags(wxString *tags, size_t ntags, bool plainText, const wxString& textToParse/* = L\"\"*/)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/styles.cpp" "${clip_gen}/clip_styles.inc"
    "double Styles::GetScaleXDouble()" "double Styles::GetScaleYDouble()" COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${clip_gen}/extracted.inc"
    "#include \"clip_visuals.inc\"\n#include \"clip_tagfind.inc\"\n#include \"clip_editbox.inc\"\n#include \"clip_config.inc\"\n"
    "#include \"clip_dialogue.inc\"\n#include \"clip_styles.inc\"\n")
# T5: the drawing's shapes (VisualDrawingShapes.h's ShapesSetting and Shapes,
# Shapes' definitions and the presets' LoadSettings, GetNames and
# SaveSettings); ShapesEdition (the dialog) is not run.
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/VisualDrawingShapes.h" "${clip_gen}/shapes_class.inc"
    "class ShapesSetting" "class Shapes : public DrawingAndClip" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/VisualDrawingShapes.cpp" "${clip_gen}/shapes_defs.inc"
    "enum {"
    "void LoadSettings(std::vector<ShapesSetting>* shapes)"
    "void GetNames(std::vector<ShapesSetting>* shapes, wxArrayString* nameList)"
    "void SaveSettings(std::vector<ShapesSetting>* shapes)"
    "Shapes::Shapes()" "void Shapes::OnMouseEvent(wxMouseEvent& evt)" "void Shapes::DrawVisual(int time)"
    "void Shapes::SetShape(int curshape)" "void Shapes::GetVisual(wxString* drawing)"
    "void Shapes::SetScale(wxString* txt, size_t position, int* diff)"
    "int Shapes::HitTest(const D3DXVECTOR2& pos, bool diff)" "void Shapes::SortPoints()"
    "void Shapes::SetDrawingScale()" "void Shapes::SetSquareShape(bool axisX)"
    "D3DXVECTOR2 Shapes::PointToVideo(const D3DXVECTOR2& point)"
    "D3DXVECTOR2 Shapes::PointToSubtitles(float x, float y)"
    "D3DXVECTOR2 Shapes::CalcDrawingAnchor(int alignment, const std::vector<ClipPoint>* points)"
    COMMAND_ERROR_IS_FATAL ANY)
foreach(probe legacy_clip_capture legacy_drawing_capture)
    add_executable(${probe} clip_capture.cpp "${clip_gen}/copy/VisualClipRect.cpp" "${clip_gen}/copy/VisualClips.cpp")
    # The stand-ins come first, so the copies' quoted includes find them; the
    # legacy directory supplies VisualClipPoint.h and the Linux D3DX stand-ins.
    target_include_directories(${probe} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/clip" "${clip_gen}" "${legacy_hs}" "${LEGACY_SOURCE}/cmake/linux-compat")
    target_compile_definitions(${probe} PRIVATE LINUX_BUILD HIKARISUB_LINUX_BUILD)
    target_compile_options(${probe} PRIVATE ${wx_cflags}
        -include "${LEGACY_SOURCE}/HikariSub/platform.h"
        -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
    target_link_libraries(${probe} PRIVATE ${wx_libs})
endforeach()
# T5: the same probe with the drawing tool (legacy Shapes, VECTORDRAW).
target_compile_definitions(legacy_drawing_capture PRIVATE PROBE_DRAWING)

# V6: the legacy video timing and selection commands and the seek-after /
# play-after choices. The definitions are copied out of their files unchanged
# (extract_functions.py); Timebase.cpp and SubsTime.cpp are linked as they
# are; the GUI they touch is stood in for by video_timing/standins.h.
set(vt_gen "${CMAKE_CURRENT_BINARY_DIR}/video_timing")
file(MAKE_DIRECTORY "${vt_gen}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/VideoBox.cpp" "${vt_gen}/videobox.inc"
    "int VideoBox::GetFrameTime(bool start)"
    "void VideoBox::GetVideoListsOptions(int *videoPlayAfter, int *videoSeekAfter)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsGridBase.cpp" "${vt_gen}/grid_base.inc"
    "void SubsGrid::SetStartTime(int stime)" "void SubsGrid::SetEndTime(int etime)" "void SubsGrid::SelectVisible()"
    "void SubsGrid::ShowEditOnVideo(bool afterUndo)"
    "size_t SubsGrid::GetKeyFromPosition(size_t position, int delta, bool safe /*= true*/)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/SubsGridWindow.cpp" "${vt_gen}/grid_window.inc"
    "void SubsGrid::SelVideoLine(int curtime)" "void SubsGrid::SetVideoLineTime(wxMouseEvent &evt, int mvtal)"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/HikariSubFrame.cpp" "${vt_gen}/frame.inc"
    "void HikariSubFrame::OnAudioSnap(wxCommandEvent& event)" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${extract}" "${legacy_hs}/EditBox.cpp" "${vt_gen}/editbox.inc"
    "void EditBox::SetLine(int Row, bool setaudio, bool save, bool nochangeline, bool autoPlay)"
    COMMAND_ERROR_IS_FATAL ANY)
add_executable(legacy_video_timing_capture video_timing_capture.cpp log_stub.cpp
    "${legacy_hs}/Timebase.cpp" "${legacy_hs}/SubsTime.cpp")
target_include_directories(legacy_video_timing_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/video_timing" "${vt_gen}" "${legacy_hs}" "${LEGACY_SOURCE}/cmake/linux-compat")
target_compile_definitions(legacy_video_timing_capture PRIVATE
    _SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS HAVE_DLL_EXPORT
    LINUX_BUILD HIKARISUB_LINUX_BUILD)
target_compile_options(legacy_video_timing_capture PRIVATE ${wx_cflags}
    -include "${LEGACY_SOURCE}/HikariSub/platform.h"
    -Wno-deprecated-declarations -Wno-narrowing -Wno-unused-result)
target_link_libraries(legacy_video_timing_capture PRIVATE ${wx_libs})
