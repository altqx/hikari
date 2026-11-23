#pragma once

#include "hikari/core/document.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

// Aegisub keeps it in [Aegisub Project Garbage]; older files have it in Script Info.
inline constexpr std::u8string_view kTimecodesFileKey = u8"Timecodes File";
inline constexpr std::u8string_view kKeyframesFileKey = u8"Keyframes File";

// Project Garbage first, then the first [Script Info] entry (older files).
std::optional<std::string> projectValue(const core::Document &document, std::u8string_view key);

// What the file says about its view, as Aegisub 3.2 writes it.
struct SavedView {
    std::optional<int> activeLine, scroll, videoFrame;
    std::optional<double> aspect; // width / height, from Video AR Mode 1–4
};
SavedView savedView(const core::Document &document);

struct MediaReference {
    std::string authored;              // the value as written
    std::optional<std::string> resolved; // an existing file, or nullopt
};

struct MediaAssociations {
    std::optional<MediaReference> video, audio, keyframes, timecodes; // nullopt: no such entry
    // Resolved entries only.
    bool offersAnything() const;
};

// From projectValue. Relative paths resolve against the subtitle file's directory; a missing file leaves the entry
// unresolved.
// `windows` selects drive-letter ("X:...") absolute paths and "\" separators.
MediaAssociations resolveMediaAssociations(const core::Document &document, const std::string &subtitlePath,
                                           const std::function<bool(const std::string &)> &exists, bool windows);

}
