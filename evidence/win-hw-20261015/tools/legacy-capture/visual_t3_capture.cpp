// T3: the legacy Scale, RotationZ and RotationXY tools. Runs the cases in
// inputs/visual-t3-cases.txt through the legacy tools and prints one JSON
// object per "state" line.
//
// Compiled with this file: VisualScale.cpp, VisualRotationZ.cpp and
// VisualRotationXY.cpp unchanged, Timebase.cpp unchanged, and, copied
// unchanged by extract_functions.py, the Visuals, Scale, RotationZ,
// RotationXY, TagFindReplace, FindData, ClipPoint, AssColor, Styles, TagData
// and ParseData classes and the definitions they call (listed in
// CMakeLists.txt). The stand-ins they run against are in visual_t3/standins.h.
//
// Built with PROBE_LEGACY_MEASURE (target legacy_visual_t3_measure_capture)
// the text measure is legacy's own GetLineTextExtents (UtilsWindows.cpp:233-
// 342, copied unchanged: the Linux build's wxMemoryDC branch) instead of the
// fixed one, on the font files given as arguments (added to fontconfig
// before wx starts, as installed fonts), and every "state" also lists the
// GetLineTextExtents calls made since the last one.
//
// Case file lines:
//   case <name>
//   client <width> <height> <panel>   the VideoBox client size and m_PanelHeight (GetWindowSize)
//   rect <left> <top> <right> <bottom> m_BackBufferRect, as RendererVideo::SetVisual passes it to SizeChanged
//   zoom <moveX> <moveY> <scaleX> <scaleY> the tools' SetZoom (default 0 0 1 1)
//   script <width> <height>           GetASSRes
//   time <ms>                          VideoBox::Tell
//   fps <fps>                          the video's Timebase (Timebase::FromFps, 100000 frames)
//   tlmode                             the grid's hasTLMode
//   style <name> <fontsize> <scaleX> <scaleY> <angle> <outline> <shadow> <alignment> <marginL> <marginR> <marginV>
//   stylefont <bold 0|1> <italic 0|1> <spacing> <fontname...>   the last style's font (default Garamond, 0 0 0)
//   line <start> <end> <style> <marginL> <marginR> <marginV> <comment 0|1>|<text>|<translation>
//   active <index>                     the Line in the editor (tab->edit->line, grid->currentLine)
//   select <index>...                  the Grid's selection (default: the active Line)
//   caret <from> <to>                  the editor's selection
//   tool scale|rotz|rotxy <toggled>    the tool and VideoToolbar's GetItemToggled
//                                      (RendererVideo::SetVisual: SizeChanged, SetZoom, SetVisual(line, tool))
//   option <toggled>                   a toolbar click: ChangeTool(toggled)
//   setvisual                          RendererVideo::SetVisual again (an edit or a new active Line)
//   press <x> <y> left|right|middle [shift]
//   move <x> <y> [left] [right] [middle] [shift]   the buttons held
//   release <x> <y> left|right|middle [shift]
//   key <char> [shift] [alt] [ctrl]
//   draw                               Visuals::Draw at the video's time
//   state                              print the state
//   end
#include "Visuals.h"
#include "VideoBox.h"
#include "SubsGrid.h"
#include "EditBox.h"
#include "TabPanel.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>

ProbeOptions Options;
std::vector<std::string> probeLog;

#include "extracted_t3.inc"

#ifdef PROBE_LEGACY_MEASURE
#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/font.h>

#include <fontconfig/fontconfig.h>

#define GetLineTextExtents legacyGetLineTextExtents
#include "measure.inc"
#undef GetLineTextExtents

// One GetLineTextExtents call: what it was given (the measuring Style's
// fields it reads) and what it answered.
struct MeasureCall {
    wxString text, fontname, fontsize, scaleX, scaleY, spacing;
    bool bold, italic, ok;
    float width = 0, height = 0, descent = 0, extlead = 0;
};
std::vector<MeasureCall> probeMeasures;

bool GetLineTextExtents(const wxString &text, Styles *style, float *width, float *height, float *descent, float *extlead)
{
    MeasureCall call{text, style->Fontname, style->Fontsize, style->ScaleX, style->ScaleY, style->Spacing,
                     style->Bold, style->Italic, false};
    call.ok = legacyGetLineTextExtents(text, style, &call.width, &call.height, &call.descent, &call.extlead);
    *width = call.width;
    *height = call.height;
    if (descent)
        *descent = call.descent;
    if (extlead)
        *extlead = call.extlead;
    probeMeasures.push_back(call);
    return call.ok;
}
#else
bool GetLineTextExtents(const wxString &text, Styles *style, float *width, float *height, float *descent, float *extlead)
{
    const double fs = style->GetFontSizeDouble();
    const double sx = style->GetScaleXDouble() / 100.0;
    const double sy = style->GetScaleYDouble() / 100.0;
    *width = static_cast<float>(static_cast<double>(text.length()) * fs * 0.5 * sx);
    *height = static_cast<float>(fs * sy);
    if (descent)
        *descent = static_cast<float>(fs * 0.2 * sy);
    if (extlead)
        *extlead = static_cast<float>(fs * 0.1 * sy);
    return true;
}
#endif

namespace {

std::string json(const wxString &s)
{
    const std::string utf8(s.utf8_str());
    std::string out = "\"";
    for (const unsigned char c : utf8) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default: out += char(c);
        }
    }
    return out + "\"";
}

std::string num(double v)
{
    // Scale's rectangle on an empty Line divides by an original size of 0.
    if (!std::isfinite(v))
        return std::isnan(v) ? "\"nan\"" : v > 0 ? "\"inf\"" : "\"-inf\"";
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.9g", v);
    return buffer;
}

std::string vec(const D3DXVECTOR2 &v)
{
    return "[" + num(v.x) + "," + num(v.y) + "]";
}

struct World {
    VideoBox video;
    SubsFile file;
    SubsGrid grid;
    EditBox edit;
    TextEditor textEdit, textEditOrig;
    TabPanel tab;
    Dialogue editLine;
    std::vector<std::unique_ptr<Dialogue>> lines;
    std::vector<std::unique_ptr<Styles>> styles;
    Visuals *visual = nullptr;
    std::string toolName;
    int toggled = 0;
    wxRect rect;
    D3DXVECTOR2 zoomMove{0, 0}, zoomScale{1, 1};
    int active = 0;
    long caretFrom = 0, caretTo = 0;
    bool explicitSelection = false;
    // Visuals::Draw draws only with a line and a device (the D3DX stand-ins).
    ID3DXLine *d3dLine = new ID3DXLine();
    IDirect3DDevice9 *device = new IDirect3DDevice9();

    World()
    {
        grid.file = &file;
        edit.grid = &grid;
        edit.TextEdit = &textEdit;
        edit.TextEditOrig = &textEditOrig;
        edit.line = &editLine;
        tab.video = &video;
        tab.grid = &grid;
        tab.edit = &edit;
    }
    ~World() { delete visual; }

    // EditBox::SetLine: the editor shows the active Line (the translation in
    // TLMode, the original beside it).
    void loadEditor()
    {
        Dialogue *d = file.dialogues[static_cast<size_t>(active)];
        editLine = *d;
        grid.currentLine = active;
        if (grid.hasTLMode) {
            textEditOrig.value = d->Text;
            textEdit.value = d->TextTl;
        } else {
            textEdit.value = d->Text;
        }
        textEdit.from = textEditOrig.from = caretFrom;
        textEdit.to = textEditOrig.to = caretTo;
        if (!explicitSelection)
            file.selections = {active};
    }

    // Legacy's tools leave members unset until they are read (Visuals::
    // moveValues[2..3] without a \move, GetPosnScale; Scale's AN and
    // changeAllTags before the first SetCurVisual). The measure probe makes
    // its tools in zeroed memory, so what it prints does not depend on what
    // the heap held (wx's GUI start leaves it dirty). The fixed-measure
    // probe keeps plain new: its reviewed capture
    // (local-t3-visual-20261005) was made that way.
    template <class T> static T *zeroed()
    {
#ifdef PROBE_LEGACY_MEASURE
        void *memory = ::operator new(sizeof(T));
        std::memset(memory, 0, sizeof(T));
        return new (memory) T();
#else
        return new T();
#endif
    }

    // RendererVideo::SetVisual (RendererVideo.cpp:1096-1120).
    void setVisual()
    {
        if (!visual) {
            if (toolName == "scale")
                visual = zeroed<Scale>();
            else if (toolName == "rotz")
                visual = zeroed<RotationZ>();
            else
                visual = zeroed<RotationXY>();
            visual->tab = &tab;
            visual->Visual = toolName == "scale" ? SCALE : toolName == "rotz" ? ROTATEZ : ROTATEXY;
            visual->SetTabPanel(&tab);
        } else {
            SAFE_DELETE(visual->dummytext);
        }
        visual->SizeChanged(rect, d3dLine, nullptr, device);
        visual->SetZoom(zoomMove, zoomScale);
        visual->SetVisual(&editLine, toggled, false);
    }
};

wxMouseEvent mouse(wxEventType type, int x, int y, bool left, bool right, bool middle, bool shift)
{
    wxMouseEvent e(type);
    e.SetX(x);
    e.SetY(y);
    e.SetLeftDown(left);
    e.SetRightDown(right);
    e.SetMiddleDown(middle);
    e.SetShiftDown(shift);
    return e;
}

void printState(World &w, const std::string &caseName, int step)
{
    std::cout << "{\"case\":" << json(caseName) << ",\"step\":" << step << ",\"tool\":" << json(w.toolName);
    std::cout << ",\"editor\":" << json(w.textEdit.value) << ",\"editorOrig\":" << json(w.textEditOrig.value);
    std::cout << ",\"caret\":[" << w.textEdit.from << "," << w.textEdit.to << "]";
    std::cout << ",\"lines\":[";
    for (size_t i = 0; i < w.file.dialogues.size(); i++)
        std::cout << (i ? "," : "") << "[" << json(w.file.dialogues[i]->Text) << "," << json(w.file.dialogues[i]->TextTl)
                  << "]";
    std::cout << "],\"history\":[";
    for (size_t i = 0; i < w.grid.history.size(); i++)
        std::cout << (i ? "," : "") << json(w.grid.history[i]);
    std::cout << "],\"sent\":[";
    for (size_t i = 0; i < w.edit.sent.size(); i++)
        std::cout << (i ? "," : "") << json(w.edit.sent[i]);
    std::cout << "],\"log\":[";
    for (size_t i = 0; i < probeLog.size(); i++)
        std::cout << (i ? "," : "") << json(probeLog[i]);
    std::cout << "]";
    Visuals *v = w.visual;
    std::cout << ",\"from\":" << vec(v->from) << ",\"to\":" << vec(v->to) << ",\"moveValues\":[";
    for (int i = 0; i < 7; i++)
        std::cout << (i ? "," : "") << num(v->moveValues[i]);
    std::cout << "]";
    if (auto *s = dynamic_cast<Scale *>(v)) {
        std::cout << ",\"scale\":" << vec(s->scale) << ",\"lastScale\":" << vec(s->lastScale)
                  << ",\"originalScale\":" << vec(s->originalScale) << ",\"arrowLengths\":" << vec(s->arrowLengths)
                  << ",\"type\":" << int(s->type) << ",\"grabbed\":" << s->grabbed << ",\"an\":" << int(s->AN)
                  << ",\"sizingRectangle\":[" << vec(s->sizingRectangle[0]) << "," << vec(s->sizingRectangle[1]) << ","
                  << vec(s->sizingRectangle[2]) << "," << vec(s->sizingRectangle[3]) << "]"
                  << ",\"originalSize\":" << vec(s->originalSize) << ",\"border\":" << vec(s->border)
                  << ",\"rectangleVisible\":" << (s->rectangleVisible ? "true" : "false")
                  << ",\"originalRectangleVisible\":" << (s->originalRectangleVisible ? "true" : "false");
    } else if (auto *z = dynamic_cast<RotationZ *>(v)) {
        std::cout << ",\"org\":" << vec(z->org) << ",\"lastmove\":" << vec(z->lastmove) << ",\"lastAngle\":"
                  << num(z->lastAngle) << ",\"twoPoints\":[" << vec(z->twoPoints[0]) << "," << vec(z->twoPoints[1])
                  << "],\"visibility\":[" << (z->visibility[0] ? "true" : "false") << ","
                  << (z->visibility[1] ? "true" : "false") << "],\"hasTwoPoints\":" << (z->hasTwoPoints ? "true" : "false");
    } else if (auto *xy = dynamic_cast<RotationXY *>(v)) {
        std::cout << ",\"org\":" << vec(xy->org) << ",\"angle\":" << vec(xy->angle) << ",\"oldAngle\":"
                  << vec(xy->oldAngle) << ",\"firstmove\":" << vec(xy->firstmove) << ",\"type\":" << int(xy->type)
                  << ",\"an\":" << int(xy->AN);
    }
#ifdef PROBE_LEGACY_MEASURE
    std::cout << ",\"extents\":[";
    for (size_t i = 0; i < probeMeasures.size(); i++) {
        const MeasureCall &m = probeMeasures[i];
        std::cout << (i ? "," : "") << "{\"text\":" << json(m.text) << ",\"fontname\":" << json(m.fontname)
                  << ",\"fontsize\":" << json(m.fontsize) << ",\"bold\":" << (m.bold ? "true" : "false")
                  << ",\"italic\":" << (m.italic ? "true" : "false") << ",\"scaleX\":" << json(m.scaleX)
                  << ",\"scaleY\":" << json(m.scaleY) << ",\"spacing\":" << json(m.spacing)
                  << ",\"ok\":" << (m.ok ? "true" : "false") << ",\"width\":" << num(m.width)
                  << ",\"height\":" << num(m.height) << ",\"descent\":" << num(m.descent)
                  << ",\"extlead\":" << num(m.extlead) << "}";
    }
    std::cout << "]";
    probeMeasures.clear();
#endif
    std::cout << "}\n";
}

} // namespace

int main(int argc, char **argv)
{
#ifdef PROBE_LEGACY_MEASURE
    // The fonts the cases measure, as if installed; then wx's GUI (the
    // memory DC measures through GTK's Pango).
    for (int i = 1; i < argc; i++)
        if (!FcConfigAppFontAddFile(FcConfigGetCurrent(), reinterpret_cast<const FcChar8 *>(argv[i]))) {
            std::cerr << "cannot add font " << argv[i] << "\n";
            return 1;
        }
    wxApp::SetInstance(new wxApp());
    int wxArgc = 1;
    if (!wxEntryStart(wxArgc, argv)) {
        std::cerr << "wx did not start\n";
        return 1;
    }
#else
    (void)argc;
    (void)argv;
#endif
    std::unique_ptr<World> w;
    std::string caseName;
    int step = 0;
    std::string text;
    while (std::getline(std::cin, text)) {
        std::istringstream in(text);
        std::string key;
        if (!(in >> key) || key[0] == '#')
            continue;
        if (key == "case") {
            in >> caseName;
            w = std::make_unique<World>();
            probeLog.clear();
#ifdef PROBE_LEGACY_MEASURE
            probeMeasures.clear();
#endif
            step = 0;
        } else if (key == "client") {
            in >> w->video.clientWidth >> w->video.clientHeight >> w->video.panelHeight;
        } else if (key == "rect") {
            int l, t, r, b;
            in >> l >> t >> r >> b;
            w->rect = wxRect(l, t, r, b);
        } else if (key == "zoom") {
            in >> w->zoomMove.x >> w->zoomMove.y >> w->zoomScale.x >> w->zoomScale.y;
        } else if (key == "script") {
            in >> w->grid.subsWidth >> w->grid.subsHeight;
        } else if (key == "time") {
            in >> w->video.time;
        } else if (key == "fps") {
            float fps;
            in >> fps;
            w->video.timebase = Timebase::FromFps(fps, 100000);
        } else if (key == "tlmode") {
            w->grid.hasTLMode = true;
        } else if (key == "style") {
            auto s = std::make_unique<Styles>();
            std::string name, fs, sx, sy, angle, outline, shadow, an, ml, mr, mv;
            in >> name >> fs >> sx >> sy >> angle >> outline >> shadow >> an >> ml >> mr >> mv;
            s->Name = wxString::FromUTF8(name);
            s->Fontsize = fs;
            s->ScaleX = sx;
            s->ScaleY = sy;
            s->Angle = angle;
            s->Outline = outline;
            s->Shadow = shadow;
            s->Alignment = an;
            s->MarginL = ml;
            s->MarginR = mr;
            s->MarginV = mv;
            w->file.styles.push_back(s.get());
            w->styles.push_back(std::move(s));
        } else if (key == "stylefont") {
            Styles *s = w->styles.back().get();
            int bold = 0, italic = 0;
            std::string spacing, name;
            in >> bold >> italic >> spacing;
            std::getline(in >> std::ws, name);
            s->Bold = bold != 0;
            s->Italic = italic != 0;
            s->Spacing = wxString::FromUTF8(spacing);
            s->Fontname = wxString::FromUTF8(name);
        } else if (key == "line") {
            auto d = std::make_unique<Dialogue>();
            int comment = 0;
            std::string style;
            in >> d->Start.mstime >> d->End.mstime >> style >> d->MarginL >> d->MarginR >> d->MarginV >> comment;
            d->Style = wxString::FromUTF8(style);
            d->IsComment = comment != 0;
            const auto bar = text.find('|');
            const auto bar2 = text.find('|', bar + 1);
            d->Text = wxString::FromUTF8(text.substr(bar + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar - 1));
            if (bar2 != std::string::npos)
                d->TextTl = wxString::FromUTF8(text.substr(bar2 + 1));
            w->file.dialogues.push_back(d.get());
            w->lines.push_back(std::move(d));
        } else if (key == "active") {
            in >> w->active;
        } else if (key == "select") {
            w->explicitSelection = true;
            w->file.selections.clear();
            int i;
            while (in >> i)
                w->file.selections.push_back(i);
        } else if (key == "caret") {
            in >> w->caretFrom >> w->caretTo;
        } else if (key == "tool") {
            in >> w->toolName >> w->toggled;
            w->loadEditor();
            w->setVisual();
        } else if (key == "option") {
            in >> w->toggled;
            w->visual->ChangeTool(w->toggled);
        } else if (key == "setvisual") {
            w->loadEditor();
            w->setVisual();
        } else if (key == "press" || key == "release" || key == "move") {
            int x, y;
            in >> x >> y;
            bool left = false, right = false, middle = false, shift = false;
            std::string word;
            std::string button;
            while (in >> word) {
                if (word == "shift")
                    shift = true;
                else {
                    if (word == "left")
                        left = true;
                    if (word == "right")
                        right = true;
                    if (word == "middle")
                        middle = true;
                    button = word;
                }
            }
            wxEventType type = wxEVT_MOTION;
            if (key == "press")
                type = button == "left" ? wxEVT_LEFT_DOWN : button == "right" ? wxEVT_RIGHT_DOWN : wxEVT_MIDDLE_DOWN;
            if (key == "release") {
                type = button == "left" ? wxEVT_LEFT_UP : button == "right" ? wxEVT_RIGHT_UP : wxEVT_MIDDLE_UP;
                left = right = middle = false;
            }
            wxMouseEvent e = mouse(type, x, y, left, right, middle, shift);
            w->visual->OnMouseEvent(e);
        } else if (key == "key") {
            std::string k;
            in >> k;
            wxKeyEvent e(wxEVT_KEY_DOWN);
            e.m_keyCode = k[0];
            std::string word;
            while (in >> word) {
                if (word == "shift")
                    e.SetShiftDown(true);
                if (word == "alt")
                    e.SetAltDown(true);
                if (word == "ctrl")
                    e.SetControlDown(true);
            }
            w->visual->OnKeyPress(e);
        } else if (key == "draw") {
            w->visual->Draw(w->video.time);
        } else if (key == "state") {
            printState(*w, caseName, step++);
        }
    }
#ifdef PROBE_LEGACY_MEASURE
    w.reset();
    wxEntryCleanup();
#endif
    return 0;
}
