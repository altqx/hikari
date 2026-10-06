// Stand-ins for the legacy GUI classes V6's commands touch. The probe
// compiles definitions copied unchanged out of SubsGridBase.cpp,
// SubsGridWindow.cpp, EditBox.cpp, VideoBox.cpp and HikariSubFrame.cpp
// (extract_functions.py) after this header, and links the real Timebase.cpp
// and SubsTime.cpp. Each stand-in keeps the legacy member names and types
// those definitions read; what the GUI would do (seek, play, pause, redraw,
// history) is recorded in g_calls instead.
#pragma once

#include <wx/dynarray.h>
#include <wx/event.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/thread.h>

#include "SubsTime.h"
#include "Timebase.h"

#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#ifdef None
#undef None // X11's, should a wx header bring it in
#endif

// config.h:552-554, unchanged.
#define MIN(a,b) ((a)<(b))?(a):(b)
#define MAX(a,b) ((a)>(b))?(a):(b)
#define MID(a,b,c) MAX((a),MIN((b),(c)))
// SubsDialogue.h:19-21, unchanged.
#ifndef ZEROIT
#define ZEROIT(a) ((a/10)*10)
#endif

// What the GUI was asked to do, in order.
extern std::vector<std::string> g_calls;
inline void Call(const char *format, ...)
{
    char buffer[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    g_calls.emplace_back(buffer);
}

const wxString emptyString;

// RendererVideo.h:43-49, unchanged.
enum PlaybackState : int
{
	Playing,
	Paused,
	Stopped,
	None
};
// Playback.h:21-29.
enum SeekFlags { SEEK_NO_SNAP = 1, SEEK_WAIT = 2, SEEK_KEEP_AUDIO = 4 };
// SubtitlesProviderManager.h (OPEN_DUMMY), styles.h:124-131.
enum { OPEN_DUMMY = 1 };
enum { ASS = 1, SRT, TMP, MDVD, MPL2, FRAME = 10 };
// Visuals.h:56-68.
enum { CROSS = 0, CHANGEPOS, MOVE, SCALE, ROTATEZ, ROTATEXY, CLIPRECT, VECTORCLIP, VECTORDRAW, MOVEALL, ALL_TAGS };
// SubsFile.h: the history names these definitions record (by name only).
enum : unsigned char { EDITBOX_LINE_EDITION = 3, GRID_SET_START_TIME = 24, GRID_SET_END_TIME = 25,
                       SNAP_TO_KEYFRAME_OR_LINE_TIME = 31 };
inline const char *HistoryName(unsigned char type)
{
    switch (type) {
    case EDITBOX_LINE_EDITION: return "Line editing";
    case GRID_SET_START_TIME: return "Setting start time";
    case GRID_SET_END_TIME: return "Setting end time";
    case SNAP_TO_KEYFRAME_OR_LINE_TIME: return "Snapping to keyframe";
    }
    return "?";
}
// Hotkeys.h ids OnAudioSnap reads.
enum { GLOBAL_SNAP_WITH_START = 5158, GLOBAL_SNAP_WITH_END = 5159 };
// config.h options these definitions read, and the theme colour id.
enum { AUDIO_INACTIVE_LINES_DISPLAY_MODE, GRID_SAVE_AFTER_CHARACTER_COUNT, WINDOW_TEXT };
struct ProbeOptions {
    int inactiveLines = 1; // AUDIO_INACTIVE_LINES_DISPLAY_MODE (config.cpp:821)
    int GetInt(int id) const { return id == AUDIO_INACTIVE_LINES_DISPLAY_MODE ? inactiveLines : 0; }
};
extern ProbeOptions Options;

// The Effect field's StoreTextHelper (operator-> to its wxString).
struct ProbeText {
    wxString value;
    wxString *operator->() { return &value; }
};

// The Dialogue fields these definitions read (SubsDialogue.h).
class Dialogue {
public:
    SubsTime Start, End;
    wxString Text, TextTl, Style, Actor;
    ProbeText Effect;
    int Layer = 0, MarginL = 0, MarginR = 0, MarginV = 0;
    bool IsComment = false, NonDialogue = false;
    int isVisible = 1; // VISIBLE; NOT_VISIBLE = 0
    bool IsDoubtful() { return false; }
    Dialogue *Copy() { return new Dialogue(*this); }
};

// SubsFile: the Lines and the selection (keys).
class SubsFile {
public:
    std::vector<Dialogue *> dialogues;
    std::set<int> selections;
    size_t GetCount() { return dialogues.size(); }
    Dialogue *GetDialogue(size_t i) { return dialogues[i]; }
    void GetSelections(wxArrayInt &sels) { for (int s : selections) sels.Add(s); }
    void ClearSelections() { selections.clear(); }
    void InsertSelection(size_t i) { selections.insert(static_cast<int>(i)); }
    int FirstSelection() { return selections.empty() ? -1 : *selections.begin(); }
    bool IsFiltered() { return false; }
    void EndTypingRun() {}
    // SubsFile::GetElementById: the key of the id-th shown Line.
    size_t GetElementById(size_t id)
    {
        size_t shown = 0;
        for (size_t i = 0; i < dialogues.size(); i++)
            if (dialogues[i]->isVisible && shown++ == id)
                return i;
        return static_cast<size_t>(-1);
    }
};

// The edit box's time controls.
struct ProbeTime {
    const char *name = "";
    SubsTime time;
    bool changedBackGround = false;
    void SetTime(const SubsTime &value, bool = false, int = 0) { time = value; }
    SubsTime GetTime() { return time; }
    void SetModified(bool) {}
    void SetForegroundColour(int) {}
};
// The edit box's other controls: nothing observed.
struct ProbeControl {
    template <class T> void SetValue(const T &) {}
    template <class T> void ChangeValue(const T &) {}
    void SetInt(int) {}
    void SetLabelText(const wxString &) {}
    void SetSelection(int) {}
    int FindString(const wxString &, bool) { return 0; }
    void SetState(int) {}
    bool IsShown() { return false; }
};

struct AudioDisplay {
    void SetDialogue(Dialogue *, int line, bool atEnd = false) { Call("audio SetDialogue %d %d", line, atEnd); }
    void Update(bool atEnd) { Call("audio Update %d", atEnd); }
};
struct AudioBox {
    AudioDisplay display;
    AudioDisplay *audioDisplay = &display;
    bool IsShown() { return true; }
    void OnPlaySelection(wxCommandEvent &) { Call("OnPlaySelection"); }
};

// The focused window AudioBox play restores (EditBox.cpp:467-469); a
// windowless probe has none, so it stands in for wxWindow there.
struct ProbeWindow {
    static ProbeWindow *FindFocus() { static ProbeWindow focused; return &focused; }
    void SetFocus() {}
};

class SubsGrid;
class TabPanel;

class EditBox {
public:
    Dialogue *line = nullptr;
    int currentLine = 0;
    int Visual = CROSS;
    SubsGrid *grid = nullptr;
    TabPanel *tab = nullptr;
    AudioBox *ABox = nullptr;
    ProbeTime start{"start"}, end{"end"}, duration{"duration"};
    ProbeTime *StartEdit = &start, *EndEdit = &end, *DurEdit = &duration;
    ProbeControl controls;
    ProbeControl *LineNumber = &controls, *Comment = &controls, *LayerEdit = &controls, *StyleChoice = &controls,
                 *ActorEdit = &controls, *MarginLEdit = &controls, *MarginREdit = &controls,
                 *MarginVEdit = &controls, *EffectEdit = &controls, *TextEdit = &controls,
                 *DoubtfulTL = &controls, *TextEditOrig = &controls;
    // EditBox.cpp (copied)
    void SetLine(int Row, bool setaudio = true, bool save = true, bool nochangeline = false, bool autoPlay = false);
    void Send(unsigned char editionType, bool gotoNextLine = true, bool dummy = false, bool visualdummy = false)
    {
        Call("Send %s %d", HistoryName(editionType), gotoNextLine);
    }
    void SetTlMode(bool, bool = false) {}
    void SetTextWithTags() {}
    void UpdateChars() {}
    void SetAlignment() {}
};

struct ProbeChoice {
    int selection = 0;
    int GetSelection() { return selection; }
};
struct VideoToolbar {
    ProbeChoice seek, play;
    ProbeChoice *videoSeekAfter = &seek, *videoPlayAfter = &play;
};

class VideoBox {
public:
    PlaybackState state = None;
    int time = 0;     // RendererVideo::m_Time
    int duration = 0; // GetDuration
    Timebase timebase;
    VideoToolbar toolbar;
    VideoToolbar *m_VideoToolbar = &toolbar;
    // VideoBox.cpp (copied)
    int GetFrameTime(bool start = true);
    void GetVideoListsOptions(int *videoPlayAfter, int *videoSeekAfter);

    const Timebase &GetTimebase() { return timebase; }
    int Tell() { return time; }
    int GetDuration() { return duration; }
    PlaybackState GetState() { return state; }
    bool IsShown() { return true; }
    bool IsFullScreen() { return false; }
    // RendererVideo::SeekTarget / SetFFMS2Position move m_Time onto the frame.
    bool Seek(int ms, bool startTime = true, int flags = 0)
    {
        Call("Seek %d %d", ms, startTime);
        return true;
    }
    // VideoBox::Pause toggles (GLOBAL_PLAY_PAUSE uses it either way).
    bool Pause(bool = true)
    {
        Call("Pause");
        state = state == Playing ? Paused : Playing;
        return true;
    }
    bool Play()
    {
        Call("Play");
        state = Playing;
        return true;
    }
    void PlayLine(int start, int end) { Call("PlayLine %d %d", start, end); }
    bool OpenSubs(int, bool = true, bool = false, bool = false) { Call("OpenSubs"); return true; }
    void Render(bool = true) { Call("Render"); }
    void SetVisual(bool settext = false, bool noRefresh = false) { Call("SetVisual %d %d", settext, noRefresh); }
    void RefreshTime() { Call("RefreshTime"); }
};

class SubsGrid {
public:
    SubsFile store;
    SubsFile *file = &store;
    EditBox *edit = nullptr;
    TabPanel *tab = nullptr;
    int currentLine = 0;
    int markedLine = 0;
    bool ignoreFiltered = false;
    char subsFormat = ASS;
    int scHor = 0;
    int GridWidth[14];
    bool hasTLMode = false;
    bool showFrames = false;
    SubsGrid *preview = nullptr;
    wxMutex mutex;
    wxMutex &GetMutex() { return mutex; }

    // SubsGrid::CopyDialogue: the Line to change (legacy copies it into the next history step).
    Dialogue *CopyDialogue(size_t i, bool = true, bool = false)
    {
        return i < file->GetCount() ? file->GetDialogue(i) : nullptr;
    }
    void SetModified(unsigned char editionType, bool = true, bool = false, int = -1, bool = true)
    {
        Call("SetModified %s", HistoryName(editionType));
    }
    // SubsGrid::SelectRow(row): that Line alone.
    void SelectRow(int row, bool = false, bool = true, bool = false)
    {
        file->selections = {row};
    }
    void MakeVisible(int) {}
    void ScrollTo(int, bool = false, int = 0) {}
    void Refresh(bool = true) {}
    size_t GetDialoguePosition(size_t key) { return key; }

    // SubsGridBase.cpp and SubsGridWindow.cpp (copied)
    void SetStartTime(int stime);
    void SetEndTime(int etime);
    void SelectVisible();
    void SelVideoLine(int curtime = -1);
    void SetVideoLineTime(wxMouseEvent &evt, int mvtal);
    void ShowEditOnVideo(bool afterUndo = false);
    size_t GetKeyFromPosition(size_t position, int delta, bool safe = true);
};

class TabPanel {
public:
    SubsGrid *grid = nullptr;
    EditBox *edit = nullptr;
    VideoBox *video = nullptr;
};

class HikariSubFrame {
public:
    TabPanel *tab = nullptr;
    TabPanel *GetTab() { return tab; }
    // HikariSubFrame.cpp (copied)
    void OnAudioSnap(wxCommandEvent &event);
};
