// Stand-ins for the legacy GUI classes the clip tools touch (T4). The probe
// compiles copies of the legacy VisualClipRect.cpp and VisualClips.cpp next to
// headers that all include this one, with the real VisualClipPoint.h, the
// classes Visuals, ClipRect, DrawingAndClip, FindData and TagFindReplace
// copied out of their headers, and the definitions copied
// unchanged out of Visuals.cpp, TagFindReplace.cpp, EditBox.cpp and
// config.cpp (extract_functions.py). Each stand-in keeps the legacy member
// names and types those definitions read; what the GUI would do (the
// editor's text, the Grid's edits, the history, the mouse capture, the bell,
// the message box) is recorded instead.
#pragma once

#include <wx/arrstr.h>
#include <wx/dc.h>
#include <wx/dynarray.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/regex.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <wx/window.h>

#include "d3dx9.h" // the legacy Linux D3DX stand-ins
#include "UndoD3DXMacros.h"

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// config.h:552-560, unchanged.
#define MIN(a,b) ((a)<(b))?(a):(b)
#define MAX(a,b) ((a)>(b))?(a):(b)
#define MID(a,b,c) MAX((a),MIN((b),(c)))
// config.h:526-548, unchanged.
#define SAFE_DELETE(x) if (x !=nullptr) { delete x; x = nullptr; }
#define SAFE_RELEASE(x) if (x != nullptr) { x->Release(); x = nullptr; }
#define HR(what,err) if(FAILED(what)) {HikariLogSilent(err); return false;}
#define HRN(what,err) if(FAILED(what)) {HikariLogSilent(err); return;}
// SubsDialogue.h:20, unchanged.
#define ZEROIT(a) ((a/10)*10)

inline void HikariLogSilent(const wxString &) {}
// What the tools logged, rang and asked (the probe prints them).
struct ProbeEvents {
    std::vector<std::string> log;
    int bells = 0;
    std::vector<std::string> messages;
};
extern ProbeEvents probeEvents;
inline void HikariLog(const wxString &text) { probeEvents.log.push_back(std::string(text.utf8_str())); }
inline void probeBell() { ++probeEvents.bells; }
#define wxBell probeBell
inline int HikariMessageBox(const wxString &message, const wxString &caption = wxString())
{
    probeEvents.messages.push_back(std::string((caption + L": " + message).utf8_str()));
    return 0;
}

const wxString emptyString;

// config.h:601, 612 (definitions copied from config.cpp).
wxString getfloat(float num, const wxString& format = L"5.3f", bool Truncate = true);
size_t FindFromEnd(const wxString& text, const wxString& whatToFind, bool ignoreCase = false);

enum { TL_MODE_HIDE_ORIGINAL_ON_VIDEO };
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

// SubsFile.h's history entries the clips record (named in the output).
enum {
    VISUAL_MOVE = 101, VISUAL_SCALE, VISUAL_ROTATION_Z, VISUAL_ROTATION_X_Y, VISUAL_RECT_CLIP,
    VISUAL_VECTOR_CLIP, VISUAL_DRAWING, VISUAL_ALL_TAGS
};
enum { TXT = 512, TXTTL = 1024 };
enum PlaybackState { Playing, Paused, Stopped, None };
enum { OPEN_DUMMY = 2 };

// RendererVideo.h:58-79.
struct VERTEX
{
	float fX;
	float fY;
	float fZ;
	D3DCOLOR Color;
};
inline void CreateVERTEX(VERTEX *v, float X, float Y, D3DCOLOR Color, float Z = 0.0f)
{
    v->fX = X;
    v->fY = Y;
    v->fZ = Z;
    v->Color = Color;
}

class Styles {
public:
    wxString Name = L"Default";
    wxString Alignment = L"2", MarginL = L"0", MarginV = L"0", Angle = L"0";
    double GetScaleXDouble() { return 100.; }
    double GetScaleYDouble() { return 100.; }
};

class TagData {
public:
    wxString tagName;
    wxString value;
};
class ParseData {
public:
    std::vector<TagData *> tags;
};

struct ProbeTime {
    int mstime = 0;
};

class Dialogue {
public:
    wxString Text, TextTl, Style = L"Default";
    ProbeTime Start, End;
    bool IsComment = false;
    bool NonDialogue = false;
    int Layer = 0;
    int MarginL = 0, MarginR = 0, MarginV = 0;
    ParseData parse;
    const wxString &GetTextNoCopy() { return (TextTl != emptyString) ? TextTl : Text; } // SubsDialogue.cpp:216-219
    wxString &GetText() { return (TextTl != emptyString) ? TextTl : Text; }             // CheckTlRef
    void SetText(const wxString &text)                                               // SubsDialogue.cpp:225-231
    {
        if (TextTl != emptyString)
            TextTl = text;
        else
            Text = text;
    }
    Dialogue *Copy() { return new Dialogue(*this); }
    // The raw line, reduced to what the probe compares: the layer and text.
    void GetRaw(wxString *raw, bool tl = false, const wxString & = wxString())
    {
        *raw << Layer << L"|" << (tl ? TextTl : Text);
    }
    void SetTextElement(int element, const wxString &text, bool = false)
    {
        if (element == TXT)
            Text = text;
    }
    ParseData *ParseTags(wxString *, size_t, bool = false) { return &parse; }
    void ClearParse() {}
    void GetDefaultPosition(Styles *, int, wxSize, float *x, float *y) { *x = 0; *y = 0; }
};

class TextEditor {
public:
    wxString value;
    long from = 0, to = 0;
    bool modified = false;
    wxString GetValue(bool = true) const { return value; }
    void GetSelection(long *f, long *t) const { *f = from; *t = to; }
    void SetSelection(long f, long t, bool = false) { from = f; to = t; }
    // DialogueTextEditor.cpp:135-155: the caret goes to 0 with resetsel,
    // else stays within the text.
    void SetTextS(const wxString &text, bool modif = false, bool resetsel = true, bool = false, bool = true)
    {
        modified = modif;
        value = text;
        if (resetsel) {
            from = to = 0;
        } else {
            if ((size_t)from > value.length())
                from = value.length();
            if ((size_t)to > value.length())
                to = value.length();
        }
    }
    void SetModified(bool m = true) { modified = m; }
    bool IsModified() const { return modified; }
    void SetFocus() {}
    bool IsShown() const { return shown; }
    bool shown = true;
};

class SubsFile {
public:
    std::vector<Dialogue *> dialogues;
    wxArrayInt selections;
    Styles style;
    void GetSelections(wxArrayInt &sels) { sels = selections; }
    size_t SelectionsSize() { return selections.size(); }
    Dialogue *GetDialogue(int i) { return dialogues[i]; }
    const wxString &GetSInfo(const wxString &) { return emptyString; }
    Styles *GetStyle(int, const wxString &) { return &style; }
};

// SubsGrid::SetModified's consequences, in the probe (clip_capture.cpp).
void probeModified(int action, bool redit, bool dummy);

struct ProbeHistory {
    std::string action;
    std::vector<std::string> texts; // every Line's text|translation after it
};

class SubsGrid {
public:
    SubsFile *file = nullptr;
    bool hasTLMode = false;
    int currentLine = 0;
    int subsWidth = 0, subsHeight = 0;
    std::vector<ProbeHistory> history;
    void GetASSRes(int *x, int *y) { *x = subsWidth; *y = subsHeight; }
    Dialogue *CopyDialogue(int i) { return file->dialogues[i]; }
    void SetModified(int action, bool redit = true, bool dummy = false, int = -1, bool = true)
    {
        probeModified(action, redit, dummy);
    }
    void Refresh(bool = true) {}
    void record(int action);
    // The visible subtitles' text (the probe's renderer reads nothing):
    // the active editor's text at the line's place, or the selected Lines'
    // places.
    wxString *GetVisible(bool *visible = 0, wxPoint *point = nullptr, wxArrayInt *selected = nullptr, bool = false);
};

class VideoToolbar {
public:
    // VectorItem::SetItemToggled (VideoToolbar.h:117-123) with VectorItem's
    // seven buttons.
    int toggled = 1;
    int numIcons = 7;
    void SetItemToggled(int *item)
    {
        toggled = *item;
        if (toggled < 0)
            toggled = (*item) = numIcons - 1;
        else if (toggled >= numIcons)
            toggled = (*item) = 0;
    }
    int GetItemToggled() { return toggled; }
};

class VideoBox {
public:
    int time = 0;
    bool captured = false;
    int lastCursor = -1;
    VideoToolbar toolbar;
    PlaybackState GetState() { return Paused; }
    int Tell() { return time; }
    void SetVisualEdition(bool) {}
    void Render(bool = true) {}
    void OpenSubs(int) {}
    void OpenOwnSubsLater(wxString *subs, bool) { delete subs; }
    bool HasVideo() { return true; }
    bool IsShown() { return true; }
    bool IsFullScreen() { return false; }
    bool HasCapture() { return captured; }
    void CaptureMouse() { captured = true; }
    void ReleaseMouse() { captured = false; }
    bool HasArrow() { return true; }
    bool SetCursor(int cursorId) { lastCursor = cursorId; return true; }
    VideoToolbar *GetVideoToolbar() { return &toolbar; }
};

class TabPanel;

class EditBox {
public:
    TextEditor *TextEdit = nullptr;
    TextEditor *TextEditOrig = nullptr;
    Dialogue *line = nullptr;
    SubsGrid *grid = nullptr;
    bool splittedTags = false;
    int Visual = 0;
    // Copied from EditBox.cpp.
    TextEditor *GetEditor(const wxString &text = emptyString);
    bool IsCursorOnStart();
    void UpdateChars() {}
    // EditBox::Send, reduced to its text columns: the editors' modified
    // texts go into the Line (the translation in TLMode) and the Grid records
    // the edit.
    void Send(unsigned char editionType, bool = true, bool = false, bool = false);
};

// The Visuals base, as TagFindReplace.h declares it (copied).
#include "tagfind_class.inc"
wxPoint FindBrackets(const wxString& text, long from);

class TabPanel {
public:
    VideoBox *video = nullptr;
    SubsGrid *grid = nullptr;
    EditBox *edit = nullptr;
};

#include "VisualClipPoint.h"

// Visuals.h's Visuals and ClipRect, copied. The probe reads their state, so
// their members are opened up here.
#define private public
#include "visuals_class.inc"
#include "cliprect_class.inc"
#undef private
