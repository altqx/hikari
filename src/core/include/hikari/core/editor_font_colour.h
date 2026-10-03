#pragma once

// E1: the Line editor's font and colour commands (legacy EditBox::OnFontClick,
// ChangeFont, AllColorClick, GetColor and OnColorChange at 20d647c4), over the
// TagFindReplace port in tag_commands.h. The dialogs report each change while
// they are open; every change is applied with the values the dialog started
// from as the reset after a selection.

#include "hikari/core/tag_commands.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core::legacy {

// The font fields the legacy FontDialog edits (Styles Fontname, Fontsize
// as text, Bold, Italic, Underline, StrikeOut).
struct FontValues {
    std::u16string name;
    std::u16string size;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeOut = false;
    bool operator==(const FontValues &) const = default;
};

// new Styles(): what the dialog starts from for the line-based formats.
FontValues defaultFontValues();

// OnFontClick (ASS): the Style's font with the b/i/u/s/fs/fn tags in effect
// at the selection. `position` gets the last search's position in the text
// (legacy GetPositionInText), which places the caret after OK.
FontValues fontInEffect(const EditorText &state, FontValues style, long *position = nullptr);

// One step of ChangeFont or OnColorChange: FindTag(pattern) and then
// PutTagInText(tag, reset, focus = false); or, with `nonAss`, PutinNonass with
// the legacy arguments (`pattern` is its regex text, `tag` what it writes).
struct EditStep {
    std::u16string pattern;
    std::u16string tag;
    std::u16string reset;
    bool nonAss = false;
};

// ChangeFont for a dialog change from `edited` to `result`; `actual` is the
// font the dialog opened with, which gives the resets. For the line-based
// formats (`ass` false) the name, size and bold/italic go through
// PutinNonass, underline and strikeout through override tags as for ASS.
std::vector<EditStep> fontSteps(const FontValues &edited, const FontValues &result, const FontValues &actual,
                                bool ass);

struct StepResult {
    EditorText state;
    long position = 0; // legacy GetPositionInText after the steps
};
// The steps on the edited Line at its selection.
StepResult applySteps(EditorText state, const std::vector<EditStep> &steps, NonAssFormat format, long position);
// The steps with several Lines selected, on one Line: override tags go into
// the translation when there is one (legacy SetText), PutinNonass always
// into the text.
void applyStepsToLine(std::u16string &text, std::u16string &translation, const std::vector<EditStep> &steps,
                      NonAssFormat format);

// After OK: the caret goes past the block at `position` (legacy: unless the
// character there is "}"), or stays when there is no "}" after it.
long caretAfterDialog(std::u16string_view text, long position);

// Legacy AssColor: 0-255 channels, a is the ASS alpha (0 = opaque).
struct TagColour {
    int r = 0, g = 0, b = 0, a = 0;
    bool operator==(const TagColour &) const = default;
};

// AssColor::SetAss: "&HAABBGGRR&", "&HBBGGRR", "#RRGGBB" or a decimal value.
TagColour parseAssColour(std::u16string_view text);
// AssColor::GetAss(alpha, style): "&H[AA]BBGGRR", with "&" unless `style`.
std::u16string assColourText(const TagColour &colour, bool alpha, bool style);

// GetColor (ASS): the Style's colour `number` (1 primary, 2 secondary,
// 3 outline, 4 shadow) with the \<n>c and \<n>a/\alpha tags in effect.
TagColour colourInEffect(const EditorText &state, int number, TagColour style, long *position = nullptr);
// OnColorChange (ASS) on the edited Line: \<n>c when the RGB changed, then
// \<n>a when the alpha changed (placed after an \alpha found at the selection).
StepResult changeColour(EditorText state, int number, const TagColour &actual, const TagColour &chosen);
// The same with several Lines selected, on one Line's text (or translation).
std::u16string changeColourInLine(std::u16string text, int number, const TagColour &actual, const TagColour &chosen);
// OnColorChange for the line-based formats: PutinNonass("C:" + &HBBGGRR without "&H", "C:([^}]*)").
EditStep colourNonAssStep(const TagColour &chosen);

} // namespace hikari::core::legacy
