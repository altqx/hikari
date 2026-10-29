#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::core::grammar {

struct CheckedText {
    std::u16string text;
    // The raw [start, end) of each UTF-16 unit of `text` (a break's space
    // covers its "\N").
    std::vector<std::pair<std::size_t, std::size_t>> raw;
};

// ASS: the hidden-tag projection (tags and comments out; \N, \n, \h a space; a drawing or a
// malformed span one space). SRT drops <...>; TMPlayer, MicroDVD and MPL2 drop {...}, "|" a space.
CheckedText checkedText(std::u16string_view raw, SubtitleFormat format = SubtitleFormat::Ass);

// A suggestion over checkedText(raw, format)'s units [start, end): the new raw
// text and its caret after the replacement. The tags inside the range stay,
// after it. Nothing when the range is empty or outside the text, touches a
// drawing or a malformed span, or the replacement has the format's syntax.
struct Replaced {
    std::u16string raw;
    std::size_t caret = 0;
};
std::optional<Replaced> replace(std::u16string_view raw, SubtitleFormat format, std::size_t start, std::size_t end,
                                std::u16string_view replacement);

// The raw range under text units [start, end); nothing for an empty or
// out-of-range one.
std::optional<std::pair<std::size_t, std::size_t>> rawRange(const CheckedText &text, std::size_t start,
                                                             std::size_t end);

// A harper-ls message as plain text: its Markdown `code` spans in quotation
// marks, a backtick without its pair left out.
std::u16string plainMessage(std::u16string_view markdown);

// The last (first) `most` units, cut back to a whole word where text is left out, with an
// ellipsis there. A longer word is cut.
std::u16string contextBefore(std::u16string_view before, std::size_t most);
std::u16string contextAfter(std::u16string_view after, std::size_t most);

// Harper's rules (its default_config.json at the pinned release), each in a curated group, with
// Harper's default.
struct Rule {
    std::string_view name;
    unsigned char group;
    bool on;
};

// In Harper's order; the setting grammar.group.<id> switches each.
std::span<const std::string_view> groupIds();
std::span<const Rule> rules();
// The group a rule (a diagnostic's code) is in; nothing for one Harper
// doesn't list.
std::optional<std::size_t> groupOfRule(std::string_view rule);

// Every rule on when its group and Harper's default are on, except SpellCheck (off while
// Hunspell checks spelling) and SentenceCapitalization (always off).
std::vector<std::pair<std::string_view, bool>> linterSwitches(std::span<const bool> groupsOn, bool hunspellOn);

}
