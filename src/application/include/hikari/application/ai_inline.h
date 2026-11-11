#pragma once

#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// AI inline actions: one prompt per Line, built and checked with no Qt and no provider.
namespace hikari::application::ai_inline {

enum class Action { Rewrite, Shorten, Spelling, Translate, Custom };

std::string_view actionId(Action action);
std::optional<Action> actionFromId(std::string_view id);

enum class Skip { None, Comment, Empty, OnlyTags, Drawing, Karaoke };

// Why a Line isn't sent, or None.
Skip skipOf(const core::LineRecord &line);

// Override blocks become numbered markers, ⟦1⟧, ⟦2⟧…, so their content never leaves.
struct Masked {
    std::u8string text;
    std::vector<std::u8string> tags;
};
Masked mask(std::u8string_view text);
// Nullopt unless every marker comes back exactly once, in order.
std::optional<std::u8string> unmask(std::u8string_view answer, const std::vector<std::u8string> &tags);
// The text without its override blocks.
std::u8string stripped(std::u8string_view text);

struct Ask {
    Action action = Action::Rewrite;
    std::string instruction; // Rewrite and Custom
    std::string language;    // Translate
    int maxCps = 15;         // Shorten
};

struct Neighbours {
    std::vector<std::u8string> before; // oldest first
    std::vector<std::u8string> after;
};

// What one request asks, and how to read its answer back into the Line.
struct Prepared {
    std::string system;
    std::string user;
    std::u8string prefix, suffix; // the Line's text around the part that changes
    std::vector<std::u8string> tags;
    std::u8string sent;           // the part that changes, as the Line held it
};

// `span` limits the change to [first, second) bytes of the Line's text; the rest goes as context.
Prepared prepare(const Ask &ask, const core::LineRecord &line, const Neighbours &neighbours,
                 std::optional<std::pair<std::size_t, std::size_t>> span = std::nullopt);

enum class Outcome { Changed, Same, DroppedTag, Empty };
struct Answer {
    Outcome outcome = Outcome::Same;
    std::u8string text; // the Line's whole new text when Changed
};
Answer readAnswer(const Prepared &prepared, std::string_view answer);

}
