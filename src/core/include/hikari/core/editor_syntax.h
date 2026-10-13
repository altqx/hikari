#pragma once

// E4: the Line editor's text colours (legacy TextEditor::DrawFieldD2D,
// HikariSub/DialogueTextEditor.cpp:1178-1612 at 20d647c4) and the state that
// picks them (TextEditor::SetState, :2915-2926, as EditBox::SetLine and
// OnCommit set it, EditBox.cpp:430-432, 974-977). Text and positions are
// UTF-16 code units, as legacy's wxString on Windows.
//
// Legacy colours each token as it draws a row; this walks the whole text as
// one row, so a token is coloured as legacy colours it on a row that does not
// wrap inside it (legacy also flushes the token being read at every wrap,
// with the colour the row's end gives it).

#include <cstddef>
#include <string_view>
#include <vector>

namespace hikari::core::syntax {

// TextEditor's `state`: 0 a dialogue Line, 1 a comment, 2 a comment whose
// Effect starts with "template", 3 one whose Effect starts with "code".
enum class TextState { Dialogue = 0, Comment = 1, Template = 2, Code = 3 };
// EditBox::SetLine / OnCommit: (!IsComment) ? 0 : Effect.StartsWith("template")
// ? 2 : Effect.StartsWith("code") ? 3 : 1 (case-sensitive).
TextState textState(bool comment, std::u16string_view effect);
// SetState's SpellCheckerOnOff: spelling marks only for a dialogue Line (the
// option decides there); none are drawn for a comment.
inline bool spellChecked(TextState state) { return state == TextState::Dialogue; }

// The colour roles, legacy's Options colours (config.cpp:432-442).
enum class Role {
    Text = 0,         // EDITOR_TEXT (the field's own text colour)
    CurlyBraces,      // EDITOR_CURLY_BRACES
    Operators,        // EDITOR_TAG_OPERATORS
    Names,            // EDITOR_TAG_NAMES
    Values,           // EDITOR_TAG_VALUES
    SplitAndDrawings, // EDITOR_SPLIT_LINES_AND_DRAWINGS
    TemplateVariables, // EDITOR_TEMPLATE_VARIABLES
    TemplateCodeMarks, // EDITOR_TEMPLATE_CODE_MARKS
    TemplateFunctions, // EDITOR_TEMPLATE_FUNCTIONS
    TemplateKeywords,  // EDITOR_TEMPLATE_KEYWORDS
    TemplateStrings,   // EDITOR_TEMPLATE_STRINGS
};
inline constexpr std::size_t kRoleCount = 11;

// [start, end) in the text, in one colour role. Characters legacy draws in
// no colour (a tab, a space between template tokens) are in no run.
struct Run {
    std::size_t start = 0, end = 0;
    Role role = Role::Text;
    bool operator==(const Run &) const = default;
};
// The text's runs in order, adjacent runs of one role merged; Text runs
// included.
std::vector<Run> colourRuns(std::u16string_view text, TextState state);

} // namespace hikari::core::syntax
