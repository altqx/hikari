#pragma once

// GR1 (#364): grammar check with Harper. What harper-ls is given for a Line's
// text and where its findings go back, and Harper's rules in the groups the
// Settings page switches.
//
// For ASS (and plain text) the text is the hidden-tag projection's
// (core::project, as TM1's tag transfer reads it): override blocks (karaoke
// tags too) and brace comments are left out; \N, \n and \h are a space; a
// drawing's commands and a malformed span are one space, never checked. Its
// offsets are the projection's (what the field shows with tags hidden), so a
// finding [start, end) is a projection range that mappedReplace applies a
// suggestion over. The other formats leave out the tags spelling skips there
// (legacy SpellChecker): SRT's <...>, with \N, \n and \h a space; TMPlayer's,
// MicroDVD's and MPL2's {...}, with "|" a space. rawRange gives the raw text
// a finding covers.

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

// Harper's rules (harper-core/default_config.json at the pinned release):
// each in one of the curated groups, with Harper's default.
struct Rule {
    std::string_view name;
    unsigned char group;
    bool on;
};

// The groups' ids in Harper's order ("properNouns" ... "miscellaneous"); the
// setting grammar.group.<id> switches each.
std::span<const std::string_view> groupIds();
std::span<const Rule> rules();
// The group a rule (a diagnostic's code) is in; nothing for one Harper
// doesn't list.
std::optional<std::size_t> groupOfRule(std::string_view rule);

// harper-ls's `linters`: every rule on when its group is on and Harper has it
// on, except two. SpellCheck is off while Hunspell checks spelling (a word is
// never marked twice), and SentenceCapitalization is always off, whatever its
// group's switch: a subtitle Line is often the middle of a sentence (owner,
// 2026-10-27).
std::vector<std::pair<std::string_view, bool>> linterSwitches(std::span<const bool> groupsOn, bool hunspellOn);

} // namespace hikari::core::grammar
