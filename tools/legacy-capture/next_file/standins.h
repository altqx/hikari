// Stand-ins for the legacy GUI classes VideoBox::NextFile touches
// (HikariSub/VideoBox.cpp:682-739 at 20d647c4, copied unchanged by
// extract_functions.py after this header). Each keeps the legacy member
// names and types NextFile reads; what the GUI would do (open a file, seek,
// pause, take the focus) is recorded in g_calls instead.
#pragma once

#include <wx/arrstr.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <wx/thread.h>

#include <string>
#include <vector>

// One call NextFile made on the GUI, in order: "open <path>", "seek <ms>",
// "pause <burstbl>", "focus".
extern std::vector<std::string> g_calls;

const wxString emptyString;

// TabPanel (TabPanel.h:63): the video the tab shows.
struct TabPanel {
    wxString VideoPath;
};

// HikariSubFrame (HikariSubFrame.h:60, 77): the recent videos and OpenFile.
// Every file opens (FFMS2 accepting it), so the walk is the listing's alone;
// an open sets the tab's VideoPath as LoadVideo does.
class HikariSubFrame {
public:
    wxArrayString videorec;
    TabPanel *tab = nullptr;
    bool OpenFile(const wxString &filename, bool fulls = false, bool freeze = true)
    {
        (void)fulls;
        (void)freeze;
        g_calls.push_back("open " + std::string(filename.ToUTF8().data()));
        tab->VideoPath = filename;
        return true;
    }
};

// Notebook (Notebook.h:64): GetTabs()->GetParent() is the main frame.
class Notebook {
public:
    static Notebook *GetTabs();
    HikariSubFrame *GetParent() { return frame; }
    HikariSubFrame *frame = nullptr;
};

// VideoBox (VideoBox.h:57-69, 204-215): the members NextFile reads.
class VideoBox {
public:
    void NextFile(bool next = true);
    bool Seek(int time, bool startTime = true, int flags = 0)
    {
        (void)startTime;
        (void)flags;
        g_calls.push_back("seek " + std::to_string(time));
        return true;
    }
    bool Pause(bool burstbl = true)
    {
        g_calls.push_back(std::string("pause ") + (burstbl ? "true" : "false"));
        return true;
    }
    void SetFocus() { g_calls.push_back("focus"); }

    TabPanel *tab = nullptr;
    wxMutex nextmutex;
    int actualFile = 0;
    wxArrayString files;
    bool m_IsFullscreen = false;
    bool m_IsDirectShow = false;
};
