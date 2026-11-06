#pragma once

#include "hikari/core/document.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hikari::application {

// Aegisub keeps it in [Aegisub Project Garbage]; older files have it in Script Info.
inline constexpr std::u8string_view kTimecodesFileKey = u8"Timecodes File";

struct MediaReference {
    std::string authored;              // the Script Info value as written
    std::optional<std::string> resolved; // an existing file, or nullopt
};

struct MediaAssociations {
    std::optional<MediaReference> video, audio, keyframes, timecodes; // nullopt: no such entry
    // Resolved entries only.
    bool offersAnything() const;
};

// From the Document's own Script Info, and its Project Garbage for timecodes. Relative paths resolve against the
// subtitle file's directory; a missing file leaves the entry unresolved.
// `windows` selects drive-letter ("X:...") absolute paths and "\" separators.
MediaAssociations resolveMediaAssociations(const core::Document &document, const std::string &subtitlePath,
                                           const std::function<bool(const std::string &)> &exists, bool windows);

}
