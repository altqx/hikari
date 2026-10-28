// T4: legacy rectangle and vector clips. Runs the legacy ClipRect and
// DrawingAndClip (VisualClipRect.cpp and VisualClips.cpp, unchanged) on the
// cases in inputs/clip-cases.txt and prints one JSON object per dump.
//
// Compiled with this file: copies of VisualClipRect.cpp and VisualClips.cpp,
// the real VisualClipPoint.h, and, copied unchanged by extract_functions.py,
// the classes Visuals, ClipRect, DrawingAndClip, FindData and TagFindReplace
// (Visuals.h, VisualClips.h, TagFindReplace.h), Visuals' constructor,
// destructor, RenderSubs, SetVisual (both), SizeChanged, CalcMovePos,
// GetVectorPoints, GetPosnScale, SetModified, RotateDrawing and
// CalcDrawingSize (Visuals.cpp), TagFindReplace's FindTag, Replace,
// GetDouble, GetTwoValueDouble, GetTextResult, GetPositionInText and
// FindBrackets (TagFindReplace.cpp), EditBox::GetEditor and IsCursorOnStart
// (EditBox.cpp), getfloat and FindFromEnd (config.cpp). The stand-ins they
// run against are in clip/clipstandins.h; the drawing (DrawRect, DrawCircle,
// DrawDashedLine, Draw) is not run.
//
// Case file lines:
//   case <name>
//   script <width> <height>                 the subtitles' PlayRes (GetASSRes)
//   video <left> <top> <right> <bottom>     the video rectangle (m_BackBufferRect, SizeChanged)
//   zoom <move x> <move y> <scale x> <scale y>   SetZoom (the tools' zoom)
//   line <text>                             a Line (the first is the active one)
//   tl <translation>                        the last Line's translation (TLMode on)
//   select <index>...                       the Grid's selection (default: the active Line)
//   caret <position>                        the Line editor's caret
//   tool rect|vector                        ClipRect or DrawingAndClip (VECTORCLIP)
//   mode <n>                                the vector toolbar's toggled button (default 1)
//   setvisual                               RendererVideo::SetVisual: SizeChanged, SetZoom, SetVisual
//   down|move|up|rdown|rup|mdown|mup <x> <y> [left] [ctrl] [shift] [alt]
//                                           mouse events (move with "left": a drag)
//   wheel <x> <y> <steps>
//   key <A|D|W|S|Delete> [ctrl] [shift] [alt]
//   activate <index>                        a click on another Line (EditBox::SetLine -> SetVisual)
//   invert                                  the toolbar's Invert clip button
//   client, frame, zoomat                   the rewrite's view (ignored here)
//   dump                                    print the state
//   end
//
// T5, legacy_drawing_capture only (PROBE_DRAWING): the drawing tool (legacy
// Shapes, VECTORDRAW, VisualDrawingShapes.cpp and VisualClips.cpp) with the
// presets' LoadSettings / SaveSettings, rendered after the set-up and after
// every event as legacy's Draw ran DrawVisual (VisualClips.cpp:117-121 moves
// a \move drawing's position there):
//   tool draw                               Shapes (the drawing; six buttons)
//   shapesfile <line>                       a line of Config/ShapesSettings.txt (none: legacy's defaults)
//   shape <n>                               the toolbar's shape list (0 "Choose", 1.. the presets) -> ChangeTool
//   time <ms>                               the video's time (VideoBox::Tell, default 1500)
//   style <field> <value>                   the Style's Alignment, MarginL/R/V, Angle, ScaleX or ScaleY
//   margins <l> <r> <v>                     the last Line's margins
//   dclick <x> <y> [flags]                  a left double click
//   savepresets                             SaveSettings on the presets (the dump shows the file)
#include "Visuals.h"
#include "VisualClips.h"
#ifdef PROBE_DRAWING
// VisualDrawingShapes.h's ShapesSetting and Shapes, copied; opened up so the
// probe reads their state.
#define private public
#include "shapes_class.inc"
#undef private
#endif

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

ProbeOptions Options;
ProbeEvents probeEvents;

#include "extracted.inc"
#ifdef PROBE_DRAWING
#include "shapes_defs.inc"

std::vector<ShapesSetting> VideoToolbar::shapes;
// VideoToolbar.h:331-339.
std::vector<ShapesSetting> *VideoToolbar::GetShapesSettings()
{
    if (!shapes.size()) {
        LoadSettings(&shapes);
    }
    return &shapes;
}
void VideoToolbar::SetShapesSettings(std::vector<ShapesSetting> *_shapes) { shapes = *(_shapes); }

ProbeFiles probeFiles;
wxString *ProbeFiles::find(const wxString &path)
{
    for (auto &f : files)
        if (f.first == std::string(path.utf8_str()))
            return &f.second;
    return nullptr;
}
OpenWrite::OpenWrite(const wxString &fileName, bool clear) : path(fileName), isfirst(clear)
{
    // The file is created or, with clear, truncated (wxFile::write).
    if (wxString *f = probeFiles.find(path)) {
        if (clear)
            f->clear();
    } else {
        probeFiles.files.push_back({std::string(path.utf8_str()), wxString()});
    }
}
bool OpenWrite::FileOpen(const wxString &filename, wxString *riddenText, bool)
{
    // wxFFile::ReadAll with wxConvAuto: the BOM is dropped.
    wxString *f = probeFiles.find(filename);
    if (!f)
        return false;
    *riddenText = *f;
    if (riddenText->StartsWith(wxString(wchar_t(0xFEFF))))
        riddenText->Remove(0, 1);
    return true;
}
void OpenWrite::PartFileWrite(const wxString &parttext)
{
    wxString *f = probeFiles.find(path);
    if (isfirst) {
        wchar_t bom = 0xFEFF;
        *f << wxString(bom) + parttext;
        isfirst = false;
        return;
    }
    *f << parttext;
}
#endif

// Stubs for the drawing the probe does not run (Visuals.cpp:337-425, 503-529).
void Visuals::DrawRect(D3DXVECTOR2, bool, float) {}
void Visuals::DrawCircle(D3DXVECTOR2, bool, float) {}
void Visuals::DrawDashedLine(D3DXVECTOR2 *, size_t, int, unsigned int) {}
void Visuals::Draw(int) {}
// Only the other tools' placement reads these (GetPosnScale for
// VECTORDRAW, Position); the clips never do.
D3DXVECTOR2 Visuals::GetDialogueAdditionalPosition(Dialogue *) { return D3DXVECTOR2(0, 0); }
void Visuals::GetMoveTimes(int *start, int *end)
{
    if (start)
        *start = 0;
    if (end)
        *end = 0;
}

namespace {

struct Probe {
    VideoBox video;
    SubsFile file;
    SubsGrid grid;
    EditBox edit;
    TabPanel tab;
    TextEditor textEdit, textEditOrig;
    Dialogue editLine;
    Visuals *visual = nullptr;
    int tool = CLIPRECT;
#ifdef PROBE_DRAWING
    ID3DXLine d3dLine;
    IDirect3DDevice9 d3dDevice;
#endif
    wxRect videoSize;
    D3DXVECTOR2 zoomMove{0, 0}, zoomScale{1, 1};
    int resetDepth = 0;
    std::vector<std::string> resets;
    std::vector<std::string> initial; // every Line's text|translation at "tool"
};

Probe *current = nullptr;

void loadEditor(Probe &p)
{
    // EditBox::SetLine: the active Line into the editors (TLMode: the
    // translation in TextEdit, the original in TextEditOrig).
    p.editLine = *p.file.dialogues[p.grid.currentLine];
    if (p.grid.hasTLMode) {
        p.textEdit.SetTextS(p.editLine.TextTl, false, false);
        p.textEditOrig.SetTextS(p.editLine.Text, false, false);
    } else {
        p.textEdit.SetTextS(p.editLine.Text, false, false);
    }
}

void setVisual(Probe &p)
{
    // RendererVideo::SetVisual (RendererVideo.cpp:1096-1121). The probe keeps
    // one tool, so this is the branch for the same Visual, which drops the
    // cached Line text of the dummy renders (RendererVideo.cpp:1110).
    SAFE_DELETE(p.visual->dummytext);
#ifdef PROBE_DRAWING
    // The renderer's line and device, so DrawVisual runs.
    p.visual->SizeChanged(p.videoSize, &p.d3dLine, nullptr, &p.d3dDevice);
#else
    p.visual->SizeChanged(p.videoSize, nullptr, nullptr, nullptr);
#endif
    p.visual->SetZoom(p.zoomMove, p.zoomScale);
    p.visual->SetVisual(&p.editLine, p.video.toolbar.GetItemToggled());
}

#ifdef PROBE_DRAWING
// Visuals::Draw (Visuals.cpp:504-529) without its warning: DrawVisual while
// the Line shows at the video's time.
void render(Probe &p)
{
    Visuals *v = p.visual;
    if (!(p.video.time >= v->start && p.video.time < v->end) ||
        (v->notDialogue && v->Visual != VECTORDRAW && v->Visual != VECTORCLIP)) {
        v->blockevents = true;
        return;
    }
    v->blockevents = false;
    v->DrawVisual(p.video.time);
}
#else
void render(Probe &) {}
#endif

} // namespace

void SubsGrid::record(int action)
{
    static const char *names[] = {"VISUAL_MOVE", "VISUAL_SCALE", "VISUAL_ROTATION_Z", "VISUAL_ROTATION_X_Y",
                                  "VISUAL_RECT_CLIP", "VISUAL_VECTOR_CLIP", "VISUAL_DRAWING", "VISUAL_ALL_TAGS"};
    ProbeHistory h;
    h.action = (action >= VISUAL_MOVE && action <= VISUAL_ALL_TAGS) ? names[action - VISUAL_MOVE] : std::to_string(action);
    for (auto *d : file->dialogues)
        h.texts.push_back(std::string((d->Text + L"|" + d->TextTl).utf8_str()));
    history.push_back(h);
}

// SubsGrid::SetModified (SubsGridBase.cpp:1124-1160) as the clips reach it:
// redit reloads the edit box (ShowEditedLine -> EditBox::SetLine), and an edit
// that is not a visual's dummy runs ShowEditOnVideo, whose SetVisual resets
// the tool (RendererVideo::SetVisual). The reset can come back here
// (DrawingAndClip::ChangeTool(6) inverts the clip again); the probe stops
// that after three levels and records it.
void probeModified(int action, bool redit, bool dummy)
{
    Probe &p = *current;
    p.grid.record(action);
    if (redit)
        loadEditor(p);
    if (!dummy && p.visual) {
        if (p.resetDepth >= 3) {
            p.resets.push_back("recursion");
            return;
        }
        ++p.resetDepth;
        p.resets.push_back("reset");
        setVisual(p);
        --p.resetDepth;
    }
}

wxString *SubsGrid::GetVisible(bool *visible, wxPoint *point, wxArrayInt *selected, bool)
{
    Probe &p = *current;
    if (visible)
        *visible = true;
    wxString *text = new wxString(p.textEdit.GetValue());
    if (point)
        *point = wxPoint(0, text->length());
    if (selected) {
        selected->clear();
        for (size_t i = 0; i < p.file.selections.size(); ++i)
            selected->push_back(0);
    }
    return text;
}

void EditBox::Send(unsigned char editionType, bool, bool dummy, bool visualdummy)
{
    // EditBox::Send's text columns (EditBox.cpp), then SubsGrid::ChangeLine's
    // SetModified(editionType, false, visualdummy).
    bool changed = false;
    if (grid->hasTLMode && TextEditOrig->IsModified()) {
        line->Text = TextEditOrig->GetValue(false);
        changed = true;
        TextEditOrig->SetModified(dummy);
    }
    if (TextEdit->IsModified()) {
        if (grid->hasTLMode)
            line->TextTl = TextEdit->GetValue(false);
        else
            line->Text = TextEdit->GetValue(false);
        changed = true;
        TextEdit->SetModified(dummy);
    }
    if (changed && !dummy) {
        *grid->file->dialogues[grid->currentLine] = *line;
        probeModified(editionType, false, visualdummy);
    }
}

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
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                out += buffer;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

std::string json(const std::string &s) { return json(wxString::FromUTF8(s.c_str())); }

std::string num(double v)
{
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.9g", v);
    return buffer;
}

wxString rest(std::istringstream &in)
{
    std::string s;
    std::getline(in, s);
    if (!s.empty() && s[0] == ' ')
        s.erase(0, 1);
    return wxString::FromUTF8(s.c_str());
}

void dump(Probe &p, const std::string &name, int index)
{
    std::cout << "{\"case\":" << json(name) << ",\"dump\":" << index;
    std::cout << ",\"texts\":[";
    for (size_t i = 0; i < p.file.dialogues.size(); ++i)
        std::cout << (i ? "," : "") << "[" << json(p.file.dialogues[i]->Text) << "," << json(p.file.dialogues[i]->TextTl)
                  << "]";
    std::cout << "],\"editor\":" << json(p.textEdit.GetValue()) << ",\"history\":[";
    for (size_t i = 0; i < p.grid.history.size(); ++i)
        std::cout << (i ? "," : "") << json(p.grid.history[i].action);
    // The entries that changed a text (legacy also recorded edits that
    // changed nothing: EditBox::Send after SetModified).
    int steps = 0;
    for (size_t i = 0; i < p.grid.history.size(); ++i)
        if (p.grid.history[i].texts != (i ? p.grid.history[i - 1].texts : p.initial))
            ++steps;
    std::cout << "],\"steps\":" << steps;
    std::cout << ",\"resets\":" << p.resets.size();
    bool recursion = false;
    for (const auto &r : p.resets)
        recursion |= r == "recursion";
    std::cout << ",\"recursion\":" << (recursion ? "true" : "false");
    std::cout << ",\"bells\":" << probeEvents.bells << ",\"messages\":[";
    for (size_t i = 0; i < probeEvents.messages.size(); ++i)
        std::cout << (i ? "," : "") << json(probeEvents.messages[i]);
    std::cout << "]";
    if (p.tool == CLIPRECT) {
        auto *r = static_cast<ClipRect *>(p.visual);
        std::cout << ",\"corners\":[" << num(r->Corner[0].x) << "," << num(r->Corner[0].y) << "," << num(r->Corner[1].x)
                  << "," << num(r->Corner[1].y) << "],\"show\":" << (r->showClip ? "true" : "false")
                  << ",\"inverse\":" << (r->invClip ? "true" : "false") << ",\"grabbed\":" << r->grabbed;
    } else {
        auto *d = static_cast<DrawingAndClip *>(p.visual);
        std::cout << ",\"points\":[";
        for (size_t i = 0; i < d->Points.size(); ++i) {
            const ClipPoint &c = d->Points[i];
            std::cout << (i ? "," : "") << "[" << num(c.x) << "," << num(c.y) << "," << json(c.type) << ","
                      << (c.start ? 1 : 0) << "," << (c.isSelected ? 1 : 0) << "]";
        }
        wxString visualText;
        d->GetVisual(&visualText);
        std::cout << "],\"visual\":" << json(visualText) << ",\"mask\":" << json(d->clipMask)
                  << ",\"vectorScale\":" << d->vectorScale << ",\"mode\":" << d->tool << ",\"grabbed\":" << d->grabbed
                  << ",\"coeff\":[" << num(d->coeffW) << "," << num(d->coeffH) << "]"
                  << ",\"selecting\":" << (d->drawSelection ? "true" : "false");
#ifdef PROBE_DRAWING
        if (p.tool == VECTORDRAW) {
            auto *s = static_cast<Shapes *>(p.visual);
            std::cout << ",\"drawing\":{\"x\":" << num(d->_x) << ",\"y\":" << num(d->_y) << ",\"scale\":["
                      << num(d->scale.x) << "," << num(d->scale.y) << "],\"an\":" << int(d->alignment)
                      << ",\"frz\":" << num(d->frz) << ",\"org\":[" << num(d->org.x) << "," << num(d->org.y)
                      << "],\"move\":[";
            for (int i = 0; i < 7; ++i)
                std::cout << (i ? "," : "") << num(d->moveValues[i]);
            std::cout << "],\"shapeSelection\":" << d->shapeSelection << ",\"shape\":" << s->shape
                      << ",\"rect\":[" << num(s->drawingRectangle[0].x) << "," << num(s->drawingRectangle[0].y) << ","
                      << num(s->drawingRectangle[1].x) << "," << num(s->drawingRectangle[1].y)
                      << "],\"rectVisible\":" << (s->rectangleVisible ? "true" : "false") << ",\"shapeScale\":["
                      << num(s->shapeScale.x) << "," << num(s->shapeScale.y) << "],\"shapeSize\":["
                      << num(s->shapeSize.x) << "," << num(s->shapeSize.y) << "],\"shapeGrabbed\":" << s->grabbed
                      << ",\"diffs\":[" << d->diffs.x << "," << d->diffs.y << "],\"shapePoints\":[";
            for (size_t i = 0; i < s->points.size(); ++i) {
                const ClipPoint &c = s->points[i];
                std::cout << (i ? "," : "") << "[" << num(c.x) << "," << num(c.y) << "," << json(c.type) << ","
                          << (c.start ? 1 : 0) << "]";
            }
            std::cout << "]}";
        }
        std::cout << ",\"presets\":[";
        for (size_t i = 0; i < VideoToolbar::shapes.size(); ++i) {
            const ShapesSetting &ss = VideoToolbar::shapes[i];
            std::cout << (i ? "," : "") << "[" << json(ss.name) << "," << json(ss.shape) << "," << int(ss.mode) << ","
                      << int(ss.scalingMode) << "]";
        }
        std::cout << "]";
        if (wxString *f = probeFiles.find(Options.pathfull + L"/Config/ShapesSettings.txt"))
            std::cout << ",\"file\":" << json(*f);
#endif
    }
    std::cout << "}\n";
}

wxMouseEvent mouseEvent(wxEventType type, int x, int y, const std::string &flags)
{
    wxMouseEvent e(type);
    e.SetX(x);
    e.SetY(y);
    e.SetControlDown(flags.find("ctrl") != std::string::npos);
    e.SetShiftDown(flags.find("shift") != std::string::npos);
    e.SetAltDown(flags.find("alt") != std::string::npos);
    if (type == wxEVT_LEFT_DOWN || type == wxEVT_LEFT_DCLICK || flags.find("left") != std::string::npos)
        e.SetLeftDown(true);
    if (type == wxEVT_RIGHT_DOWN)
        e.SetRightDown(true);
    if (type == wxEVT_MIDDLE_DOWN)
        e.SetMiddleDown(true);
    return e;
}

void run(std::istream &in)
{
    std::string line;
    Probe *p = nullptr;
    std::string name;
    int dumps = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string op;
        ls >> op;
        if (op.empty() || op[0] == '#')
            continue;
        if (op == "case") {
            delete p;
            p = new Probe();
            current = p;
            probeEvents = ProbeEvents();
            ls >> name;
            dumps = 0;
            p->grid.file = &p->file;
            p->edit.TextEdit = &p->textEdit;
            p->edit.TextEditOrig = &p->textEditOrig;
            p->edit.line = &p->editLine;
            p->edit.grid = &p->grid;
            p->tab.video = &p->video;
            p->tab.grid = &p->grid;
            p->tab.edit = &p->edit;
            p->video.time = 1500;
#ifdef PROBE_DRAWING
            VideoToolbar::shapes.clear();
            probeFiles = ProbeFiles();
#endif
        } else if (op == "script") {
            ls >> p->grid.subsWidth >> p->grid.subsHeight;
        } else if (op == "video") {
            int l, t, r, b;
            ls >> l >> t >> r >> b;
            p->videoSize = wxRect(l, t, r, b);
        } else if (op == "zoom") {
            ls >> p->zoomMove.x >> p->zoomMove.y >> p->zoomScale.x >> p->zoomScale.y;
        } else if (op == "line") {
            auto *d = new Dialogue();
            d->Text = rest(ls);
            d->Start.mstime = 1000;
            d->End.mstime = 2000;
            p->file.dialogues.push_back(d);
            if (p->file.selections.empty())
                p->file.selections.push_back(0);
        } else if (op == "tl") {
            p->file.dialogues.back()->TextTl = rest(ls);
            p->grid.hasTLMode = true;
        } else if (op == "select") {
            p->file.selections.clear();
            int i;
            while (ls >> i)
                p->file.selections.push_back(i);
        } else if (op == "caret") {
            long c;
            ls >> c;
            p->textEdit.SetSelection(c, c);
        } else if (op == "tool") {
            std::string t;
            ls >> t;
            p->tool = t == "rect" ? CLIPRECT : t == "draw" ? VECTORDRAW : VECTORCLIP;
            {
                const long caret = p->textEdit.from;
                loadEditor(*p);
                p->textEdit.SetSelection(caret, caret);
            }
            for (auto *d : p->file.dialogues)
                p->initial.push_back(std::string((d->Text + L"|" + d->TextTl).utf8_str()));
            if (p->tool == CLIPRECT) {
                p->visual = new ClipRect();
                p->video.toolbar.toggled = 0;
#ifdef PROBE_DRAWING
            } else if (p->tool == VECTORDRAW) {
                // Visuals::Get (Visuals.cpp:68-70); VectorItem(false) has six buttons.
                p->visual = new Shapes();
                p->video.toolbar.numIcons = 6;
#endif
            } else {
                p->visual = new DrawingAndClip();
            }
            p->visual->tab = &p->tab;
            p->visual->Visual = p->tool;
            p->visual->SetTabPanel(&p->tab);
            p->edit.Visual = p->tool;
        } else if (op == "mode") {
            ls >> p->video.toolbar.toggled;
        } else if (op == "setvisual") {
            setVisual(*p);
            render(*p);
#ifdef PROBE_DRAWING
        } else if (op == "shapesfile") {
            const wxString path = Options.pathfull + L"/Config/ShapesSettings.txt";
            if (!probeFiles.find(path))
                probeFiles.files.push_back({std::string(path.utf8_str()), wxString()});
            *probeFiles.find(path) << rest(ls) << L"\n";
        } else if (op == "shape") {
            // VectorItem's shape list (VideoToolbar.cpp:516-553): the
            // selection, then ID_MOVE_TOOLBAR_EVENT -> RendererVideo::
            // VisualChangeTool -> ChangeTool(GetItemToggled()).
            ls >> p->video.toolbar.shapeListSelection;
            p->visual->ChangeTool(p->video.toolbar.GetItemToggled());
            render(*p);
        } else if (op == "time") {
            // A seek: the renderer draws the new frame (Draw -> DrawVisual).
            ls >> p->video.time;
            render(*p);
        } else if (op == "style") {
            std::string field;
            ls >> field;
            const wxString value = rest(ls);
            Styles &st = p->file.style;
            if (field == "Alignment") st.Alignment = value;
            else if (field == "MarginL") st.MarginL = value;
            else if (field == "MarginR") st.MarginR = value;
            else if (field == "MarginV") st.MarginV = value;
            else if (field == "Angle") st.Angle = value;
            else if (field == "ScaleX") st.ScaleX = value;
            else if (field == "ScaleY") st.ScaleY = value;
        } else if (op == "margins") {
            Dialogue *d = p->file.dialogues.back();
            ls >> d->MarginL >> d->MarginR >> d->MarginV;
        } else if (op == "savepresets") {
            SaveSettings(VideoToolbar::GetShapesSettings());
#endif
        } else if (op == "down" || op == "move" || op == "up" || op == "rdown" || op == "rup" || op == "mdown" ||
                   op == "mup" || op == "dclick") {
            int x, y;
            ls >> x >> y;
            std::string flags, f;
            while (ls >> f)
                flags += f + " ";
            const wxEventType type = op == "down"    ? wxEVT_LEFT_DOWN
                                     : op == "move"  ? wxEVT_MOTION
                                     : op == "up"    ? wxEVT_LEFT_UP
                                     : op == "rdown" ? wxEVT_RIGHT_DOWN
                                     : op == "rup"   ? wxEVT_RIGHT_UP
                                     : op == "mdown" ? wxEVT_MIDDLE_DOWN
                                     : op == "dclick" ? wxEVT_LEFT_DCLICK
                                                     : wxEVT_MIDDLE_UP;
            wxMouseEvent e = mouseEvent(type, x, y, flags);
            p->visual->OnMouseEvent(e);
            render(*p);
        } else if (op == "wheel") {
            int x, y, steps;
            ls >> x >> y >> steps;
            wxMouseEvent e(wxEVT_MOUSEWHEEL);
            e.SetX(x);
            e.SetY(y);
            e.m_wheelRotation = steps * 120;
            e.m_wheelDelta = 120;
            p->visual->OnMouseEvent(e);
            render(*p);
        } else if (op == "key") {
            std::string k, f, flags;
            ls >> k;
            while (ls >> f)
                flags += f + " ";
            wxKeyEvent e(wxEVT_KEY_DOWN);
            e.m_keyCode = k == "Delete" ? WXK_DELETE : k[0];
            e.SetControlDown(flags.find("ctrl") != std::string::npos);
            e.SetShiftDown(flags.find("shift") != std::string::npos);
            e.SetAltDown(flags.find("alt") != std::string::npos);
            p->visual->OnKeyPress(e);
            render(*p);
        } else if (op == "activate") {
            // A click on another Grid row: EditBox::SetLine with the row
            // changed runs SetVisual (EditBox.cpp:485-488).
            int i;
            ls >> i;
            p->grid.currentLine = i;
            p->file.selections.clear();
            p->file.selections.push_back(i);
            loadEditor(*p);
            setVisual(*p);
            render(*p);
        } else if (op == "client" || op == "frame" || op == "zoomat") {
            // The rewrite's view set-up; the probe runs with "video" and "zoom".
        } else if (op == "invert") {
            // VectorItem's normal button (6) / ClipRectangleItem's (0), through
            // RendererVideo::VisualChangeTool -> ChangeTool(tool, false).
            p->visual->ChangeTool(p->tool == CLIPRECT ? 0 : 6, false);
            render(*p);
        } else if (op == "dump") {
            dump(*p, name, dumps++);
        } else if (op == "end") {
            delete p->visual;
            for (auto *d : p->file.dialogues)
                delete d;
            delete p;
            p = nullptr;
            current = nullptr;
        }
    }
}

} // namespace

int main()
{
    run(std::cin);
    return 0;
}
