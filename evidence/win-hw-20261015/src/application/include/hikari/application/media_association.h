#pragma once

// A Document's associated media (I1; legacy Notebook::LoadVideo with
// loadPrompt). The association comes only from the Document's own Script
// Info ("Video File", "Audio File", "Keyframes File"): approved
// C05-audio-association, never another tab's. A path is used as given when it
// is absolute, otherwise relative to the subtitle file's directory; a path
// that does not exist leaves that association unresolved, and nothing is
// offered for it.

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
    // What the legacy confirmation offers: resolved entries only.
    bool offersAnything() const;
};

// `exists` checks a path on disk; `windows` selects the platform's absolute
// path and separator rules (legacy: a drive letter, "X:...").
MediaAssociations resolveMediaAssociations(const core::Document &document, const std::string &subtitlePath,
                                           const std::function<bool(const std::string &)> &exists, bool windows);

} // namespace hikari::application
