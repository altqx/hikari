#pragma once

// P6: legacy session files, LastSession.txt and the .kls files of "Save
// session to file" (Notebook::SaveLastSession, LoadLastSession and
// CheckLastSession at 20d647c4). CF3 (#490): sessions are JSON now
// (app/session_json.h); this grammar is only read, for a .kls the user opens
// and for the one conversion of LastSession.txt. Nothing writes it.
//
// Legacy wrote UTF-8 with a BOM (OpenWrite::FileWrite) and CRLF line ends:
//
//   [HikariSub v0.0.1]
//   [Close session]            <- only when written as the program closed
//   Tab: 0
//   Video: <path>              <- "" when the tab has no video
//   Position: <ms>
//   FFMS2: 1
//   Subtitles: <path>          <- "" for an Untitled tab
//   Active: <row>
//   Scroll: <row>
//   Editor: 1
//   Audio: <path>              <- only an audio file other than the video
//   Keyframes: <path>          <- only when a keyframes file was opened
//   Layout: <json>             <- WS1: the tab's panel arrangement, one line
//   Tab: 1
//   ...
//
// Reading follows LoadLastSession's tokenizer exactly: the text is split at
// "\n" with empty pieces dropped (wxTOKEN_STRTOK); the first piece must start
// with "[HikariSub" or the file is corrupt; a field takes the rest of its
// line (numbers through wxAtoi); a tab is finished by the next "Tab: " line
// other than "Tab: 0" or by the last line, so a session with no tab lines
// still gives one empty tab, and lines the reader does not know are skipped.
//
// "Layout: " was the rewrite's own field (WS1-session-layout): each Document
// tab's panel arrangement (ui::WorkspaceArrangement, its compact envelope).
// "FFMS2: " and "Editor: " are skipped: DirectShow and the player layout are
// gone.
//
// Departures (approved, docs/compatibility-decisions.md):
// - C05-audio-association: legacy never reset the Audio field between tabs,
//   so a tab without an "Audio:" line inherited the previous tab's audio.
//   Here each tab has only its own audio.
// - P6-session-crlf: the Windows build read in text mode (CRLF became LF);
//   the Linux build kept every '\r', so a session it wrote itself restored
//   as an empty first tab ("Tab: 0\r") with every path ending in '\r'. Here
//   CRLF is folded on both platforms.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct SessionTab {
    std::string video;     // "Video: "
    int position = 0;      // "Position: ", the video time in ms
    std::string subtitles; // "Subtitles: "
    int active = 0;        // "Active: ", the active Line's row among all Lines
    int scroll = 0;        // "Scroll: ", the Grid's first row
    std::string audio;     // "Audio: "; written only when not "" and not the video
    std::string keyframes; // "Keyframes: "
    std::string layout;    // "Layout: " (WS1), the arrangement's compact JSON
    bool operator==(const SessionTab &) const = default;
};

struct Session {
    std::string header; // the first line as read, e.g. "[HikariSub v0.0.1]"
    bool closed = false; // "[Close session]": written as the program closed
    std::vector<SessionTab> tabs;
    // CF3: the active Document tab, which only the JSON file marks; without
    // one the last tab is (legacy's AddPage made each loaded tab active).
    std::optional<std::size_t> activeTab;
};

// OpenWrite::FileOpen(path, &text, false): wxFFile::ReadAll through
// wxConvAuto. A UTF-8, UTF-16 or UTF-32 BOM decides (and is dropped);
// otherwise valid UTF-8 is UTF-8 and anything else ISO-8859-1. CRLF becomes LF
// first, as the Windows text-mode read did (on Linux too: P6-session-crlf).
// The text as UTF-8; nullopt when it is empty (FileOpen returned false and the
// session was ignored).
std::optional<std::string> decodeSessionBytes(std::string_view bytes);

// LoadLastSession's reading of `text`: nullopt when the file is corrupt (its
// first line does not start with "[HikariSub"; legacy logged "Session file is
// corrupt" and changed nothing).
std::optional<Session> parseSession(std::string_view text);

// CheckLastSession on a file that could be read: whether "]\n[Close
// session]\n" follows the header (CRLF and CR read as LF).
bool sessionClosed(std::string_view text);

} // namespace hikari::application
