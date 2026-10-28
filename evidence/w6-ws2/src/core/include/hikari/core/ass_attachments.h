#pragma once

// 38c (user decision, 2026-10-14): the fonts a script carries in its [Fonts]
// sections, decoded for the subtitle renderers. The renderer script
// (AssSaveOptions::renderer) leaves [Fonts] out: the renderers take the fonts
// as font leases, added to libass's library and, for a CSRI renderer, to the
// process once each, where libass reads no font from a script (no
// ass_set_extract_fonts) and VSFilter would add every font of the text to
// the process again with each parse. [Graphics] stays in the script, where
// VSFilterMod reads the images of \1img and the other image tags.

#include "hikari/core/document.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

struct AttachedFont {
    std::string name; // after "fontname:", as written (UTF-8)
    std::vector<std::byte> bytes;
};

// The uuencoding of ASS attachments, as Aegisub writes it and VSFilter and
// libass read it: each character is 6 bits plus 33, four characters make
// three bytes, a last two or three characters one or two bytes. Empty when
// the length leaves a single character over, which neither renderer decodes.
std::vector<std::byte> decodeAssAttachment(std::string_view text);

// Whether `section` is the rest of the attachment section before it: the
// encoding's characters are '!' to '`', so one line in 64 of an attachment
// begins with '[', and the loader takes it for a header as legacy's did
// (SubsLoader::LoadASS), making an Unknown section of it and what follows.
// A real header has a space or a lowercase letter, which no such line has.
bool continuesAttachment(const Section &section);

// Each font of the Document's [Fonts] sections in order (with the sections
// that continue them); a font whose data does not decode is left out.
std::vector<AttachedFont> attachedFonts(const Document &document);

// What changes when the attached fonts may: the number of [Fonts] records
// (and of the sections that continue them) and their bytes, without
// decoding anything.
struct AttachedFontsKey {
    std::size_t records = 0;
    std::size_t bytes = 0;
    bool operator==(const AttachedFontsKey &) const = default;
};
AttachedFontsKey attachedFontsKey(const Document &document);

} // namespace hikari::core
