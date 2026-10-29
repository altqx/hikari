#pragma once

#include "hikari/core/document.h"

#include <functional>
#include <optional>
#include <string>

namespace hikari::application {

struct MediaReference {
    std::string authored;              // the Script Info value as written
    std::optional<std::string> resolved; // an existing file, or nullopt
};

struct MediaAssociations {
    std::optional<MediaReference> video, audio, keyframes; // nullopt: no such Script Info entry
    // Resolved entries only.
    bool offersAnything() const;
};

// From the Document's own Script Info only. Relative paths resolve against the
// subtitle file's directory; a missing file leaves the entry unresolved.
// `windows` selects drive-letter ("X:...") absolute paths and "\" separators.
MediaAssociations resolveMediaAssociations(const core::Document &document, const std::string &subtitlePath,
                                           const std::function<bool(const std::string &)> &exists, bool windows);

} // namespace hikari::application
