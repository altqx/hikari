#pragma once

// Text and positions are UTF-16 code units; the text is coloured as one unwrapped row.

#include <cstddef>
#include <string_view>
#include <vector>

namespace hikari::core::syntax {

enum class TextState { Dialogue = 0, Comment = 1, Template = 2, Code = 3 };
// Template and Code are comments whose Effect starts with "template" / "code" (case-sensitive).
TextState textState(bool comment, std::u16string_view effect);
inline bool spellChecked(TextState state) { return state == TextState::Dialogue; }

enum class Role {
    Text = 0,
    CurlyBraces,
    Operators,
    Names,
    Values,
    SplitAndDrawings,
    TemplateVariables,
    TemplateCodeMarks,
    TemplateFunctions,
    TemplateKeywords,
    TemplateStrings,
};
inline constexpr std::size_t kRoleCount = 11;

// [start, end). A tab or a space between template tokens is in no run.
struct Run {
    std::size_t start = 0, end = 0;
    Role role = Role::Text;
    bool operator==(const Run &) const = default;
};
// In order, adjacent runs of one role merged, Text runs included.
std::vector<Run> colourRuns(std::u16string_view text, TextState state);

} // namespace hikari::core::syntax
