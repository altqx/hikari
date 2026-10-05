// Stand-ins for the legacy GUI classes the Scale and rotation tools touch
// (T3). The probe compiles copies of the legacy VisualScale.cpp,
// VisualRotationZ.cpp and VisualRotationXY.cpp next to headers that all
// include this one, and definitions and classes copied unchanged out of
// Visuals.h/.cpp, TagFindReplace.h/.cpp, styles.h/.cpp, SubsDialogue.h/.cpp,
// VisualClipPoint.h, VisualClips.cpp, RendererVideo.cpp and config.cpp
// (extract_functions.py). Each stand-in keeps the legacy member names and
// types those definitions read; what the GUI would do (redraw, cursor,
// history, the subtitles preview) is recorded instead.
#pragma once

#include <wx/arrstr.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/dynarray.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/intl.h>
#include <wx/regex.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/tokenzr.h>
#include <wx/window.h>

#include "d3dx9.h" // the legacy Linux D3DX stand-ins
#include "Timebase.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// config.h:526-560, unchanged.
#define SAFE_DELETE(x) if (x !=nullptr) { delete x; x = nullptr; }
#define SAFE_RELEASE(x) if (x != nullptr) { x->Release(); x = nullptr; }
#define HR(what,err) if(FAILED(what)) {HikariLogSilent(err); return false;}
#define HRN(what,err) if(FAILED(what)) {HikariLogSilent(err); return;}
#define MIN(a,b) ((a)<(b))?(a):(b)
#define MAX(a,b) ((a)>(b))?(a):(b)
#define MID(a,b,c) MAX((a),MIN((b),(c)))
// SubsDialogue.h:20.
#define ZEROIT(a) ((a/10)*10)

extern std::vector<std::string> probeLog;
inline void HikariLogSilent(const wxString &text)
{
    probeLog.push_back(std::string(text.utf8_str()));
}
inline void HikariLog(const wxString &text)
{
    probeLog.push_back(std::string(text.utf8_str()));
}

const wxString emptyString;

#define DRAWOUTTEXT(font, text, rect, align, color) (void)0

// config.h:601 and 612 (the definitions are copied from config.cpp).
wxString getfloat(float num, const wxString& format = L"5.3f", bool Truncate = true);
size_t FindFromEnd(const wxString& text, const wxString& whatToFind, bool ignoreCase = false);

enum { VIDEO_VISUAL_WARNINGS_OFF, TL_MODE_HIDE_ORIGINAL_ON_VIDEO };
struct ProbeOptions {
    bool GetBool(int) const { return false; }
};
extern ProbeOptions Options;

// Visuals.h:56-68.
enum{
	CROSS = 0,
	CHANGEPOS,
	MOVE,
	SCALE,
	ROTATEZ,
	ROTATEXY,
	CLIPRECT,
	VECTORCLIP,
	VECTORDRAW,
	MOVEALL,
	ALL_TAGS
};

// The history names Visuals::SetVisual and SetModified pass (SubsFile.h);
// recorded by name.
enum { VISUAL_POSITION = 38, VISUAL_MOVE, VISUAL_SCALE, VISUAL_ROTATION_Z, VISUAL_ROTATION_X_Y, VISUAL_RECT_CLIP,
       VISUAL_VECTOR_CLIP, VISUAL_DRAWING, VISUAL_POSITION_SHIFTER, VISUAL_ALL_TAGS };
enum PlaybackState { Playing, Paused, Stopped, None };
enum { OPEN_DUMMY = 1 };

// RendererVideo.h:56-79.
struct VERTEX {
    float fX, fY, fZ;
    D3DCOLOR Color;
};
void CreateVERTEX(VERTEX * v, float X, float Y, D3DCOLOR Color, float Z = 0.0f);

// styles.h (AssColor, Styles), copied.
class ParseData;
#include "styles_classes.inc"

// SubsDialogue.h (TagData, ParseData), copied.
#include "parse_classes.inc"

// The Dialogue members the definitions read (SubsDialogue.h:333-420), with
// GetTextNoCopy, GetText and SetText as SubsDialogue.cpp:216-232 has them
// (Text and TextTl are StoreTextHelpers there; plain strings here).
struct STime {
    int mstime = 0;
};
class Visibility {
public:
    unsigned char value = 1;
    bool operator!() const { return value == 0; }
};
class Dialogue {
public:
    wxString Text, TextTl, Style;
    STime Start, End;
    int MarginL = 0, MarginR = 0, MarginV = 0;
    bool IsComment = false;
    bool NonDialogue = false;
    Visibility isVisible;
    ParseData *parseData = nullptr;
    Dialogue() = default;
    Dialogue(const Dialogue &d)
        : Text(d.Text), TextTl(d.TextTl), Style(d.Style), Start(d.Start), End(d.End), MarginL(d.MarginL),
          MarginR(d.MarginR), MarginV(d.MarginV), IsComment(d.IsComment), NonDialogue(d.NonDialogue),
          isVisible(d.isVisible)
    {
    }
    ~Dialogue() { ClearParse(); }
    const wxString &GetTextNoCopy() { return (TextTl != emptyString) ? TextTl : Text; }
    wxString &GetText() { return (TextTl != emptyString) ? TextTl : Text; }
    void SetText(const wxString &text)
    {
        if (TextTl != emptyString)
            TextTl = text;
        else
            Text = text;
    }
    // The preview's raw line (only its length is used, to place the next).
    void GetRaw(wxString *raw, bool tl = false, const wxString & = emptyString)
    {
        *raw << (tl ? TextTl : Text) << L"\n";
    }
    void ClearParse();
    ParseData *ParseTags(wxString *tags, size_t ntags, bool plainText, const wxString &textToParse = L"");
    void GetDefaultPosition(Styles *lineStyle, int an, const wxSize &subsSize, float *posx, float *posy);
};

// UtilsWindows.cpp:233: GetLineTextExtents. The probe has no fonts; its
// measure is the rewrite tests' fixed one: a character half the font size
// wide, the font size high, a descent of a fifth and a leading of a tenth,
// all times ScaleX / ScaleY.
bool GetLineTextExtents(const wxString &text, Styles *style, float *width, float *height, float *descent, float *extlead);

class SubsFile {
public:
    std::vector<Dialogue *> dialogues;
    std::vector<Styles *> styles;
    std::vector<int> selections;
    size_t GetCount() { return dialogues.size(); }
    Dialogue *GetDialogue(size_t i) { return dialogues[i]; }
    // SubsFile::GetStyle (SubsFile.cpp:818-830).
    Styles *GetStyle(size_t i, const wxString &name = emptyString)
    {
        if (name != emptyString) {
            for (size_t j = 0; j < styles.size(); j++)
                if (name == styles[j]->Name)
                    return styles[j];
        }
        if (!styles.size())
            styles.push_back(new Styles());
        return styles[i];
    }
    void GetSelections(wxArrayInt &sels)
    {
        sels.clear();
        for (int s : selections)
            sels.push_back(s);
    }
    size_t SelectionsSize() { return selections.size(); }
    const wxString &GetSInfo(const wxString &) { return emptyString; }
};

class SubsGrid {
public:
    SubsFile *file = nullptr;
    int subsWidth = 0, subsHeight = 0;
    bool hasTLMode = false;
    bool ignoreFiltered = false;
    int currentLine = 0;
    std::vector<std::string> history; // "dummy <action>" / "<action>"
    void GetASSRes(int *x, int *y) { *x = subsWidth; *y = subsHeight; }
    // SubsGrid::CopyDialogue: the Line to change (the probe's are its own).
    Dialogue *CopyDialogue(size_t i) { return file->dialogues[i]; }
    void SetModified(int action, bool = true, bool dummy = false)
    {
        history.push_back((dummy ? "dummy " : "") + std::to_string(action));
    }
    void Refresh(bool = true) {}
    // SubsGrid::GetVisible: the subtitles shown, with the selected Lines'
    // places; the probe's preview is empty, a place for each selection.
    wxString *GetVisible(bool *visible, wxPoint *dumplaced = nullptr, wxArrayInt *selPositions = nullptr)
    {
        if (visible)
            *visible = true;
        if (dumplaced)
            *dumplaced = wxPoint(0, 0);
        if (selPositions)
            for (size_t i = 0; i < file->selections.size(); i++)
                selPositions->push_back(0);
        return new wxString();
    }
};

class TextEditor {
public:
    wxString value;
    long from = 0, to = 0;
    bool modified = false;
    wxString GetValue() const { return value; }
    void SetTextS(const wxString &text, bool = false, bool = false) { value = text; }
    void SetSelection(long start, long end, bool = false)
    {
        from = start;
        to = end;
    }
    void GetSelection(long *start, long *end) const
    {
        *start = from;
        *end = to;
    }
    void SetModified() { modified = true; }
};

class EditBox {
public:
    Dialogue *line = nullptr;
    SubsGrid *grid = nullptr;
    TextEditor *TextEdit = nullptr;
    TextEditor *TextEditOrig = nullptr;
    bool splittedTags = false;
    std::vector<std::string> sent; // "<action> <text>"
    int Visual = 0;
    // EditBox::IsCursorOnStart (EditBox.cpp:1922-1925).
    bool IsCursorOnStart() { return grid->file->SelectionsSize() > 1; }
    // EditBox::GetEditor (EditBox.cpp:2185-2197).
    TextEditor *GetEditor(const wxString &text = emptyString)
    {
        if (!text.empty()) {
            if (TextEditOrig->GetValue() == text)
                return TextEditOrig;
            return TextEdit;
        }
        else if (grid->hasTLMode && TextEdit->GetValue() == emptyString)
            return TextEditOrig;
        return TextEdit;
    }
    // EditBox::Send: the editors go into the Line (the original editor's
    // into Text, the other into TextTl in TLMode, else Text) and the grid's.
    void Send(int action, bool = false, bool = false, bool = false)
    {
        if (grid->hasTLMode) {
            line->Text = TextEditOrig->GetValue();
            line->TextTl = TextEdit->GetValue();
        } else {
            line->Text = TextEdit->GetValue();
        }
        Dialogue *gridLine = grid->file->dialogues[grid->currentLine];
        gridLine->Text = line->Text;
        gridLine->TextTl = line->TextTl;
        sent.push_back(std::to_string(action));
    }
};

class VideoBox {
public:
    int time = 0;
    Timebase timebase;
    int clientWidth = 0, clientHeight = 0, panelHeight = 0;
    bool captured = false;
    int lastCursor = -1;
    int renders = 0;
    int Tell() { return time; }
    const Timebase &GetTimebase() { return timebase; }
    bool HasVideo() { return true; }
    void SetVisualEdition(bool) {}
    void OpenOwnSubsLater(wxString *subs, bool = true) { delete subs; }
    PlaybackState GetState() { return Paused; }
    bool HasCapture() { return captured; }
    void CaptureMouse() { captured = true; }
    void ReleaseMouse() { captured = false; }
    bool HasArrow() { return true; }
    bool SetCursor(int cursor) { lastCursor = cursor; return true; }
    void Render(bool = true) { renders++; }
    bool IsShown() { return true; }
    bool IsFullScreen() { return false; }
    void OpenSubs(int) {}
    // VideoBox::GetWindowSize (VideoBox.cpp:2010-2022), not full screen.
    void GetWindowSize(int *x, int *y, bool withTabPanel = true)
    {
        *x = clientWidth;
        *y = clientHeight;
        if (!withTabPanel)
            *y -= panelHeight;
    }
};

class TabPanel {
public:
    VideoBox *video = nullptr;
    SubsGrid *grid = nullptr;
    EditBox *edit = nullptr;
};

// TagFindReplace.h (FindData, TagFindReplace), copied.
#include "tagfind_classes.inc"
wxPoint FindBrackets(const wxString& text, long from);

// VisualClipPoint.h (ClipPoint), copied.
class DrawingAndClip;
#include "clippoint_class.inc"

// Visuals.h (Visuals, Scale, RotationZ, RotationXY), copied, their members
// opened to the probe.
#define private public
#include "visuals_classes.inc"
#undef private

// Only Visuals::GetPosnScale's drawing branch names it (VECTORCLIP and
// VECTORDRAW), which the probe never runs.
class DrawingAndClip : public Visuals {
public:
    int vectorScale = 1;
};
