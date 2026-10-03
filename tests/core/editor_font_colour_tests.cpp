// E1: the Line editor's font and colour commands against legacy
// EditBox::OnFontClick/ChangeFont/GetColor/OnColorChange/PutinNonass and
// TagFindReplace::PutTagInText at 20d647c4.

#include "hikari/core/editor_font_colour.h"

#include <gtest/gtest.h>

using namespace hikari::core::legacy;

namespace {

const FontValues arial{u"Arial", u"20"};

StepResult font(EditorText state, const FontValues &from, const FontValues &to)
{
    return applySteps(std::move(state), fontSteps(from, to, from, true), NonAssFormat::Srt, 0);
}

} // namespace

TEST(EditorFont, TheFontInEffectAtTheSelection)
{
    long position = -1;
    const auto f = fontInEffect({u"{\\b1\\fs30.5\\fnTimes New Roman}abc", 33, 33}, arial, &position);
    EXPECT_TRUE(f.bold);
    EXPECT_FALSE(f.italic);
    EXPECT_EQ(f.size, u"30.5");
    EXPECT_EQ(f.name, u"Times New Roman");
    EXPECT_EQ(fontInEffect({u"abc", 1, 1}, arial), arial);
}

TEST(EditorFont, ASelectionGetsTheTagAndTheFontItOpenedWith)
{
    FontValues bold = arial;
    bold.bold = true;
    EXPECT_EQ(font({u"abc", 0, 3}, arial, bold).state.text, u"{\\b1}abc{\\b0}");
    FontValues times = arial;
    times.name = u"Times";
    EXPECT_EQ(font({u"abc", 0, 3}, arial, times).state.text, u"{\\fnTimes}abc{\\fnArial}");
}

TEST(EditorFont, LaterChangesReplaceTheTagsInPlace)
{
    // The dialog reports each change against its previous state; the resets
    // stay those of the font it opened with.
    FontValues a = arial, b = arial;
    a.size = u"30";
    auto first = applySteps({u"abc", 1, 1}, fontSteps(arial, a, arial, true), NonAssFormat::Srt, 0);
    EXPECT_EQ(first.state.text, u"a{\\fs30}bc");
    b.size = u"40";
    auto second = applySteps(first.state, fontSteps(a, b, arial, true), NonAssFormat::Srt, first.position);
    EXPECT_EQ(second.state.text, u"a{\\fs40}bc");
    // OK puts the caret after the block.
    EXPECT_EQ(caretAfterDialog(second.state.text, second.position), 8);
}

TEST(EditorFont, LineFormatsUseTheLegacyPutinNonassArguments)
{
    FontValues times = defaultFontValues();
    times.name = u"Times";
    times.underline = true;
    const auto steps = fontSteps(defaultFontValues(), times, defaultFontValues(), false);
    ASSERT_EQ(steps.size(), 2u);
    // Legacy defect kept: the regex text is written instead of the font.
    EXPECT_EQ(applySteps({u"abc", 0, 0}, steps, NonAssFormat::MicroDvd, 0).state.text,
              u"{f:([^}]*)}{\\u1}abc");
    std::u16string text = u"{F:Arial}abc", translation;
    applyStepsToLine(text, translation, {steps[0]}, NonAssFormat::MicroDvd);
    EXPECT_EQ(text, u"{F:Times}abc"); // the old tag goes (the regex finds it)
}

TEST(EditorFont, SeveralLinesGetTheTagInTheirFirstBlock)
{
    FontValues bold = arial;
    bold.bold = true;
    const auto steps = fontSteps(arial, bold, arial, true);
    std::u16string text = u"{\\b0\\i1}abc", translation;
    applyStepsToLine(text, translation, steps, NonAssFormat::Srt);
    EXPECT_EQ(text, u"{\\b1\\i1}abc");
    text = u"abc";
    translation = u"xyz";
    applyStepsToLine(text, translation, steps, NonAssFormat::Srt);
    EXPECT_EQ(text, u"abc");
    EXPECT_EQ(translation, u"{\\b1}xyz");
}

TEST(EditorColour, LegacyAssColourText)
{
    EXPECT_EQ(parseAssColour(u"&H000000FF&"), (TagColour{255, 0, 0, 0}));
    EXPECT_EQ(parseAssColour(u"&H80FF0000"), (TagColour{0, 0, 255, 0x80}));
    EXPECT_EQ(parseAssColour(u"#FF8000"), (TagColour{255, 128, 0, 0}));
    EXPECT_EQ(parseAssColour(u"255"), (TagColour{255, 0, 0, 0}));
    EXPECT_EQ(assColourText({255, 128, 0, 0x40}, false, true), u"&H0080FF");
    EXPECT_EQ(assColourText({255, 128, 0, 0x40}, true, false), u"&H400080FF&");
}

TEST(EditorColour, TheColourInEffect)
{
    const TagColour white{255, 255, 255, 0};
    EXPECT_EQ(colourInEffect({u"{\\c&H0000FF&\\1a&H80&}x", 22, 22}, 1, white), (TagColour{255, 0, 0, 0x80}));
    EXPECT_EQ(colourInEffect({u"{\\3c&H00FF00&\\alpha&H40&}x", 25, 25}, 3, white), (TagColour{0, 255, 0, 0x40}));
    EXPECT_EQ(colourInEffect({u"x", 0, 0}, 2, white), white);
}

TEST(EditorColour, ColourAndAlphaTags)
{
    const TagColour white{255, 255, 255, 0};
    EXPECT_EQ(changeColour({u"abc", 0, 3}, 1, white, {255, 0, 0, 0}).state.text, u"{\\1c&H0000FF&}abc{\\1c&HFFFFFF&}");
    EXPECT_EQ(changeColour({u"abc", 1, 1}, 3, white, {255, 255, 255, 0x80}).state.text, u"a{\\3a&H80&}bc");
    // An existing colour at the caret's block is replaced in place; after the
    // block the caret gets a block of its own.
    EXPECT_EQ(changeColour({u"{\\1c&H00FF00&}abc", 16, 16}, 1, {0, 255, 0, 0}, {0, 0, 255, 0}).state.text,
              u"{\\1c&H00FF00&}ab{\\1c&HFF0000&}c");
    EXPECT_EQ(changeColour({u"{\\1c&H00FF00&}abc", 5, 5}, 1, {0, 255, 0, 0}, {0, 0, 255, 0}).state.text,
              u"{\\1c&HFF0000&}abc");
    EXPECT_EQ(changeColourInLine(u"{\\1c&H00FF00&}abc", 1, white, {0, 0, 255, 0}), u"{\\1c&HFF0000&}abc");
    EXPECT_EQ(changeColourInLine(u"abc", 4, white, {255, 255, 255, 0x10}), u"{\\4a&H10&}abc");
}

TEST(EditorColour, LineFormats)
{
    const auto step = colourNonAssStep({255, 0, 0, 0});
    EXPECT_EQ(step.pattern, u"C:0000FF");
    // Legacy defect kept: one Line gets the regex text...
    EXPECT_EQ(applySteps({u"abc", 1, 1}, {step}, NonAssFormat::MicroDvd, 0).state.text, u"{C:([^}]*)}abc");
    // ...several Lines get the colour, replacing theirs.
    std::u16string text = u"{c:00FF00}abc", translation;
    applyStepsToLine(text, translation, {step}, NonAssFormat::MicroDvd);
    EXPECT_EQ(text, u"{C:0000FF}abc");
}
