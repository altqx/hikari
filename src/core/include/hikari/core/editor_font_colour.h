#pragma once

#include "hikari/core/tag_commands.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core::legacy {

struct FontValues {
    std::u16string name;
    std::u16string size;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeOut = false;
    bool operator==(const FontValues &) const = default;
};

// What the dialog starts from for the line-based formats.
FontValues defaultFontValues();

// The Style's font with the b/i/u/s/fs/fn tags in effect at the selection; `position` gets the last
// search's position, where the caret goes after OK.
FontValues fontInEffect(const EditorText &state, FontValues style, long *position = nullptr);

// findTag(pattern) then putTagInText(tag, reset); with `nonAss`, putInNonAss. SRT skips a `valueFirst` step.
struct EditStep {
    std::u16string pattern;
    std::u16string tag;
    std::u16string reset;
    bool nonAss = false;
    // MUST KILL: valueFirst swaps them: the value is in `pattern`, the regex in `tag`
    bool valueFirst = false;
    // 'b', 'i', 'u' or 's' for the line formats, applied as the editor's buttons do.
    char16_t flag = 0;
};

// `actual` is the font the dialog opened with and gives the resets. Without `ass` the name and size go in
// as line-format markup, the rest as the editor's buttons do them.
std::vector<EditStep> fontSteps(const FontValues &edited, const FontValues &result, const FontValues &actual,
                                bool ass);

struct StepResult {
    EditorText state;
    long position = 0; // the position in the text after the steps
};
// The steps on the edited Line at its selection.
StepResult applySteps(EditorText state, const std::vector<EditStep> &steps, NonAssFormat format, long position);
// One of several selected Lines: override tags go into the translation when there is one, line-format
// markup into the text.
void applyStepsToLine(std::u16string &text, std::u16string &translation, const std::vector<EditStep> &steps,
                      NonAssFormat format);

// Past the block at `position` unless the character there is "}"; stays when no "}" follows.
long caretAfterDialog(std::u16string_view text, long position);

// 0-255 channels; `a` is the ASS alpha (0 = opaque).
struct TagColour {
    int r = 0, g = 0, b = 0, a = 0;
    bool operator==(const TagColour &) const = default;
};

// "&HAABBGGRR&", "&HBBGGRR", "#RRGGBB" or a decimal value.
TagColour parseAssColour(std::u16string_view text);
// "&H[AA]BBGGRR", with "&" unless `style`.
std::u16string assColourText(const TagColour &colour, bool alpha, bool style);

// The Style's colour `number` (1 primary, 2 secondary, 3 outline, 4 shadow) with the \<n>c and \<n>a/\alpha
// tags in effect.
TagColour colourInEffect(const EditorText &state, int number, TagColour style, long *position = nullptr);
// \<n>c when the RGB changed, then \<n>a when the alpha changed (after an \alpha found at the selection).
StepResult changeColour(EditorText state, int number, const TagColour &actual, const TagColour &chosen);
// The same with several Lines selected, on one Line's text (or translation).
std::u16string changeColourInLine(std::u16string text, int number, const TagColour &actual, const TagColour &chosen);
// "C:" + BBGGRR markup for the line-based formats.
EditStep colourNonAssStep(const TagColour &chosen);

}
