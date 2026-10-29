#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hikari::core {

// Every record whose source span is still valid is copied byte for byte,
// terminator included; an edited Line is regenerated and keeps its original
// terminator. An unchanged Document encodes to exactly its input.
std::vector<std::byte> encodeAss(const Document &document);

// hideOriginalOnVideo writes a TLMode original as a Comment; an unchanged
// pair with a translation or Unconfirmed is then rewritten too, keeping its
// translation line's bytes.
struct AssSaveOptions {
    bool hideOriginalOnVideo = false;
    // The script the video renderer reads. With "TLMode: Yes" a Line with a
    // translation is its original line (TLMode Style, "\fD" effect when
    // Unconfirmed; left out with hideOriginalOnVideo) then its translation
    // line; any other Line is its text alone. [Fonts] is left out
    // (ass_attachments.h); [Graphics] stays for VSFilterMod.
    bool renderer = false;
};
// An edited TLMode pair with a translation or Unconfirmed is the original line
// (TLMode Style, "\fD" effect when Unconfirmed) then the translation line;
// otherwise one line. "TLMode: Translated" writes only that one line.
std::vector<std::byte> encodeAss(const Document &document, const AssSaveOptions &options);

namespace legacy {
// "%01i:%02i:%02i.%02i", truncated to centiseconds.
std::u8string assTimeText(std::int64_t milliseconds);
// Without the line terminator.
std::u8string assLineText(const LineRecord &line);
}

}
