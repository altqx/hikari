// Runs the legacy "Moving tags" split (EditBox::SetTextWithTags,
// EditBox.cpp:1809-1874, copied unchanged by extract_functions.py; the
// editor's windows are stood in for by tag_move/standins.h) on the cases in
// inputs/tag-move-cases.txt and prints one JSON object per case: for each
// call, whether the text was split, both fields' text, the Translated field's
// selection (Selend.x, Cursor.x, in the build's wchar_t units), whether it
// took the focus, and the video calls.
//
// Every case is run twice, once per wxString width:
//   "linux":   each wchar_t a code point, as wxGTK's wxString stores text
//              (the legacy Linux build);
//   "windows": each wchar_t a UTF-16 code unit, as wxMSW's wxString stores it
//              (wchar_t is 16 bits there): a character outside the BMP is two
//              wchar_t, its surrogates. The same compiled legacy code runs on
//              both; only the text it is given differs. "same" when the
//              calls are those of "linux".
//
// Case file lines (UTF-8, fields separated by a tab):
//   case <name>
//   line <text> [<translation>]          (the file's Line, and the editor's copy)
//   state <hasTLMode> <Moving tags> <Visual> <Original field shown> <video PlaybackState>
//   call <RefreshVideo 0|1>               (SetTextWithTags, then a record)
//   end
// A state line applies to the calls after it.
#include "standins.h"

#include <cstdio>
#include <iostream>
#include <string>

// The legacy function, unchanged.
#include "tag_move.inc"

namespace {

std::vector<std::string> split(const std::string &line, char separator)
{
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto at = line.find(separator, from);
        out.push_back(line.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos)
            return out;
        from = at + 1;
    }
}

void appendUtf8(std::string &out, unsigned long cp)
{
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// A JSON string of the text as the build holds it: in "windows" form a
// surrogate pair is joined back into its character, and a lone surrogate (a
// split that fell inside a pair) is written as its \u escape.
std::string json(const wxString &s, bool utf16Units)
{
    const std::wstring units = s.ToStdWstring();
    std::string out = "\"";
    for (std::size_t i = 0; i < units.size(); i++) {
        unsigned long c = static_cast<unsigned long>(units[i]);
        if (utf16Units && c >= 0xD800 && c <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 &&
            units[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<unsigned long>(units[i + 1]) - 0xDC00);
            ++i;
        }
        char buffer[8];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || (c >= 0xD800 && c <= 0xDFFF)) {
                std::snprintf(buffer, sizeof buffer, "\\u%04lx", c);
                out += buffer;
            } else {
                appendUtf8(out, c);
            }
        }
    }
    return out + "\"";
}

// The text as the build stores it: code points (wxGTK) or UTF-16 code units
// widened one per wchar_t (wxMSW).
wxString text(const std::string &utf8, bool utf16Units)
{
    const wxString points = wxString::FromUTF8(utf8.data(), utf8.size());
    if (!utf16Units)
        return points;
    std::wstring units;
    for (const wxUniChar ch : points) {
        const unsigned long cp = ch.GetValue();
        if (cp >= 0x10000) {
            units += wchar_t(0xD800 + ((cp - 0x10000) >> 10));
            units += wchar_t(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            units += wchar_t(cp);
        }
    }
    return wxString(units);
}

struct State {
    bool tl = true, move = true, origShown = true;
    int visual = CROSS;
    int video = None;
};

struct Call {
    State state;
    bool refresh = false;
};

// One width: the file's Line, the editor showing a copy of it (EditBox::SetLine
// leaves `line` a Copy and both fields not modified), then the calls in order.
std::string run(const std::string &lineText, const std::string &lineTl, const std::vector<Call> &calls,
                bool utf16Units)
{
    Dialogue fileLine;
    fileLine.Text = text(lineText, utf16Units);
    fileLine.TextTl = text(lineTl, utf16Units);
    SubsFile file;
    file.dialogues.push_back(&fileLine);
    SubsGrid grid;
    grid.file = &file;
    ToggleButton moveTags;
    TextEditor edit, orig;
    VideoBox video;
    TabPanel tab;
    tab.video = &video;
    EditBox box;
    box.grid = &grid;
    box.tab = &tab;
    box.AutoMoveTags = &moveTags;
    box.TextEdit = &edit;
    box.TextEditOrig = &orig;
    box.line = fileLine.Copy();

    std::string out = "[";
    for (std::size_t n = 0; n < calls.size(); n++) {
        const Call &c = calls[n];
        grid.hasTLMode = c.state.tl;
        moveTags.value = c.state.move;
        box.Visual = c.state.visual;
        orig.shown = c.state.origShown;
        video.state = PlaybackState(c.state.video);
        edit.focused = false;
        edit.sets = orig.sets = 0;
        video.calls.clear();
        box.SetTextWithTags(c.refresh);
        std::string videoCalls = "[";
        for (std::size_t i = 0; i < video.calls.size(); i++)
            videoCalls += (i ? ",\"" : "\"") + video.calls[i] + "\"";
        videoCalls += "]";
        out += std::string(n ? "," : "") + "{\"split\":" + (box.splittedTags ? "true" : "false") +
               ",\"edit\":" + json(edit.MText, utf16Units) + ",\"editSet\":" + std::to_string(edit.sets) +
               ",\"editModified\":" + (edit.modified ? "true" : "false") + ",\"orig\":" +
               json(orig.MText, utf16Units) + ",\"origSet\":" + std::to_string(orig.sets) + ",\"selection\":[" +
               std::to_string(edit.selend) + "," + std::to_string(edit.cursor) + "],\"focus\":" +
               (edit.focused ? "true" : "false") + ",\"video\":" + videoCalls + "}";
    }
    delete box.line;
    return out + "]";
}

} // namespace

int main()
{
    std::string line, name, lineText, lineTl;
    State state;
    std::vector<Call> calls;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        auto fields = split(line, '\t');
        if (fields.size() == 1 && line.find(' ') != std::string::npos) // "case <name>", "end"
            fields = {line.substr(0, line.find(' ')), line.substr(line.find(' ') + 1)};
        const std::string op = fields[0];
        if (op == "case") {
            name = fields.at(1);
            lineText.clear();
            lineTl.clear();
            state = State{};
            calls.clear();
        } else if (op == "line") {
            lineText = fields.at(1);
            lineTl = fields.size() > 2 ? fields[2] : std::string();
        } else if (op == "state") {
            state.tl = fields.at(1) == "1";
            state.move = fields.at(2) == "1";
            state.visual = std::stoi(fields.at(3));
            state.origShown = fields.at(4) == "1";
            state.video = std::stoi(fields.at(5));
        } else if (op == "call") {
            calls.push_back(Call{state, fields.at(1) == "1"});
        } else if (op == "end") {
            const std::string onLinux = run(lineText, lineTl, calls, false);
            const std::string onWindows = run(lineText, lineTl, calls, true);
            std::printf("{\"case\":%s,\"text\":%s,\"translation\":%s,\"linux\":%s,\"windows\":%s}\n",
                        json(wxString::FromUTF8(name), false).c_str(), json(text(lineText, false), false).c_str(),
                        json(text(lineTl, false), false).c_str(), onLinux.c_str(),
                        onWindows == onLinux ? "\"same\"" : onWindows.c_str());
            std::fflush(stdout);
        } else {
            std::fprintf(stderr, "unknown op %s\n", op.c_str());
            return 2;
        }
    }
    return 0;
}
