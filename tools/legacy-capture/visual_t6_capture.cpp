// T6: the legacy Position shifter (MoveAll) and all-tags tool (AllTags).
// Runs the cases in inputs/visual-t6-cases.txt through the legacy tools and
// prints one JSON object per "state" line.
//
// Compiled with this file: VisualMoveAll.cpp, VisualAllTags.cpp,
// VisualAllTagsControls.cpp and Timebase.cpp unchanged, and, copied unchanged
// by extract_functions.py, the Visuals, moveElems, MoveAll, AllTags,
// TagFindReplace, FindData, ClipPoint, AssColor, Styles, TagData, ParseData
// and AllTagsSetting classes and the definitions they call (listed in
// CMakeLists.txt), LoadSettings, GetNames and SaveSettings of the all-tags
// definitions among them. The stand-ins they run against are in
// visual_t6/standins.h.
//
// Case file lines (the T3 probe's, and):
//   case <name>
//   client <width> <height> <panel>   the VideoBox client size and m_PanelHeight (GetWindowSize)
//   rect <left> <top> <right> <bottom> m_BackBufferRect, as RendererVideo::SetVisual passes it to SizeChanged
//   zoom <moveX> <moveY> <scaleX> <scaleY> the tools' SetZoom (default 0 0 1 1)
//   script <width> <height>           GetASSRes
//   time <ms>                          VideoBox::Tell
//   fps <fps>                          the video's Timebase (Timebase::FromFps, 100000 frames)
//   state-video playing|paused|none   VideoBox::GetState (default paused)
//   tlmode                             the grid's hasTLMode
//   style <name> <fontsize> <scaleX> <scaleY> <angle> <outline> <shadow> <alignment> <marginL> <marginR> <marginV>
//   stylecolour <primary> <secondary> <outline> <back> the last Style's colours (SetAss texts)
//   line <start> <end> <style> <marginL> <marginR> <marginV> <comment 0|1>|<text>|<translation>
//   hidden <index>...                  Lines the Grid hides (isVisible 0)
//   active <index>                     the Line in the editor (tab->edit->line, grid->currentLine)
//   select <index>...                  the Grid's selection (default: the active Line)
//   caret <from> <to>                  the editor's selection
//   config <path>                      Options.pathfull
//   file <path>|<text>                 a file the probe keeps in memory ("\n" for new lines)
//   loadtags                           VideoToolbar's definitions dropped and loaded again (LoadSettings)
//   tagset <index> <field> <value>     a definition's field (name tag min max value value2 value3 value4
//                                      step digits mode tagMode count)
//   savetags                           SaveSettings(VideoToolbar::GetTagsSettings())
//   tool moveall|alltags <toggled>     the tool and VideoToolbar's GetItemToggled
//                                      (RendererVideo::SetVisual: SizeChanged, SetZoom, SetVisual(line, tool))
//   option <toggled>                   a toolbar change: ChangeTool(toggled)
//   setvisual                          RendererVideo::SetVisual again (an edit or a new active Line)
//   press <x> <y> left|right|middle [shift]
//   dclick <x> <y> [shift]             a left double click (wxEVT_LEFT_DCLICK)
//   move <x> <y> [left] [right] [middle] [shift]   the buttons held
//   release <x> <y> left|right|middle [shift]
//   wheel <x> <y> <steps> [shift]      a wheel turn (WHEEL_DELTA 120 a step)
//   leave <x> <y>                      the pointer leaves the video (wxEVT_LEAVE_WINDOW)
//   key <char> [shift] [alt] [ctrl]
//   draw                               Visuals::Draw at the video's time (its drawing is recorded)
//   state                              print the state
//   end
#include "Visuals.h"
#include "VideoBox.h"
#include "SubsGrid.h"
#include "EditBox.h"
#include "TabPanel.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>

ProbeOptions Options;
ProbeHotkeys Hkeys;
ProbeFiles probeFiles;
std::vector<std::string> probeLog;
std::vector<ProbeDraw> probeDrawn;
std::vector<AllTagsSetting> VideoToolbar::tags;

#include "extracted_t6.inc"

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

void probeRecordPrimitive(int type, unsigned count, const void *vertices, unsigned stride)
{
    // DrawPrimitiveUP's vertex count: a strip or fan of n primitives has
    // n + 2 vertices, a line strip n + 1.
    ProbeDraw d;
    d.kind = "primitive";
    d.type = type;
    const unsigned n = type == D3DPT_LINESTRIP ? count + 1 : type == D3DPT_LINELIST ? count * 2 : count + 2;
    const auto *bytes = static_cast<const unsigned char *>(vertices);
    for (unsigned i = 0; i < n; i++) {
        const auto *v = reinterpret_cast<const VERTEX *>(bytes + i * stride);
        d.points.emplace_back(v->fX, v->fY);
        d.colours.push_back(v->Color);
    }
    probeDrawn.push_back(std::move(d));
}

void probeRecordLine(const float *points, unsigned stride, unsigned count, unsigned colour)
{
    ProbeDraw d;
    d.kind = "line";
    d.colour = colour;
    const auto *bytes = reinterpret_cast<const unsigned char *>(points);
    for (unsigned i = 0; i < count; i++) {
        const auto *p = reinterpret_cast<const float *>(bytes + i * stride);
        d.points.emplace_back(p[0], p[1]);
    }
    probeDrawn.push_back(std::move(d));
}

void probeText(const wxString &text, const RECT &rect, unsigned align, unsigned colour)
{
    ProbeDraw d;
    d.kind = "text";
    d.text = text;
    d.type = static_cast<int>(align);
    d.colour = colour;
    d.rect[0] = rect.left;
    d.rect[1] = rect.top;
    d.rect[2] = rect.right;
    d.rect[3] = rect.bottom;
    probeDrawn.push_back(std::move(d));
}

wxString *ProbeFiles::find(const wxString &path)
{
    for (auto &f : files)
        if (wxString::FromUTF8(f.first) == path)
            return &f.second;
    return nullptr;
}

OpenWrite::OpenWrite(const wxString &fileName, bool clear)
    : path(fileName), isfirst(clear)
{
    // The constructor creates the file or opens it for writing (truncating
    // it with `clear`, OpennWrite.cpp:35-49).
    if (wxString *f = probeFiles.find(fileName)) {
        if (clear)
            f->clear();
    } else {
        probeFiles.files.emplace_back(std::string(fileName.utf8_str()), wxString());
    }
}

bool OpenWrite::FileOpen(const wxString &filename, wxString *riddenText, bool)
{
    // OpennWrite.cpp:55-110: false for a missing or empty file; wxFFile's
    // ReadAll (wxConvAuto) drops a UTF-8 BOM.
    const wxString *f = probeFiles.find(filename);
    if (!f)
        return false;
    *riddenText = *f;
    if (riddenText->StartsWith(wxString(wchar_t(0xFEFF))))
        riddenText->Remove(0, 1);
    return !riddenText->empty();
}

void OpenWrite::FileWrite(const wxString &fileName, const wxString &textfile, bool)
{
    // OpennWrite.cpp:112-138: UTF-8 with a BOM.
    wxString *f = probeFiles.find(fileName);
    if (!f) {
        probeFiles.files.emplace_back(std::string(fileName.utf8_str()), wxString());
        f = &probeFiles.files.back().second;
    }
    *f = wxString(wchar_t(0xFEFF)) + textfile;
}

void OpenWrite::PartFileWrite(const wxString &parttext)
{
    // OpennWrite.cpp:140-150.
    wxString *f = probeFiles.find(path);
    if (!f)
        return;
    if (isfirst) {
        *f += wxString(wchar_t(0xFEFF)) + parttext;
        isfirst = false;
        return;
    }
    *f += parttext;
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
        default: out += char(c);
        }
    }
    return out + "\"";
}

std::string num(double v)
{
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.9g", v);
    return buffer;
}

std::string vec(const D3DXVECTOR2 &v)
{
    return "[" + num(v.x) + "," + num(v.y) + "]";
}

std::string boolean(bool b)
{
    return b ? "true" : "false";
}

wxString unescape(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
            out += '\n';
            i++;
        } else if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'r') {
            out += '\r';
            i++;
        } else {
            out += text[i];
        }
    }
    return wxString::FromUTF8(out);
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
    // Visuals::Draw draws only with a line and a device; the sliders need a
    // font too (the D3DX stand-ins).
    ID3DXLine *d3dLine = new ID3DXLine();
    ID3DXFont *d3dFont = new ID3DXFont();
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
        grid.modified = [this](bool dummy) {
            // EditBox::SetLine on the changed Line (the caret kept), then
            // unless dummy RendererVideo::SetVisual.
            Dialogue *d = file.dialogues[static_cast<size_t>(active)];
            editLine = *d;
            if (grid.hasTLMode) {
                textEditOrig.value = d->Text;
                textEdit.value = d->TextTl;
            } else {
                textEdit.value = d->Text;
            }
            if (!dummy)
                setVisual();
        };
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

    // RendererVideo::SetVisual (RendererVideo.cpp:1096-1120).
    void setVisual()
    {
        if (!visual) {
            if (toolName == "moveall")
                visual = new MoveAll();
            else
                visual = new AllTags();
            visual->tab = &tab;
            visual->Visual = toolName == "moveall" ? MOVEALL : ALL_TAGS;
            visual->SetTabPanel(&tab);
        } else {
            SAFE_DELETE(visual->dummytext);
        }
        visual->SizeChanged(rect, d3dLine, d3dFont, device);
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

void printTags(std::ostream &out, const std::vector<AllTagsSetting> &tags)
{
    out << "[";
    for (size_t i = 0; i < tags.size(); i++) {
        const AllTagsSetting &t = tags[i];
        out << (i ? "," : "") << "{\"name\":" << json(t.name) << ",\"tag\":" << json(t.tag) << ",\"min\":" << num(t.rangeMin)
            << ",\"max\":" << num(t.rangeMax) << ",\"step\":" << num(t.step) << ",\"values\":[" << num(t.values[0]) << ","
            << num(t.values[1]) << "," << num(t.values[2]) << "," << num(t.values[3]) << "],\"mode\":" << int(t.mode)
            << ",\"digits\":" << int(t.digitsAfterDot) << ",\"count\":" << int(t.numOfValues) << ",\"tagMode\":" << t.tagMode
            << "}";
    }
    out << "]";
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
    std::cout << "],\"files\":[";
    for (size_t i = 0; i < probeFiles.files.size(); i++)
        std::cout << (i ? "," : "") << "[" << json(wxString::FromUTF8(probeFiles.files[i].first)) << ","
                  << json(probeFiles.files[i].second) << "]";
    std::cout << "],\"tags\":";
    printTags(std::cout, VideoToolbar::tags);
    std::cout << ",\"itemToggled\":[";
    for (size_t i = 0; i < w.video.toolbar.itemToggled.size(); i++)
        std::cout << (i ? "," : "") << w.video.toolbar.itemToggled[i];
    std::cout << "]";
    Visuals *v = w.visual;
    if (v) {
        std::cout << ",\"coeff\":[" << num(v->coeffW) << "," << num(v->coeffH) << "],\"from\":" << vec(v->from)
                  << ",\"to\":" << vec(v->to) << ",\"lastmove\":" << vec(v->lastmove) << ",\"firstmove\":"
                  << vec(v->firstmove) << ",\"axis\":" << int(v->axis) << ",\"blockevents\":" << boolean(v->blockevents)
                  << ",\"captured\":" << boolean(w.video.captured) << ",\"moveValues\":[";
        for (int i = 0; i < 7; i++)
            std::cout << (i ? "," : "") << num(v->moveValues[i]);
        std::cout << "]";
    }
    if (auto *m = dynamic_cast<MoveAll *>(v)) {
        std::cout << ",\"elems\":[";
        for (size_t i = 0; i < m->elems.size(); i++) {
            const moveElems *e = m->elems[i];
            std::cout << (i ? "," : "") << "{\"type\":" << int(e->type) << ",\"elem\":" << vec(e->elem);
            if (e->vectorPoints) {
                std::cout << ",\"points\":[";
                for (size_t j = 0; j < e->vectorPoints->size(); j++) {
                    const ClipPoint &p = (*e->vectorPoints)[j];
                    std::cout << (j ? "," : "") << "[" << num(p.x) << "," << num(p.y) << "," << json(p.type) << ","
                              << boolean(p.start) << "]";
                }
                std::cout << "]";
            }
            std::cout << "}";
        }
        std::cout << "],\"numElem\":" << m->numElem << ",\"selectedTags\":" << int(m->selectedTags)
                  << ",\"diffs\":[" << m->diffs.x << "," << m->diffs.y << "],\"beforeMove\":" << vec(m->beforeMove)
                  << ",\"drawingPos\":" << vec(m->drawingPos) << ",\"drawingOriginalPos\":" << vec(m->drawingOriginalPos)
                  << ",\"drawingScale\":" << vec(m->drawingScale) << ",\"scale\":" << vec(m->scale)
                  << ",\"vectorClipScale\":" << num(m->vectorClipScale) << ",\"vectorDrawScale\":"
                  << num(m->vectorDrawScale);
    } else if (auto *a = dynamic_cast<AllTags *>(v)) {
        std::cout << ",\"actualTag\":";
        printTags(std::cout, {a->actualTag});
        std::cout << ",\"currentTag\":" << a->currentTag << ",\"mode\":" << a->mode << ",\"tagMode\":" << a->tagMode
                  << ",\"multiplyCounter\":" << num(a->multiplyCounter) << ",\"sliderPositionY\":" << a->sliderPositionY
                  << ",\"sliderPositionDiff\":" << a->sliderPositionDiff << ",\"rholding\":" << boolean(a->rholding)
                  << ",\"selectedTag\":" << json(a->selectedTag) << ",\"floatFormat\":" << json(a->floatFormat)
                  << ",\"inCursor\":" << boolean(a->replaceTagsInCursorPosition) << ",\"sliders\":[";
        for (int i = 0; i < 4; i++) {
            const AllTagsSlider &s = a->slider[i];
            std::cout << (i ? "," : "") << "{\"thumb\":" << num(s.thumbValue) << ",\"first\":" << num(s.firstThumbValue)
                      << ",\"last\":" << num(s.lastThumbValue) << ",\"rect\":[" << num(s.left) << "," << num(s.top) << ","
                      << num(s.right) << "," << num(s.bottom) << "],\"at\":[" << num(s.x) << "," << num(s.y)
                      << "],\"thumbState\":" << s.thumbState << ",\"onThumb\":" << boolean(s.onThumb)
                      << ",\"onSlider\":" << boolean(s.onSlider) << ",\"holding\":" << boolean(s.holding) << "}";
        }
        std::cout << "]";
    }
    std::cout << ",\"drawn\":[";
    for (size_t i = 0; i < probeDrawn.size(); i++) {
        const ProbeDraw &d = probeDrawn[i];
        std::cout << (i ? "," : "") << "{\"kind\":\"" << d.kind << "\"";
        if (d.kind == "text") {
            std::cout << ",\"text\":" << json(d.text) << ",\"align\":" << d.type << ",\"colour\":" << d.colour
                      << ",\"rect\":[" << d.rect[0] << "," << d.rect[1] << "," << d.rect[2] << "," << d.rect[3] << "]";
        } else {
            if (d.kind == "primitive")
                std::cout << ",\"type\":" << d.type;
            else
                std::cout << ",\"colour\":" << d.colour;
            std::cout << ",\"points\":[";
            for (size_t j = 0; j < d.points.size(); j++)
                std::cout << (j ? "," : "") << "[" << num(d.points[j].first) << "," << num(d.points[j].second) << "]";
            std::cout << "]";
            if (d.kind == "primitive") {
                std::cout << ",\"colours\":[";
                for (size_t j = 0; j < d.colours.size(); j++)
                    std::cout << (j ? "," : "") << d.colours[j];
                std::cout << "]";
            }
        }
        std::cout << "}";
    }
    std::cout << "]}\n";
}

void setTagField(AllTagsSetting &t, const std::string &field, const std::string &value)
{
    const wxString v = wxString::FromUTF8(value);
    double d = 0;
    v.ToCDouble(&d);
    if (field == "name")
        t.name = v;
    else if (field == "tag")
        t.tag = v;
    else if (field == "min")
        t.rangeMin = d;
    else if (field == "max")
        t.rangeMax = d;
    else if (field == "value")
        t.values[0] = d;
    else if (field == "value2")
        t.values[1] = d;
    else if (field == "value3")
        t.values[2] = d;
    else if (field == "value4")
        t.values[3] = d;
    else if (field == "step")
        t.step = d;
    else if (field == "digits")
        t.digitsAfterDot = static_cast<unsigned char>(d);
    else if (field == "mode")
        t.mode = static_cast<unsigned char>(d);
    else if (field == "tagMode")
        t.tagMode = static_cast<int>(d);
    else if (field == "count")
        t.numOfValues = static_cast<unsigned char>(d);
}

} // namespace

int main()
{
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
            probeDrawn.clear();
            probeFiles.files.clear();
            VideoToolbar::tags.clear();
            Options.pathfull = L"/probe";
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
        } else if (key == "state-video") {
            std::string s;
            in >> s;
            w->video.state = s == "playing" ? Playing : s == "none" ? None : Paused;
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
        } else if (key == "stylecolour") {
            std::string c1, c2, c3, c4;
            in >> c1 >> c2 >> c3 >> c4;
            Styles *s = w->styles.back().get();
            s->PrimaryColour.SetAss(c1);
            s->SecondaryColour.SetAss(c2);
            s->OutlineColour.SetAss(c3);
            s->BackColour.SetAss(c4);
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
        } else if (key == "hidden") {
            int i;
            while (in >> i)
                w->file.dialogues[static_cast<size_t>(i)]->isVisible.value = 0;
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
        } else if (key == "config") {
            std::string path;
            in >> path;
            Options.pathfull = wxString::FromUTF8(path);
        } else if (key == "file") {
            const auto space = text.find(' ');
            const auto bar = text.find('|');
            const std::string path = text.substr(space + 1, bar - space - 1);
            const wxString content = unescape(text.substr(bar + 1));
            if (wxString *f = probeFiles.find(wxString::FromUTF8(path)))
                *f = content;
            else
                probeFiles.files.emplace_back(path, content);
        } else if (key == "loadtags") {
            VideoToolbar::tags.clear();
            VideoToolbar::GetTagsSettings();
        } else if (key == "tagset") {
            size_t index;
            std::string field, value;
            in >> index >> field;
            std::getline(in, value);
            value.erase(0, value.find_first_not_of(' '));
            setTagField(VideoToolbar::tags[index], field, value);
        } else if (key == "savetags") {
            SaveSettings(VideoToolbar::GetTagsSettings());
        } else if (key == "tool") {
            in >> w->toolName >> w->toggled;
            w->grid.snapshot = w->grid.texts();
            w->loadEditor();
            w->setVisual();
        } else if (key == "option") {
            in >> w->toggled;
            w->visual->ChangeTool(w->toggled);
        } else if (key == "setvisual") {
            w->loadEditor();
            w->setVisual();
        } else if (key == "press" || key == "release" || key == "move" || key == "dclick") {
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
            if (key == "dclick") {
                type = wxEVT_LEFT_DCLICK;
                left = true;
            }
            if (key == "release") {
                type = button == "left" ? wxEVT_LEFT_UP : button == "right" ? wxEVT_RIGHT_UP : wxEVT_MIDDLE_UP;
                left = right = middle = false;
            }
            wxMouseEvent e = mouse(type, x, y, left, right, middle, shift);
            w->visual->OnMouseEvent(e);
        } else if (key == "wheel") {
            int x, y, steps;
            in >> x >> y >> steps;
            std::string word;
            bool shift = false;
            while (in >> word)
                shift = shift || word == "shift";
            wxMouseEvent e = mouse(wxEVT_MOUSEWHEEL, x, y, false, false, false, shift);
            e.m_wheelRotation = steps * 120;
            e.m_wheelDelta = 120;
            w->visual->OnMouseEvent(e);
        } else if (key == "leave") {
            int x, y;
            in >> x >> y;
            wxMouseEvent e = mouse(wxEVT_LEAVE_WINDOW, x, y, false, false, false, false);
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
            probeDrawn.clear();
            w->visual->Draw(w->video.time);
        } else if (key == "state") {
            printState(*w, caseName, step++);
            probeDrawn.clear();
        }
    }
    return 0;
}
