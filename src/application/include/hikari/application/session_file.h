#pragma once

// Session files written by HikariSub legacy (LastSession.txt, .kls); read only.
// A tab ends at the next "Tab: " line other than "Tab: 0" or at the last line;
// unknown lines (FFMS2:, Editor:) are skipped. Each tab has only its own audio.

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
    std::string layout;    // "Layout: ", the arrangement's compact JSON
    bool operator==(const SessionTab &) const = default;
};

struct Session {
    std::string header; // the first line as read, e.g. "[HikariSub v0.0.1]"
    bool closed = false; // "[Close session]": written as the program closed
    std::vector<SessionTab> tabs;
    // Only the JSON session file marks it; nullopt: the last tab.
    std::optional<std::size_t> activeTab;
};

// A UTF-8, UTF-16 or UTF-32 BOM decides (and is dropped); otherwise CRLF becomes
// LF, then valid UTF-8 is UTF-8, else ISO-8859-1. nullopt when the text is empty.
std::optional<std::string> decodeSessionBytes(std::string_view bytes);

// nullopt when the first line does not start with "[HikariSub".
std::optional<Session> parseSession(std::string_view text);

// Whether "]\n[Close session]\n" follows the header (CRLF and CR read as LF).
bool sessionClosed(std::string_view text);

}
