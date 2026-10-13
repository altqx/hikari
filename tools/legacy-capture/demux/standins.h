// Stand-ins for what legacy Demux.cpp (copied unchanged) reaches outside
// itself and FFMS2: the grid and its file, the track chooser, the progress
// dialog and the message box. Each records what it is given; the probe
// (demux_capture.cpp) prints the record. The pieces that decide the result
// are legacy's own: SubsTime is the legacy SubsTime.cpp, HikariLog is
// LogHandler.h's, SubsFile::AddSInfo, GetSInfo and GetSInfoP are copied
// unchanged out of SubsFile.cpp, HikariPathName (and the HikariNormalizePath
// it calls) out of config.h and ZEROIT
// out of SubsDialogue.h. The Style and Dialogue stand-ins also read their
// line with legacy's own Styles::parseStyle and Dialogue::SetRaw
// (legacy_parse.h) and record what legacy's GetRaw gives back.
#pragma once

#include <wx/arrstr.h>
#include <wx/dynarray.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <wx/thread.h>
#include <wx/tokenzr.h>

#include <functional>
#include <string>
#include <vector>

#include "LogHandler.h"
#include "SubsTime.h"

// config.h:36
const wxString emptyString;

// HikariNormalizePath and HikariPathName (config.h), unchanged.
#include "path_name.inc"

// ZEROIT (SubsDialogue.h), unchanged.
#include "zeroit.inc"

// styles.h:125-132: the subtitle formats SubsTime::raw takes.
enum { ASS = 1, SRT, TMP, MDVD, MPL2, FRAME = 10 };

// SubsFile.h:28: the first edition type.
enum { OPEN_SUBTITLES = 1 };

#include "legacy_parse.h"

// What the probe records, in the order Demux did it.
namespace probe {
struct Op {
    std::string kind; // "sinfo", "style", "line", "endload", "clearing", "message", "log", ...
    std::string text;
    int number = 0;
    std::string parsed; // GetRaw of what legacy read from text
};
extern std::vector<Op> ops;
void record(const char *kind, const wxString &text, int number = 0, const wxString &parsed = emptyString);
// The case's answers: the chooser's choice (-1 Cancel) and the packet
// (counted from 1) during which the progress dialog's Cancel is pressed
// (0 never).
extern int choice;
extern int cancelAfter;
extern int progressCalls;
} // namespace probe

// SInfo (styles.h:88-97): a Script Info key and value.
class SInfo {
public:
    wxString Name;
    wxString Val;
    SInfo(wxString _Name, wxString _Val) : Name(_Name), Val(_Val) {}
};

// Styles(raw, format) (styles.h): the raw line, the layout Demux reads it
// with (1 ASS, 2 SSA) and the Style legacy read (Styles::GetRaw).
class Styles {
public:
    Styles(const wxString &raw, char format)
    {
        legacy_parse::Styles parsed(raw, format);
        probe::record("style", raw, format, parsed.GetRaw());
    }
};

// Dialogue(raw) (SubsDialogue.h): the raw line Demux builds and the Line
// legacy read from it (Dialogue::GetRaw).
class Dialogue {
public:
    explicit Dialogue(const wxString &raw)
    {
        legacy_parse::Dialogue parsed(raw);
        wxString back;
        parsed.GetRaw(&back);
        probe::record("line", raw, 0, back);
    }
};

// The SubsFile members Demux and the copied SubsFile functions use.
class SubsFile {
public:
    struct Subs {
        std::vector<SInfo *> sinfo;
        std::vector<SInfo *> deleteSinfo;
    };
    Subs storage;
    Subs *subs = &storage;
    void MarkEdited() {}
    void AddStyle(Styles *) {}
    void AddLine(Dialogue *) {}
    void EndLoad(unsigned char editionType, int activeLine, bool initialSave = false)
    {
        probe::record("endload", wxString::Format(L"%i,%i", int(editionType), activeLine), initialSave);
    }
    const wxString &GetSInfo(const wxString &key, int *ii = 0);
    SInfo *GetSInfoP(const wxString &key, int *ii);
    void AddSInfo(const wxString &SI, wxString val = emptyString, bool save = true);
};

// The SubsGrid members Demux uses (SubsGrid.h).
class SubsGrid {
public:
    SubsFile *file = nullptr;
    int originalFormat = 1;
    wxWindow *GetParent() { return nullptr; }
    void Clearing() { probe::record("clearing", emptyString); }
};

// HikariMessageBox (HikariMessageBox.h:41): the text and caption, answered OK.
inline int HikariMessageBox(const wxString &msg, const wxString &caption = emptyString, long elems = wxOK,
                            wxWindow *parent = 0, const wxPoint &pos = wxDefaultPosition, long buttonWithFocus = -1)
{
    probe::record("message", msg + L"\x1f" + caption, int(elems));
    return wxOK;
}

// HikariListBox (Stylelistbox.h:58-71): its title and items, answered with
// the case's choice or Cancel.
class HikariListBox {
public:
    HikariListBox(wxWindow *parent, const wxArrayString &list, const wxString &title, bool centerOnParent = false)
    {
        probe::record("chooser", title, centerOnParent);
        for (const auto &item : list)
            probe::record("chooser-item", item);
    }
    int ShowModal() { return probe::choice < 0 ? wxID_CANCEL : wxID_OK; }
    int GetIntSelection() { return probe::choice; }
};

// ProgressSink (ProgressDialog.h:70-98). Legacy runs the task on its own
// thread while the dialog is modal; here ShowDialog runs it on the caller's,
// which keeps the order of every call the task makes. Cancel is pressed
// during the case's packet: WasCancelled is true from its Progress on.
class ProgressSink {
public:
    ProgressSink(wxWindow *parent, const wxString &title, const wxPoint & = wxDefaultPosition,
                 const wxSize & = wxDefaultSize, int = 0)
    {
        probe::record("progress-title", title);
    }
    void SetAndRunTask(std::function<int()> task) { m_task = std::move(task); }
    void ShowDialog() { m_result = m_task ? m_task() : 0; }
    wxThread::ExitCode Wait() { return (wxThread::ExitCode)(long long)m_result; }
    bool WasCancelled() { return probe::cancelAfter > 0 && probe::progressCalls >= probe::cancelAfter; }
    void Progress(int num)
    {
        ++probe::progressCalls;
        probe::record("progress", emptyString, num);
    }

private:
    std::function<int()> m_task;
    int m_result = 0;
};
