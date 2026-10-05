// T2: the legacy Position and Move tools. Runs them on the cases in
// inputs/position-cases.txt and prints one JSON object per dump.
//
// Compiled with this file: VisualPosition.cpp and VisualMove.cpp unchanged,
// Timebase.cpp, and, copied unchanged by extract_functions.py, the class
// declarations of Visuals, PosData, Position, Move (Visuals.h), FindData and
// TagFindReplace (TagFindReplace.h), TagData and ParseData (SubsDialogue.h),
// and the definitions they call: Visuals' constructor, SizeChanged, SetVisual
// (both), Draw, GetPosition, GetPosnScale, GetMoveTimes, GetDialoguesWithout-
// Position, GetDialogueAdditionalPosition, GetRectFromSize, IsInRect,
// RenderSubs, SetModified, GetTextSize, GetDrawingSize, GetVectorPoints,
// Curve and the wx drawing (Visuals.cpp); FindTag, Replace, ChangeText,
// GetPositionInText, GetDouble, TagValueToStyle and FindBrackets
// (TagFindReplace.cpp); Dialogue::GetDefaultPosition, ParseTags, ClearParse
// and TagData/ParseData (SubsDialogue.cpp); Styles' constructor, Copy and
// Get*Double (styles.cpp); CalcMovePosition (UtilsWindows.cpp); getfloat and
// FindFromEnd (config.cpp). The stand-ins are in position/standins.h. The
// Direct3D drawing (DrawRect, DrawCircle, DrawCross, DrawArrow,
// DrawDashedLine, DrawWarning) is recorded instead of drawn; DrawArrow's
// arithmetic is Visuals.cpp:308-336's and DrawDashedLine's loop
// Visuals.cpp:402-423's, each dash recorded where line->Draw draws it.
//
// Case file lines:
//   case <name>
//   view <left> <top> <right> <bottom> <zoomMoveX> <zoomMoveY> <zoomScaleX> <zoomScaleY>
//                                     the video rectangle (SizeChanged) and SetZoom
//   script <width> <height>           GetASSRes
//   timecodes <ms> ...                the video's frame starts (Timebase::FromTimecodes, fps 0)
//   time <ms>                         VideoBox::Tell
//   style <ASS style fields after "Style: ">
//   line <start> <end> <style> <marginL> <marginR> <marginV> <comment 0|1> <text>[|<translation>]
//   tlmode                            TLMode (SubsGrid::hasTLMode)
//   active <row>                      the active Line (currentLine, edit->line)
//   select <row> ...                  the selection (default: the active Line)
//   tool position|move <value>        the family and its toolbar value (GetItemToggled)
//   option <value>                    a toolbar change (VisualChangeTool -> ChangeTool(value))
//   mouse <type> <x> <y> [left] [right] [middle] [shift] [ctrl] [alt]
//                                     type: ldown lup rdown rup mdown mup move ldclick
//   key <char> [shift] [ctrl] [alt]   OnKeyPress
//   activate <row>                    another active Line (SubsGrid::SelectRow: EditBox::SetLine,
//                                     then the tool set again, RendererVideo::SetVisual)
//   dump                              the Lines, history, log, tool state and drawing
//   end
#include "standins.h"
// The probe reads the tools' private state, so their members are opened up.
#define private public
#include "Visuals.h"
#undef private

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

ProbeOptions Options;
std::vector<std::string> probeLog;
std::function<void()> probeSetVisual;

// Visuals::GetPosnScale casts to DrawingAndClip for the clip and drawing
// families only; the type must be complete.
class DrawingAndClip : public Visuals {
public:
    int vectorScale = 1;
};

#include "extracted.inc"

namespace {

struct Draw {
    std::string kind;
    float x = 0, y = 0, x2 = 0, y2 = 0, size = 0;
    unsigned color = 0;
};
std::vector<Draw> probeDraws;

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
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.9g", v);
    return buffer;
}

std::string vec(const D3DXVECTOR2 &v)
{
    return "[" + num(v.x) + "," + num(v.y) + "]";
}

} // namespace

// The Direct3D drawing, recorded.
void Visuals::DrawRect(D3DXVECTOR2 pos, bool sel, float size)
{
    probeDraws.push_back({sel ? "rectsel" : "rect", pos.x, pos.y, 0, 0, size, 0});
}
void Visuals::DrawCircle(D3DXVECTOR2 pos, bool sel, float size)
{
    probeDraws.push_back({sel ? "circlesel" : "circle", pos.x, pos.y, 0, 0, size, 0});
}
void Visuals::DrawCross(D3DXVECTOR2 position, D3DCOLOR color, bool)
{
    probeDraws.push_back({"cross", position.x, position.y, 0, 0, 0, color});
}
void Visuals::DrawArrow(D3DXVECTOR2 from, D3DXVECTOR2 *to, int diff)
{
    // Visuals.cpp:308-336's arithmetic; the triangle is recorded by its corners.
    D3DXVECTOR2 pdiff = from - (*to);
    float len = sqrt((pdiff.x * pdiff.x) + (pdiff.y * pdiff.y));
    D3DXVECTOR2 diffUnits = (len == 0) ? D3DXVECTOR2(0, 0) : pdiff / len;
    D3DXVECTOR2 pend = (*to) + (diffUnits * (12 + diff));
    D3DXVECTOR2 halfbase = D3DXVECTOR2(-diffUnits.y, diffUnits.x) * 5.f;
    D3DXVECTOR2 v3[3];
    v3[0] = pend - diffUnits * 12;
    v3[1] = pend + halfbase;
    v3[2] = pend - halfbase;
    // The triangle: its tip and where the line now ends, then its base.
    probeDraws.push_back({"arrow", v3[0].x, v3[0].y, pend.x, pend.y, 0, 0});
    probeDraws.push_back({"arrowbase", v3[1].x, v3[1].y, v3[2].x, v3[2].y, 0, 0});
    *to = pend;
}
void Visuals::DrawDashedLine(D3DXVECTOR2 *vector, size_t vectorSize, int dashLen, unsigned int color)
{
    // The whole line, then each dash as Visuals.cpp:402-423 draws it (its
    // loop, with line->Draw recorded).
    if (vectorSize >= 2)
        probeDraws.push_back({"dashed", vector[0].x, vector[0].y, vector[1].x, vector[1].y, 0, color});
    D3DXVECTOR2 actualPoint[2];
    for (size_t i = 0; i < vectorSize - 1; i++){
        size_t iPlus1 = (i < (vectorSize - 1)) ? i + 1 : 0;
        D3DXVECTOR2 pdiff = vector[i] - vector[iPlus1];
        float len = sqrt((pdiff.x * pdiff.x) + (pdiff.y * pdiff.y));
        if (len == 0){ return; }
        D3DXVECTOR2 diffUnits = pdiff / len;
        float singleMovement = 1 / (len / (dashLen * 2));
        actualPoint[0] = vector[i];
        actualPoint[1] = actualPoint[0];
        for (float j = 0; j <= 1; j += singleMovement){
            actualPoint[1] -= diffUnits * dashLen;
            if (j + singleMovement >= 1){ actualPoint[1] = vector[iPlus1]; }
            probeDraws.push_back({"dash", actualPoint[0].x, actualPoint[0].y, actualPoint[1].x, actualPoint[1].y, 0, color});
            actualPoint[1] -= diffUnits * dashLen;
            actualPoint[0] -= (diffUnits * dashLen) * 2;
        }
    }
}
void Visuals::DrawWarning(bool comment)
{
    probeDraws.push_back({comment ? "warning-comment" : "warning", 0, 0, 0, 0, 0, 0});
}

// GetLineTextExtents with a fixed measure: half the font size a character
// (spacing added per character when not zero), the height 1.25 times the
// size, the descent 0.25 and the external leading 0.125 times, scaled by
// ScaleX/ScaleY as legacy's tail does (UtilsWindows.cpp:331-339).
bool GetLineTextExtents(const wxString &text, Styles *style, float *width, float *height, float *descent,
                        float *extlead)
{
    const float fontsize = style->GetFontSizeDouble() * 64.f;
    const float spacing = wxAtof(style->Spacing) * 64.f;
    const size_t len = text.length();
    if (!len) {
        *width = 0;
        *height = 0;
        if (descent)
            *descent = 0;
        if (extlead)
            *extlead = 0;
        return true;
    }
    float fwidth = 0;
    if (spacing != 0) {
        for (size_t i = 0; i < len; i++)
            fwidth += fontsize * 0.5f + spacing;
    } else {
        fwidth = fontsize * 0.5f * static_cast<float>(len);
    }
    if (style->Bold)
        fwidth *= 1.25f;
    const float fheight = fontsize * 1.25f;
    const float fdescent = fontsize * 0.25f;
    const float fextlead = fontsize * 0.125f;
    float scalex = wxAtof(style->ScaleX) / 100.f;
    float scaley = wxAtof(style->ScaleY) / 100.f;
    *width = scalex * (fwidth / 64.f);
    *height = scaley * (fheight / 64.f);
    if (descent)
        *descent = scaley * (fdescent / 64.f);
    if (extlead)
        *extlead = scaley * (fextlead / 64.f);
    return true;
}

static const char *historyName(unsigned char type)
{
    // SubsFile.cpp:228-238.
    switch (type) {
    case VISUAL_POSITION: return "Visual positioning tool";
    case VISUAL_MOVE: return "Visual movement tool";
    case EDITBOX_LINE_EDITION: return "Line edition";
    default: return "other";
    }
}

static TabPanel *probeTab = nullptr;

void SubsGrid::SetModified(unsigned char editionType, bool redit, bool dummy, int, bool)
{
    if (file->edited) {
        history.push_back(historyName(editionType));
        file->edited = false;
        if (redit)
            tab->edit->Load();
        if (!dummy && probeSetVisual)
            probeSetVisual();
    } else if (redit) {
        tab->edit->Load();
    }
}

void EditBox::Load()
{
    // EditBox::SetLine: edit->line is a copy of the active Line, its text in
    // the editors (TLMode: the translation in TextEdit, the original in
    // TextEditOrig).
    Dialogue *active = grid->file->GetDialogue(grid->currentLine);
    delete line;
    line = new Dialogue(*active);
    if (grid->hasTLMode) {
        TextEdit->value = line->TextTl;
        TextEditOrig->value = line->Text;
    } else {
        TextEdit->value = line->Text;
        TextEditOrig->value = emptyString;
    }
}

void EditBox::Send(unsigned char editionType, bool, bool, bool visualdummy)
{
    // EditBox::Send (EditBox.cpp:543-641): the modified editor's text into
    // the active Line (the visual tools mark it modified first), then
    // SubsGrid::ChangeLine's SetModified(editionType, false, visualdummy)
    // (SubsGridBase.cpp:133-155): the editor is not reloaded and, for a
    // visual tool's commit, the tool is not set again.
    Dialogue *active = grid->CopyDialogue(grid->currentLine);
    if (grid->hasTLMode) {
        if (TextEditOrig->modified)
            active->Text = TextEditOrig->value;
        if (TextEdit->modified)
            active->TextTl = TextEdit->value;
    } else {
        active->Text = TextEdit->value;
    }
    TextEdit->modified = TextEditOrig->modified = false;
    line->Text = active->Text;
    line->TextTl = active->TextTl;
    grid->SetModified(editionType, false, visualdummy);
}

namespace {

std::vector<wxString> split(const wxString &s, wxChar sep)
{
    std::vector<wxString> out;
    wxString cur;
    for (const auto c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

Styles *styleFrom(const wxString &fields)
{
    // Styles(styledata, ASS): the positional fields after "Style: ".
    const auto f = split(fields, L',');
    Styles *s = new Styles();
    auto at = [&](size_t i) { return i < f.size() ? f[i] : wxString(); };
    s->Name = at(0);
    s->Fontname = at(1);
    s->Fontsize = at(2);
    s->Bold = at(7) == L"-1" || at(7) == L"1";
    s->Italic = at(8) == L"-1" || at(8) == L"1";
    s->Underline = at(9) == L"-1" || at(9) == L"1";
    s->StrikeOut = at(10) == L"-1" || at(10) == L"1";
    s->ScaleX = at(11);
    s->ScaleY = at(12);
    s->Spacing = at(13);
    s->Angle = at(14);
    s->BorderStyle = at(15) == L"3";
    s->Outline = at(16);
    s->Shadow = at(17);
    s->Alignment = at(18);
    s->MarginL = at(19);
    s->MarginR = at(20);
    s->MarginV = at(21);
    s->Encoding = at(22);
    return s;
}

struct Case {
    std::string name;
    wxRect view;
    D3DXVECTOR2 zoomMove{0, 0}, zoomScale{1, 1};
    SubsFile file;
    SubsGrid grid;
    EditBox edit;
    VideoBox video;
    TabPanel tab;
    TextEditor textEdit, textEditOrig;
    std::vector<int> timecodes;
    Visuals *visual = nullptr;
    int family = CHANGEPOS;
    int tool = 0;
    ID3DXLine line;
    IDirect3DDevice9 device;
    int dumps = 0;
};

void dump(Case &c)
{
    probeDraws.clear();
    c.visual->Draw(c.video.time);
    std::string out = "{\"case\":" + json(c.name) + ",\"dump\":" + std::to_string(c.dumps++) + ",\"lines\":[";
    for (size_t i = 0; i < c.file.dialogues.size(); i++) {
        out += (i ? "," : "") + std::string("[") + json(c.file.dialogues[i]->Text) + "," +
               json(c.file.dialogues[i]->TextTl) + "]";
    }
    out += "],\"history\":[";
    for (size_t i = 0; i < c.grid.history.size(); i++)
        out += (i ? "," : "") + json(c.grid.history[i]);
    out += "],\"log\":[";
    for (size_t i = 0; i < probeLog.size(); i++)
        out += (i ? "," : "") + json(probeLog[i]);
    out += "],\"editor\":" + json(c.edit.TextEdit->value);
    out += ",\"coeff\":[" + num(c.visual->coeffW) + "," + num(c.visual->coeffH) + "]";
    if (c.family == CHANGEPOS) {
        auto *p = static_cast<Position *>(c.visual);
        out += ",\"data\":[";
        for (size_t i = 0; i < p->data.size(); i++) {
            const PosData *d = p->data[i];
            out += (i ? "," : "") + std::string("{\"row\":") + std::to_string(d->numpos) + ",\"pos\":" + vec(d->pos) +
                   ",\"lastpos\":" + vec(d->lastpos) + ",\"textPos\":[" + std::to_string(d->TextPos.x) + "," +
                   std::to_string(d->TextPos.y) + "],\"bracket\":" + (d->putinBracket ? "true" : "false");
            if (d->moveTable)
                out += ",\"move\":[" + num(d->moveTable[0]) + "," + num(d->moveTable[1]) + "," +
                       num(d->moveTable[2]) + "," + num(d->moveTable[3]) + "," + num(d->moveTable[4]) + "]";
            out += "}";
        }
        out += "],\"rect\":[" + vec(p->PositionRectangle[0]) + "," + vec(p->PositionRectangle[1]) +
               "],\"rectVisible\":" + (p->rectangleVisible ? "true" : "false") +
               ",\"alignment\":" + std::to_string(p->alignment) +
               ",\"curLineAlignment\":" + std::to_string(p->curLineAlingment);
    } else {
        auto *m = static_cast<Move *>(c.visual);
        out += ",\"from\":" + vec(m->from) + ",\"to\":" + vec(m->to) + ",\"moveStart\":" +
               std::to_string(m->moveStart) + ",\"moveEnd\":" + std::to_string(m->moveEnd) +
               ",\"twoPoints\":[" + vec(m->lineToMoveStart) + "," + vec(m->lineToMoveEnd) + "]";
    }
    out += ",\"blocked\":" + std::string(c.visual->blockevents ? "true" : "false");
    out += ",\"draws\":[";
    for (size_t i = 0; i < probeDraws.size(); i++) {
        const Draw &d = probeDraws[i];
        out += (i ? "," : "") + std::string("[") + json(d.kind) + "," + num(d.x) + "," + num(d.y) + "," +
               num(d.x2) + "," + num(d.y2) + "," + num(d.size) + "," + std::to_string(d.color) + "]";
    }
    out += "]}";
    std::printf("%s\n", out.c_str());
}

bool has(const std::vector<std::string> &words, const char *w)
{
    for (const auto &x : words)
        if (x == w)
            return true;
    return false;
}

} // namespace

int main()
{
    std::string text;
    Case *c = nullptr;
    while (std::getline(std::cin, text)) {
        if (text.empty() || text[0] == '#')
            continue;
        std::istringstream in(text);
        std::string op;
        in >> op;
        if (op == "case") {
            c = new Case();
            in >> c->name;
            c->grid.file = &c->file;
            c->grid.tab = &c->tab;
            c->edit.grid = &c->grid;
            c->edit.TextEdit = &c->textEdit;
            c->edit.TextEditOrig = &c->textEditOrig;
            c->tab.video = &c->video;
            c->tab.grid = &c->grid;
            c->tab.edit = &c->edit;
            probeLog.clear();
        } else if (op == "view") {
            int l, t, r, b;
            in >> l >> t >> r >> b >> c->zoomMove.x >> c->zoomMove.y >> c->zoomScale.x >> c->zoomScale.y;
            c->view = wxRect(l, t, r, b); // SizeChanged's wxRect(left, top, right, bottom)
        } else if (op == "script") {
            in >> c->grid.subsWidth >> c->grid.subsHeight;
        } else if (op == "timecodes") {
            int ms;
            while (in >> ms)
                c->timecodes.push_back(ms);
            c->video.timebase = Timebase::FromTimecodes(c->timecodes, 0.f);
        } else if (op == "time") {
            in >> c->video.time;
        } else if (op == "style") {
            std::string rest;
            std::getline(in >> std::ws, rest);
            c->file.styles.push_back(styleFrom(wxString::FromUTF8(rest)));
        } else if (op == "line") {
            auto *d = new Dialogue();
            int comment = 0;
            std::string style;
            in >> d->Start.mstime >> d->End.mstime >> style >> d->MarginL >> d->MarginR >> d->MarginV >> comment;
            d->Style = wxString::FromUTF8(style);
            d->IsComment = comment != 0;
            std::string rest;
            std::getline(in >> std::ws, rest);
            const wxString all = wxString::FromUTF8(rest);
            const int bar = all.Find(L'|');
            if (bar >= 0) {
                d->Text = all.Mid(0, bar);
                d->TextTl = all.Mid(bar + 1);
            } else {
                d->Text = all;
            }
            c->file.dialogues.push_back(d);
        } else if (op == "tlmode") {
            c->grid.hasTLMode = true;
        } else if (op == "active") {
            in >> c->grid.currentLine;
        } else if (op == "select") {
            int row;
            while (in >> row)
                c->file.selections.insert(row);
        } else if (op == "tool") {
            std::string family;
            in >> family >> c->tool;
            c->family = family == "move" ? MOVE : CHANGEPOS;
            if (c->file.selections.empty())
                c->file.selections.insert(c->grid.currentLine);
            c->edit.Load();
            c->visual = c->family == MOVE ? static_cast<Visuals *>(new Move()) : new Position();
            c->visual->tab = &c->tab;
            c->visual->Visual = static_cast<unsigned char>(c->family);
            c->visual->SetTabPanel(&c->tab);
            Case *cc = c;
            probeSetVisual = [cc] {
                // RendererVideo::SetVisual (RendererVideo.cpp:1096-1121).
                cc->visual->SizeChanged(cc->view, &cc->line, nullptr, &cc->device);
                cc->visual->SetZoom(cc->zoomMove, cc->zoomScale);
                cc->visual->SetVisual(cc->edit.line, cc->tool);
            };
            probeSetVisual();
        } else if (op == "option") {
            in >> c->tool;
            c->visual->ChangeTool(c->tool);
        } else if (op == "mouse") {
            std::string type;
            int x, y;
            in >> type >> x >> y;
            std::vector<std::string> words;
            std::string w;
            while (in >> w)
                words.push_back(w);
            wxEventType et = type == "ldown" ? wxEVT_LEFT_DOWN : type == "lup" ? wxEVT_LEFT_UP
                           : type == "rdown" ? wxEVT_RIGHT_DOWN : type == "rup" ? wxEVT_RIGHT_UP
                           : type == "mdown" ? wxEVT_MIDDLE_DOWN : type == "mup" ? wxEVT_MIDDLE_UP
                           : type == "ldclick" ? wxEVT_LEFT_DCLICK : wxEVT_MOTION;
            wxMouseEvent evt(et);
            evt.SetX(x);
            evt.SetY(y);
            evt.SetLeftDown(has(words, "left"));
            evt.SetRightDown(has(words, "right"));
            evt.SetMiddleDown(has(words, "middle"));
            evt.SetShiftDown(has(words, "shift"));
            evt.SetControlDown(has(words, "ctrl"));
            evt.SetAltDown(has(words, "alt"));
            c->visual->Draw(c->video.time); // the render before the event (blockevents)
            c->visual->OnMouseEvent(evt);
        } else if (op == "key") {
            std::string k;
            in >> k;
            std::vector<std::string> words;
            std::string w;
            while (in >> w)
                words.push_back(w);
            wxKeyEvent evt(wxEVT_KEY_DOWN);
            evt.m_keyCode = k[0];
            evt.SetShiftDown(has(words, "shift"));
            evt.SetControlDown(has(words, "ctrl"));
            evt.SetAltDown(has(words, "alt"));
            c->visual->Draw(c->video.time);
            c->visual->OnKeyPress(evt);
        } else if (op == "activate") {
            in >> c->grid.currentLine;
            c->file.selections = {c->grid.currentLine};
            c->edit.Load();
            probeSetVisual();
        } else if (op == "dump") {
            dump(*c);
        } else if (op == "end") {
            probeSetVisual = nullptr;
            c = nullptr; // leaked: the probe is short-lived
        }
    }
    return 0;
}
