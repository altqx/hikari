#pragma once

#include <bitset>
#include <cstddef>
#include <string>
#include <string_view>

namespace hikari::core::tagcleanup {

enum class TagFamily : int {
    Colour,       // \c \1c-\4c
    Alpha,        // \alpha \1a-\4a
    Border,       // \bord \xbord \ybord
    Shadow,       // \shad \xshad \yshad
    Blur,         // \blur \be
    Font,         // \fn \fs \fe
    Scale,        // \fscx \fscy
    Spacing,      // \fsp
    Rotation,     // \frz \fr \frx \fry
    Shear,        // \fax \fay
    FontStyle,    // \b \i \u \s
    Position,     // \pos \move
    Origin,       // \org
    Alignment,    // \an \a
    Clip,         // \clip \iclip
    Fade,         // \fad \fade
    Transform,    // \t
    Karaoke,      // \k \K \kf \ko \kt
    Reset,        // \r
    WrapStyle,    // \q
    Drawing,      // \p \pbo
    Other,        // any other tag
};
inline constexpr std::size_t kFamilyCount = 22;
using Families = std::bitset<kFamilyCount>;

// Start: the block the text begins with. Inline: every other block.
enum class BlockScope { Start, Inline, All };

struct Cleanup {
    bool removeComments = false; // blocks with no tag, as Script Cleanup's "Remove comments from lines"
    bool mergeBlocks = false;    // adjacent tag blocks, as Masquerade's tagmerge
    bool dropRedundant = false;  // tags a later one overrides, and repeats of line-wide tags
    bool cleanTags = false;      // the bundled cleantags.lua 1.301
    bool deleteEmptyLines = false;
    bool operator==(const Cleanup &) const = default;
};

struct Options {
    Families families;
    BlockScope scope = BlockScope::All;
    Cleanup cleanup;
};

TagFamily familyOf(std::string_view tagName);

// Removes the families' tags, inside \t too; a \t left with no tag goes, and so does a block the
// removal empties. Then the cleanup runs in Cleanup's field order.
std::u8string apply(std::u8string_view text, const Options &options);

std::u8string removeTags(std::u8string_view text, const Families &families, BlockScope scope);
std::u8string removeComments(std::u8string_view text);
std::u8string mergeBlocks(std::u8string_view text);
std::u8string dropRedundant(std::u8string_view text);
// Byte for byte what cleantags() in Automation/automation/Include/cleantags.lua returns.
std::u8string cleanTags(std::u8string_view text);

}
