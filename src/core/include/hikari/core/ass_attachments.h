#pragma once

// [Fonts] decoded for the renderers, which take them as font leases: libass
// reads no font from a script and VSFilter would add every font again with
// each parse.

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

// ASS attachment uuencoding: each character is 6 bits plus 33, four make
// three bytes, a last two or three make one or two. Empty when a single
// character is left over, which neither VSFilter nor libass decodes.
std::vector<std::byte> decodeAssAttachment(std::string_view text);

// Whether `section` is the rest of the attachment before it: encoded lines
// use '!' to '`', so one in 64 starts with '[' and loads as a header. A real
// header has a space or a lowercase letter, which no encoded line has.
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
