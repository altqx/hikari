// V6: runs the legacy video timing and selection code on the cases in
// inputs/video-timing-cases.txt and prints one JSON line per case.
//
// The definitions under test are copied unchanged out of the legacy sources
// (extract_functions.py, see CMakeLists.txt): VideoBox::GetFrameTime and
// GetVideoListsOptions, SubsGrid::SetStartTime, SetEndTime, SelectVisible,
// ShowEditOnVideo, GetKeyFromPosition (SubsGridBase.cpp), SelVideoLine and
// SetVideoLineTime (SubsGridWindow.cpp), EditBox::SetLine and
// HikariSubFrame::OnAudioSnap; Timebase.cpp and SubsTime.cpp are linked as
// they are. The GUI they reach is stood in for by video_timing/standins.h.
//
// Two call sites are transcribed (each a single expression inside a function
// too large to extract), and cited where they run below:
// - GLOBAL_SET_START_TIME / _END_TIME, HikariSubFrame.cpp:750-760;
// - the Grid's press that seeks, SubsGridWindow.cpp:1647-1648.
//
// Case file (one directive per line):
//   case <name>
//   timebase <fps> <frame start ms>...   Timebase::FromTimecodes (FFMS2's truncated ms)
//   keyframes <ms>...                    Timebase::SetKeyframes
//   line <start> <end> <flags>           flags: '-' or any of H (hidden), C (comment),
//                                        N (not dialogue), S (selected), E (no text),
//                                        T (translation only)
//   active <row>                         the edit box's Line (currentLine)
//   editline <start> <end>               the edit box's Line's times (edit->line)
//   video <none|playing|paused|stopped> <tell ms> <duration ms>
//   format <ass|srt|tmp>
//   inactive <mode>                      AUDIO_INACTIVE_LINES_DISPLAY_MODE
//   <op>...                              each op runs on a fresh copy of the case;
//   at <tell> <op>...                    the op with the video's time at tell
//   end
// Ops:
//   setstart <offset> / setend <offset>                   GLOBAL_SET_START_TIME / _END_TIME
//   frametime                                             GetFrameTime(true), GetFrameTime(false)
//   selvideo                                              GLOBAL_SELECT_FROM_VIDEO
//   selvisible <ignoreFiltered>                           GRID_SELECT_VISIBLE_LINES
//   snap <start|end> <abox>                               GLOBAL_SNAP_WITH_START / _END
//   setline <row> <seekAfter> <playAfter> <nochangeline> <autoPlay> <abox>
//                                                         EditBox::SetLine(row, true, true, ...)
//   press <seekAfter> <playAfter> <dclick> <ctrl> <endColumn> <rowChanged> <abox>
//                                                         the Grid's press (SetVideoLineTime)
//   edit <seekAfter> <visual>                             ShowEditOnVideo(false)
#include "standins.h"

#include <iostream>
#include <map>
#include <sstream>

std::vector<std::string> g_calls;
ProbeOptions Options;

// The copied legacy definitions.
#include "videobox.inc"
#include "grid_base.inc"
#include "grid_window.inc"
#include "frame.inc"
#define wxWindow ProbeWindow
#include "editbox.inc"
#undef wxWindow

namespace {

struct LineSpec {
    int start = 0, end = 0;
    std::string flags;
};

struct Case {
    std::string name;
    float fps = 0.f;
    std::vector<int> timecodes, keyframes;
    std::vector<LineSpec> lines;
    int active = 0;
    bool hasEditLine = false;
    int editStart = 0, editEnd = 0;
    PlaybackState state = None;
    int tell = 0, duration = 0;
    char format = ASS;
    int inactive = 1;
    std::vector<std::string> ops;
};

// One fresh legacy tab for an op.
struct Tab {
    SubsGrid grid;
    EditBox edit;
    VideoBox video;
    TabPanel panel;
    AudioBox audio;
    HikariSubFrame frame;

    explicit Tab(const Case &c)
    {
        panel.grid = &grid;
        panel.edit = &edit;
        panel.video = &video;
        grid.tab = &panel;
        grid.edit = &edit;
        edit.grid = &grid;
        edit.tab = &panel;
        frame.tab = &panel;
        for (int &w : grid.GridWidth)
            w = 50;
        grid.subsFormat = c.format;
        for (std::size_t i = 0; i < c.lines.size(); i++) {
            auto *d = new Dialogue;
            d->Start = SubsTime(c.lines[i].start);
            d->End = SubsTime(c.lines[i].end);
            const std::string &f = c.lines[i].flags;
            auto has = [&](char ch) { return f.find(ch) != std::string::npos; };
            d->Text = has('E') || has('T') ? wxString() : wxString::Format("line %d", (int)i);
            d->TextTl = has('T') ? wxString::Format("tl %d", (int)i) : wxString();
            d->IsComment = has('C');
            d->NonDialogue = has('N');
            d->isVisible = has('H') ? 0 : 1;
            grid.file->dialogues.push_back(d);
            if (has('S'))
                grid.file->InsertSelection(i);
        }
        grid.currentLine = grid.markedLine = edit.currentLine = c.active;
        edit.line = grid.file->GetDialogue(c.active)->Copy();
        if (c.hasEditLine) {
            edit.line->Start = SubsTime(c.editStart);
            edit.line->End = SubsTime(c.editEnd);
        }
        edit.StartEdit->SetTime(edit.line->Start);
        edit.EndEdit->SetTime(edit.line->End);
        video.state = c.state;
        video.time = c.tell;
        video.duration = c.duration;
        video.timebase = Timebase::FromTimecodes(c.timecodes, c.fps);
        video.timebase.SetKeyframes(c.keyframes);
        Options.inactiveLines = c.inactive;
    }
    ~Tab()
    {
        for (auto *d : grid.file->dialogues)
            delete d;
        delete edit.line;
    }
};

std::string quoted(const std::string &s)
{
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"' || ch == '\\')
            out += '\\';
        out += ch;
    }
    return out + "\"";
}

std::string state(Tab &t)
{
    std::ostringstream out;
    out << "\"lines\":[";
    for (std::size_t i = 0; i < t.grid.file->GetCount(); i++) {
        auto *d = t.grid.file->GetDialogue(i);
        out << (i ? "," : "") << "[" << d->Start.mstime << "," << d->End.mstime << "]";
    }
    out << "],\"selected\":[";
    bool first = true;
    for (int s : t.grid.file->selections) {
        out << (first ? "" : ",") << s;
        first = false;
    }
    out << "],\"current\":" << t.edit.currentLine << ",\"edit\":[" << t.edit.StartEdit->GetTime().mstime << ","
        << t.edit.EndEdit->GetTime().mstime << "],\"calls\":[";
    for (std::size_t i = 0; i < g_calls.size(); i++)
        out << (i ? "," : "") << quoted(g_calls[i]);
    out << "]";
    return out.str();
}

std::string run(const Case &c, const std::string &opLine)
{
    g_calls.clear();
    Tab t(c);
    std::istringstream in(opLine);
    std::string op;
    in >> op;
    if (op == "at") { // at <tell> <op>...: the op with the video at another time
        in >> t.video.time;
        in >> op;
    }
    std::ostringstream extra;
    if (op == "setstart" || op == "setend") {
        int offset = 0;
        in >> offset;
        // HikariSubFrame.cpp:750-760 (transcribed; GRID_INSERT_START_OFFSET / _END_OFFSET = offset)
        if (t.video.GetState() != None) {
            if (op == "setstart") {
                int time = t.video.GetFrameTime() + offset;
                t.grid.SetStartTime(ZEROIT(time));
            } else {
                int time = t.video.GetFrameTime(false) + offset;
                t.grid.SetEndTime(ZEROIT(time));
            }
        }
    } else if (op == "frametime") {
        extra << ",\"start\":" << t.video.GetFrameTime() << ",\"end\":" << t.video.GetFrameTime(false);
    } else if (op == "selvideo") {
        t.grid.SelVideoLine();
    } else if (op == "selvisible") {
        int ignore = 0;
        in >> ignore;
        t.grid.ignoreFiltered = ignore != 0;
        t.grid.SelectVisible();
    } else if (op == "snap") {
        std::string which;
        int abox = 0;
        in >> which >> abox;
        if (abox)
            t.edit.ABox = &t.audio;
        wxCommandEvent event(wxEVT_MENU, which == "start" ? GLOBAL_SNAP_WITH_START : GLOBAL_SNAP_WITH_END);
        t.frame.OnAudioSnap(event);
    } else if (op == "setline") {
        int row = 0, seekAfter = 0, playAfter = 0, nochangeline = 0, autoPlay = 0, abox = 0;
        in >> row >> seekAfter >> playAfter >> nochangeline >> autoPlay >> abox;
        t.video.toolbar.seek.selection = seekAfter;
        t.video.toolbar.play.selection = playAfter;
        if (abox)
            t.edit.ABox = &t.audio;
        t.edit.SetLine(row, true, true, nochangeline != 0, autoPlay != 0);
    } else if (op == "press") {
        int seekAfter = 0, playAfter = 0, dclick = 0, ctrl = 0, endColumn = 0, rowChanged = 0, abox = 0;
        in >> seekAfter >> playAfter >> dclick >> ctrl >> endColumn >> rowChanged >> abox;
        if (abox)
            t.edit.ABox = &t.audio;
        t.video.toolbar.seek.selection = seekAfter;
        t.video.toolbar.play.selection = playAfter;
        bool changeActive = true; // GRID_CHANGE_ACTIVE_ON_SELECTION (config.cpp:388)
        bool click = !dclick;
        int lastActiveLine = rowChanged ? -1 : t.grid.currentLine;
        int row = t.grid.currentLine;
        // the End column: legacy whh (SubsGridWindow.cpp:1382-1389) with 50-pixel columns
        short wh = (t.grid.subsFormat < SRT) ? 2 : 1;
        int x = 2 + 50 * (wh + 1) + (endColumn ? 10 : -10);
        wxMouseEvent event(dclick ? wxEVT_LEFT_DCLICK : wxEVT_LEFT_DOWN);
        event.SetPosition(wxPoint(x, 30));
        event.SetControlDown(ctrl != 0);
        // SubsGridWindow.cpp:1647-1648 (transcribed)
        if (dclick || (click && lastActiveLine != row && (changeActive || !ctrl) && seekAfter < 4 && seekAfter > 0) && playAfter < 2){
            t.grid.SetVideoLineTime(event, seekAfter);
        }
    } else if (op == "edit") {
        int seekAfter = 0, visual = 0;
        in >> seekAfter >> visual;
        t.video.toolbar.seek.selection = seekAfter;
        t.edit.Visual = visual;
        t.grid.ShowEditOnVideo(false);
    } else {
        return "";
    }
    return "{\"op\":" + quoted(opLine) + extra.str() + "," + state(t) + "}";
}

std::vector<Case> readCases(std::istream &in)
{
    std::vector<Case> cases;
    std::string text;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r')
            text.pop_back();
        std::istringstream line(text);
        std::string op;
        line >> op;
        if (op.empty() || op[0] == '#')
            continue;
        if (op == "case") {
            cases.emplace_back();
            line >> cases.back().name;
            continue;
        }
        Case &c = cases.back();
        if (op == "timebase") {
            line >> c.fps;
            int ms;
            while (line >> ms)
                c.timecodes.push_back(ms);
        } else if (op == "keyframes") {
            int ms;
            while (line >> ms)
                c.keyframes.push_back(ms);
        } else if (op == "line") {
            LineSpec l;
            line >> l.start >> l.end >> l.flags;
            c.lines.push_back(l);
        } else if (op == "active") {
            line >> c.active;
        } else if (op == "editline") {
            c.hasEditLine = true;
            line >> c.editStart >> c.editEnd;
        } else if (op == "video") {
            std::string s;
            line >> s >> c.tell >> c.duration;
            c.state = s == "playing" ? Playing : s == "paused" ? Paused : s == "stopped" ? Stopped : None;
        } else if (op == "format") {
            std::string f;
            line >> f;
            c.format = f == "srt" ? SRT : f == "tmp" ? TMP : ASS;
        } else if (op == "inactive") {
            line >> c.inactive;
        } else if (op != "end") {
            c.ops.push_back(text);
        }
    }
    return cases;
}

} // namespace

int main()
{
    for (const Case &c : readCases(std::cin)) {
        std::cout << "{\"case\":" << quoted(c.name) << ",\"ops\":[";
        bool first = true;
        for (const auto &op : c.ops) {
            const std::string result = run(c, op);
            if (result.empty()) {
                std::cerr << "unknown op: " << op << "\n";
                return 1;
            }
            std::cout << (first ? "" : ",") << result;
            first = false;
        }
        std::cout << "]}\n";
    }
    return 0;
}
