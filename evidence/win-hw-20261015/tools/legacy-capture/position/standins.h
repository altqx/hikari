// Stand-ins for the legacy GUI classes Position and Move touch (T2). The
// probe compiles copies of the legacy VisualPosition.cpp and VisualMove.cpp
// next to headers that all include this one; the class declarations of
// Visuals, PosData, Position, Move, FindData, TagFindReplace, TagData and
// ParseData, and the definitions they call, are copied unchanged out of their
// files (extract_functions.py). Each stand-in keeps the legacy member names
// and types those definitions read; what the GUI would do (cursor, render,
// history, the editor) is recorded instead.
#pragma once

#include <wx/arrstr.h>
#include <wx/brush.h>
#include <wx/cursor.h>
#include <wx/dc.h>
#include <wx/dynarray.h>
#include <wx/font.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/intl.h>
#include <wx/pen.h>
#include <wx/regex.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/tokenzr.h>
#include <wx/window.h>

#include "d3dx9.h" // the legacy Linux D3DX stand-ins (D3DXVECTOR2, ID3DXLine...)
#include "Timebase.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
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

// config.h:601, 612 (the definitions are copied from config.cpp).
wxString getfloat(float num, const wxString &format = L"5.3f", bool Truncate = true);
size_t FindFromEnd(const wxString &text, const wxString &whatToFind, bool ignoreCase = false);

// Visuals.h:56-68.
enum {
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

// SubsFile.h's history entries the tools record (their names are
// SubsFile.cpp:228-238's).
enum { EDITBOX_LINE_EDITION = 3, VISUAL_POSITION = 38, VISUAL_MOVE, VISUAL_SCALE, VISUAL_ROTATION_Z,
       VISUAL_ROTATION_X_Y, VISUAL_RECT_CLIP, VISUAL_VECTOR_CLIP, VISUAL_DRAWING, VISUAL_POSITION_SHIFTER,
       VISUAL_ALL_TAGS };
enum { OPEN_DUMMY = 2 };
enum PlaybackState : int { Playing, Paused, Stopped, None };

// The options the definitions read; both are off by default.
enum { TL_MODE_HIDE_ORIGINAL_ON_VIDEO, VIDEO_VISUAL_WARNINGS_OFF };
struct ProbeOptions {
    bool GetBool(int) const { return false; }
    wxFont *GetFont(int) const { return nullptr; } // DrawWarningWx's font; never drawn
};
extern ProbeOptions Options;

// AssColor: the definitions only assign colours.
class AssColor {
public:
    void SetAss(const wxString &) {}
    void SetAlphaString(const wxString &) {}
};

// Styles (styles.h): the fields, with the definitions the tools use copied
// from styles.cpp.
class Styles {
public:
    Styles();
    wxString Name, Fontname, Fontsize;
    AssColor PrimaryColour, SecondaryColour, OutlineColour, BackColour;
    bool Bold, Italic, Underline, StrikeOut;
    wxString ScaleX, ScaleY, Spacing, Angle;
    bool BorderStyle;
    wxString Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding;
    Styles *Copy();
    double GetOtlineDouble();
    double GetShadowDouble();
    double GetSpacingDouble();
    double GetScaleXDouble();
    double GetScaleYDouble();
    double GetFontSizeDouble();
};

// GetLineTextExtents (UtilsWindows.cpp:233-341) measures with GDI or wx;
// the probe measures with a fixed rule the rewrite's replay repeats
// (position_capture.cpp).
bool GetLineTextExtents(const wxString &text, Styles *style, float *width, float *height, float *descent = nullptr,
                        float *extlead = nullptr);

// CalcMovePosition (UtilsWindows.h:41; copied from UtilsWindows.cpp).
void CalcMovePosition(D3DXVECTOR2 *point, double *moveTable, int time);

#include "tagdata_class.inc"

struct ProbeTime {
    int mstime = 0;
};

// Dialogue (SubsDialogue.h): the fields the tools read, with GetDefaultPosition,
// ParseTags and ClearParse copied from SubsDialogue.cpp.
class Dialogue {
public:
    Dialogue() = default;
    Dialogue(const Dialogue &other)
        : Start(other.Start), End(other.End), Style(other.Style), Text(other.Text), TextTl(other.TextTl),
          MarginL(other.MarginL), MarginR(other.MarginR), MarginV(other.MarginV), IsComment(other.IsComment),
          NonDialogue(other.NonDialogue), isVisible(other.isVisible)
    {
    }
    ~Dialogue() { ClearParse(); }
    ProbeTime Start, End;
    wxString Style;
    wxString Text, TextTl;
    int MarginL = 0, MarginR = 0, MarginV = 0;
    bool IsComment = false, NonDialogue = false;
    unsigned char isVisible = 1;
    ParseData *parseData = nullptr;
    // SubsDialogue.cpp:216-231.
    const wxString &GetTextNoCopy() { return (TextTl != emptyString) ? TextTl : Text; }
    wxString &GetText() { return (TextTl != emptyString) ? TextTl : Text; }
    void SetText(const wxString &text)
    {
        if (TextTl != emptyString)
            TextTl = text;
        else
            Text = text;
    }
    // The preview's raw line: only rendered, never recorded.
    void GetRaw(wxString *txt, bool tl = false, const wxString &style = emptyString)
    {
        (void)style;
        *txt << (tl ? TextTl : Text) << L"\n";
    }
    void GetDefaultPosition(Styles *lineStyle, int an, const wxSize &subsSize, float *posx, float *posy);
    ParseData *ParseTags(wxString *tags, size_t ntags, bool plainText = false, const wxString &textToParse = L"");
    void ClearParse();
};

// VisualClipPoint.h: the point GetVectorPoints makes.
class ClipPoint {
public:
    ClipPoint(float _x, float _y, wxString _type, bool isstart) : x(_x), y(_y), type(_type), start(isstart) {}
    ClipPoint() = default;
    float x = 0, y = 0;
    wxString type;
    bool start = false;
    bool isSelected = false;
};

// The Line editor's text fields (TextEditor): the value and its selection.
class TextEditor {
public:
    wxString value;
    long from = 0, to = 0;
    bool modified = false;
    const wxString &GetValue() const { return value; }
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

// SubsFile: the Lines, the selection, the Styles and Script Info.
class SubsFile {
public:
    std::vector<Dialogue *> dialogues;
    std::set<int> selections;
    std::vector<Styles *> styles;
    std::map<wxString, wxString> sinfo;
    bool edited = false;
    Dialogue *GetDialogue(size_t i) { return i < dialogues.size() ? dialogues[i] : nullptr; }
    size_t GetCount() const { return dialogues.size(); }
    void GetSelections(wxArrayInt &sels) const
    {
        sels.clear();
        for (int s : selections)
            sels.Add(s);
    }
    size_t SelectionsSize() const { return selections.size(); }
    // SubsFile.cpp:818-830.
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
    const wxString &GetSInfo(const wxString &key, int * = 0)
    {
        static const wxString none;
        const auto it = sinfo.find(key);
        return it == sinfo.end() ? none : it->second;
    }
};

class Visuals;
class TabPanel;

// The renderer's part (RendererVideo::SetVisual after an edit): the probe's
// driver gives the tool its window and toolbar state again.
extern std::function<void()> probeSetVisual;

class SubsGrid {
public:
    SubsFile *file = nullptr;
    int currentLine = 0;
    bool hasTLMode = false;
    bool ignoreFiltered = false;
    int subsWidth = 0, subsHeight = 0;
    std::vector<std::string> history; // the entries SetModified recorded
    TabPanel *tab = nullptr;
    void GetASSRes(int *x, int *y) const
    {
        *x = subsWidth;
        *y = subsHeight;
    }
    Dialogue *CopyDialogue(size_t i)
    {
        file->edited = true;
        return file->GetDialogue(i);
    }
    // SubsGrid::SetModified (SubsGridBase.cpp:1125-1160): a history step
    // when a Line was copied, the editor reloaded (ShowEditedLine) and, unless
    // dummy, the tool set again (ShowEditOnVideo: SetVisual).
    void SetModified(unsigned char editionType, bool redit = true, bool dummy = false, int = -1, bool = true);
    // GetVisible for the preview: never rendered here.
    wxString *GetVisible(bool *visible, wxPoint *point = nullptr, wxArrayInt *selected = nullptr)
    {
        *visible = true;
        if (point)
            *point = wxPoint(0, 0);
        if (selected) {
            selected->clear();
            for (size_t i = 0; i < file->selections.size(); i++)
                selected->Add(0);
        }
        return new wxString();
    }
    void Refresh(bool = true) {}
};

class EditBox {
public:
    Dialogue *line = nullptr;
    TextEditor *TextEdit = nullptr;
    TextEditor *TextEditOrig = nullptr;
    bool splittedTags = false;
    SubsGrid *grid = nullptr;
    TextEditor *GetEditor() { return TextEdit; }
    // EditBox::IsCursorOnStart (EditBox.cpp:1922-1943).
    bool IsCursorOnStart() { return grid->file->SelectionsSize() > 1; }
    // EditBox::Send: the editor's text into the active Line, then SetModified.
    void Send(unsigned char editionType, bool = false, bool = false, bool = false);
    // ShowEditedLine's SetLine: the editor shows the active Line again.
    void Load();
};

class VideoBox {
public:
    int time = 0;
    Timebase timebase;
    bool captured = false;
    int lastCursor = -1;
    int Tell() const { return time; }
    const Timebase &GetTimebase() const { return timebase; }
    PlaybackState GetState() const { return Paused; }
    bool HasVideo() const { return true; }
    bool HasCapture() const { return captured; }
    void CaptureMouse() { captured = true; }
    void ReleaseMouse() { captured = false; }
    bool HasArrow() const { return true; }
    void SetCursor(int cursor) { lastCursor = cursor; }
    void SetCursor(const wxCursor &) {}
    void Render(bool = true) {}
    void SetVisualEdition(bool) {}
    void OpenOwnSubsLater(wxString *subs, bool) { delete subs; }
    bool IsShown() const { return true; }
    bool IsFullScreen() const { return false; }
    void OpenSubs(int) {}
};

class TabPanel {
public:
    VideoBox *video = nullptr;
    SubsGrid *grid = nullptr;
    EditBox *edit = nullptr;
};

class DrawingAndClip;
class DrawingAndClips;

#include "tagfind_class.inc"
// TagFindReplace.h:111 (copied from TagFindReplace.cpp).
wxPoint FindBrackets(const wxString &text, long from);
