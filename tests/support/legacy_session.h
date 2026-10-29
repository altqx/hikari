#pragma once

// CF3 (#490): legacy SaveLastSession's bytes, for .kls and LastSession.txt
// fixtures. The program writes JSON sessions only; the tests still need the
// legacy files it reads.

#include "hikari/application/session_file.h"

#include <string>
#include <string_view>
#include <vector>

namespace hikari::testing {

// The file's bytes, with the "[Close session]" line when `closed`. `program`
// is legacy Options.progname ("HikariSub v<version>").
inline std::string writeLegacySession(std::string_view program, bool closed,
                                      const std::vector<application::SessionTab> &tabs)
{
    std::string out = "\xEF\xBB\xBF"; // FileWrite(utf = true): a BOM, then UTF-8
    out += "[";
    out += program;
    out += "]\r\n";
    if (closed)
        out += "[Close session]\r\n";
    int number = 0;
    for (const auto &tab : tabs) {
        out += "Tab: " + std::to_string(number) + "\r\nVideo: " + tab.video + "\r\nPosition: " +
               std::to_string(tab.position) + "\r\nFFMS2: 1\r\nSubtitles: " + tab.subtitles +
               "\r\nActive: " + std::to_string(tab.active) + "\r\nScroll: " + std::to_string(tab.scroll) +
               "\r\nEditor: 1\r\n";
        if (!tab.audio.empty() && tab.audio != tab.video)
            out += "Audio: " + tab.audio + "\r\n";
        if (!tab.keyframes.empty())
            out += "Keyframes: " + tab.keyframes + "\r\n";
        if (!tab.layout.empty())
            out += "Layout: " + tab.layout + "\r\n";
        ++number;
    }
    return out;
}

} // namespace hikari::testing
