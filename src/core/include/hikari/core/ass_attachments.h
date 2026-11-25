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

struct Attachment {
    AttachmentKind kind = AttachmentKind::Font;
    std::string name;      // after "fontname:" or "filename:", as written (UTF-8)
    std::size_t size = 0;  // decoded bytes
};

// Every attachment of the Document's [Fonts] and [Graphics] sections (with the sections that
// continue them) in file order.
std::vector<Attachment> attachments(const Document &document);
// The decoded bytes of attachments()[index]; empty when there is none.
std::vector<std::byte> attachmentBytes(const Document &document, std::size_t index);

// The name Aegisub gives an attached file: a font's stem gets "_0" before its extension.
std::u8string attachmentName(AttachmentKind kind, std::u8string_view fileName);

// decodeAssAttachment's encoding, in lines of 80 characters.
std::vector<std::u8string> encodeAssAttachment(const std::vector<std::byte> &bytes);

// Whether a TrueType/OpenType font (the first of a collection) only allows Restricted License
// embedding: OS/2 fsType has bit 1 and neither Preview & Print nor Editable.
bool embeddingRestricted(const std::vector<std::byte> &font);

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

}
