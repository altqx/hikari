#include "hikari/application/text_commands.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::text_commands;

namespace {

std::u16string asciiMapped(std::u16string_view t, bool upper)
{
    std::u16string out(t);
    for (auto &c : out)
        if (upper && c >= u'a' && c <= u'z')
            c = static_cast<char16_t>(c - 32);
        else if (!upper && c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c + 32);
    return out;
}

TextServices ascii()
{
    return {
        [](std::u16string_view t) { return asciiMapped(t, true); },
        [](std::u16string_view t) { return asciiMapped(t, false); },
        [](std::u16string_view t) {
            std::vector<std::size_t> out;
            for (std::size_t i = 1; i < t.size(); ++i)
                if (t[i - 1] == u' ' && t[i] != u' ')
                    out.push_back(i);
            return out;
        },
    };
}

std::u8string u8(std::string_view s) { return std::u8string(s.begin(), s.end()); }

std::u8string changed(std::string_view text, Case kind) { return changeCase(u8(text), kind, ascii()); }

double shownLength(std::u8string_view prefix)
{
    return static_cast<double>(core::project(core::toUtf16(prefix)).text.size());
}

}

TEST(TextCommands, LowerAndUpperLeaveTagsBreaksAndDrawings)
{
    EXPECT_EQ(changed(R"({\b1}HeLLo\NWORLD{\p1}M 0 0 L{\p0} \hX)", Case::Lower),
              u8(R"({\b1}hello\Nworld{\p1}M 0 0 L{\p0} \hx)"));
    EXPECT_EQ(changed("say {Note: Keep} hi\\n", Case::Upper), u8("SAY {Note: Keep} HI\\n"));
}

TEST(TextCommands, TitleCaseFollowsFluxTitleCase)
{
    EXPECT_EQ(changed("the lord of the rings", Case::Title), u8("The Lord of the Rings"));
    EXPECT_EQ(changed("a tale of two cities: the end", Case::Title), u8("A Tale of Two Cities: The End"));
    EXPECT_EQ(changed("my iPhone is new", Case::Title), u8("My iPhone Is New"));
    EXPECT_EQ(changed("THE KING OF THE HILL", Case::Title), u8("The King of the Hill"));
    EXPECT_EQ(changed(R"({\i1}war{\i0} and peace)", Case::Title), u8(R"({\i1}War{\i0} and Peace)"));
    EXPECT_EQ(changed("'twas the night", Case::Title), u8("'Twas the Night"));
    EXPECT_EQ(changed(R"(go up\Nand away)", Case::Title), u8(R"(Go up\Nand Away)"));
}

TEST(TextCommands, SentenceCaseCapitalisesSentencesAndKeepsNames)
{
    EXPECT_EQ(changed(R"(hello there. how are you? i'm fine!\Nyes.)", Case::Sentence),
              u8(R"(Hello there. How are you? I'm fine!\NYes.)"));
    EXPECT_EQ(changed("WHAT IS THIS. NOTHING.", Case::Sentence), u8("What is this. Nothing."));
    EXPECT_EQ(changed("my friend in Tokyo", Case::Sentence), u8("My friend in Tokyo"));
    EXPECT_EQ(changed("and i said i'd go", Case::Sentence), u8("And I said I'd go"));
    EXPECT_EQ(changed(R"({\an8}"wait," he said)", Case::Sentence), u8(R"({\an8}"Wait," he said)"));
}

TEST(TextCommands, FullWidthMatchesTheBundledDemoByteForByte)
{
    std::ifstream golden(HIKARI_AU20_FIXTURES "/fullwidth-golden.tsv", std::ios::binary);
    ASSERT_TRUE(golden);
    int rows = 0;
    for (std::string row; std::getline(golden, row); ++rows) {
        const auto tab = row.find('\t');
        ASSERT_NE(tab, std::string::npos);
        EXPECT_EQ(changeWidth(u8(row.substr(0, tab)), Width::Full), u8(row.substr(tab + 1))) << row;
    }
    EXPECT_EQ(rows, 8);
}

TEST(TextCommands, FullWidthKeepsBreaksAndDrawingsUnlikeTheDemo)
{
    EXPECT_EQ(changeWidth(u8(R"(a\Nb\hc\nd)"), Width::Full), u8(R"(ａ\Nｂ\hｃ\nｄ)"));
    EXPECT_EQ(changeWidth(u8(R"({\p1}m 0 0 l 1 1{\p0}x)"), Width::Full), u8(R"({\p1}m 0 0 l 1 1{\p0}ｘ)"));
}

TEST(TextCommands, HalfWidthNeverMakesTagsOrEscapes)
{
    EXPECT_EQ(changeWidth(u8("Ｈｅｌｌｏ　ｗｏｒｌｄ！ ｛＼Ｎ｝"), Width::Half), u8("Hello world! ｛＼N｝"));
    EXPECT_EQ(changeWidth(changeWidth(u8("Plain (text) 42!"), Width::Full), Width::Half), u8("Plain (text) 42!"));
}

TEST(TextCommands, SwapAlternativesIsFluxDialogSwapper)
{
    EXPECT_EQ(swapAlternatives(u8("Tanaka{*}-san{*}")), u8("Tanaka{**-san}"));
    EXPECT_EQ(swapAlternatives(u8("Tanaka{**-san}")), u8("Tanaka{*}-san{*}"));
    EXPECT_EQ(swapAlternatives(u8("{*}Big brother{*Onii-chan}!")), u8("{*}Onii-chan{*Big brother}!"));
    EXPECT_EQ(swapAlternatives(u8("{*}Onii-chan{*Big brother}!")), u8("{*}Big brother{*Onii-chan}!"));
    EXPECT_EQ(swapAlternatives(u8(R"({\b1}x{*note})")), u8(R"({\b1}x{*note})"));
}

TEST(TextCommands, RemoveBreaksLeavesOneSpace)
{
    EXPECT_EQ(removeBreaks(u8(R"(one two\Nthree)"), ascii()), u8("one two three"));
    EXPECT_EQ(removeBreaks(u8(R"(one \N three\Nfour)"), ascii()), u8("one three four"));
    EXPECT_EQ(removeBreaks(u8(R"(one {\i1}\N{\i0}two)"), ascii()), u8(R"(one {\i1}{\i0}two)"));
    EXPECT_EQ(removeBreaks(u8(R"(one{\i1}\N{\i0}two)"), ascii()), u8(R"(one{\i1} {\i0}two)"));
}

TEST(TextCommands, MoveBreakStepsOneWordAndTagsGoWithTheWord)
{
    const auto s = ascii();
    EXPECT_EQ(moveBreak(u8("one two three four"), Direction::Right, s), u8(R"(one\Ntwo three four)"));
    EXPECT_EQ(moveBreak(u8("one two three four"), Direction::Left, s), u8(R"(one two three\Nfour)"));
    EXPECT_EQ(moveBreak(u8(R"(one two\Nthree four)"), Direction::Right, s), u8(R"(one two three\Nfour)"));
    EXPECT_EQ(moveBreak(u8(R"(one two\Nthree four)"), Direction::Left, s), u8(R"(one\Ntwo three four)"));
    EXPECT_EQ(moveBreak(u8(R"(one two three\Nfour)"), Direction::Right, s), u8("one two three four"));
    EXPECT_EQ(moveBreak(u8(R"(one {\i1}two\Nthree)"), Direction::Left, s), u8(R"(one\N{\i1}two three)"));
    EXPECT_EQ(moveBreak(u8("single"), Direction::Right, s), u8("single"));
}

TEST(TextCommands, BalancedBreakEvensTheHalvesAndPrefersPunctuationNearby)
{
    const auto s = ascii();
    EXPECT_EQ(balancedBreak(u8("aaa bbb ccc ddd"), s, shownLength), u8(R"(aaa bbb\Nccc ddd)"));
    EXPECT_EQ(balancedBreak(u8("it was late, so all went home"), s, shownLength), u8(R"(it was late,\Nso all went home)"));
    EXPECT_EQ(balancedBreak(u8("we went there, and then we came back home"), s, shownLength),
              u8(R"(we went there, and\Nthen we came back home)"));
    EXPECT_EQ(balancedBreak(u8(R"(aaa\Nbbb ccc ddd)"), s, shownLength), u8(R"(aaa bbb\Nccc ddd)"));
    EXPECT_EQ(balancedBreak(u8(R"({\an8}aaa {\b1}bbb ccc ddd)"), s, shownLength), u8(R"({\an8}aaa {\b1}bbb\Nccc ddd)"));
    EXPECT_EQ(balancedBreak(u8("word"), s, shownLength), u8("word"));
}

TEST(TextCommands, WrapStyle2GoesInTheStartBlock)
{
    EXPECT_FALSE(hasWrapStyle2(u8("text")));
    EXPECT_TRUE(hasWrapStyle2(u8(R"({\b1\q2}text)")));
    EXPECT_FALSE(hasWrapStyle2(u8(R"(a{\q2}text)")));
    EXPECT_FALSE(hasWrapStyle2(u8(R"({\q22}text)")));
    EXPECT_EQ(setWrapStyle2(u8("text"), true), u8(R"({\q2}text)"));
    EXPECT_EQ(setWrapStyle2(u8(R"({\b1}text)"), true), u8(R"({\q2\b1}text)"));
    EXPECT_EQ(setWrapStyle2(u8(R"({\q1\b1}text)"), true), u8(R"({\q2\b1}text)"));
    EXPECT_EQ(setWrapStyle2(u8(R"({\q2}text)"), false), u8("text"));
    EXPECT_EQ(setWrapStyle2(u8(R"({\b1\q2}text)"), false), u8(R"({\b1}text)"));
}

namespace {

constexpr std::string_view kScript =
    "[Script Info]\n"
    "PlayResX: 640\n"
    "PlayResY: 480\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,hello world\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,***,Tanaka{*}-san{*}\n"
    "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,untouched\n";

class CharacterWidths : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &, const std::string &text) override
    {
        return TextExtents{static_cast<double>(core::toUtf16(std::u8string(text.begin(), text.end())).size()), 20, 4, 0};
    }
};

EditSession session()
{
    std::vector<std::byte> bytes(kScript.size());
    std::memcpy(bytes.data(), kScript.data(), kScript.size());
    return EditSession(core::loadAss(bytes).document);
}

std::vector<std::u8string> texts(const EditSession &s)
{
    std::vector<std::u8string> out;
    for (const auto *l : s.document().lines())
        out.push_back(l->text);
    return out;
}

void selectFirstTwo(EditSession &s)
{
    const auto lines = s.document().lines();
    s.setSelection(Selection{lines[0]->id, {lines[0]->id, lines[1]->id}});
}

}

TEST(TextCommands, RunEditsTheSelectionAsOneStep)
{
    auto s = session();
    selectFirstTwo(s);
    CharacterWidths widths;
    ASSERT_TRUE(run(s, text_commands::Command::Uppercase, ascii(), widths).has_value());
    EXPECT_EQ(texts(s), (std::vector<std::u8string>{u8"HELLO WORLD", u8"TANAKA{*}-SAN{*}", u8"untouched"}));
    ASSERT_TRUE(s.undo());
    EXPECT_EQ(texts(s), (std::vector<std::u8string>{u8"hello world", u8"Tanaka{*}-san{*}", u8"untouched"}));
}

TEST(TextCommands, SwapTogglesTheCommentOfStarredEffects)
{
    auto s = session();
    selectFirstTwo(s);
    CharacterWidths widths;
    ASSERT_TRUE(run(s, text_commands::Command::SwapAlternatives, ascii(), widths).has_value());
    const auto lines = s.document().lines();
    EXPECT_EQ(lines[1]->text, u8"Tanaka{**-san}");
    EXPECT_TRUE(lines[1]->comment);
    EXPECT_FALSE(lines[0]->comment);
}

TEST(TextCommands, WrapToggleSetsAllThenClearsAll)
{
    auto s = session();
    selectFirstTwo(s);
    CharacterWidths widths;
    EXPECT_FALSE(selectionWrapsAtBreaksOnly(s));
    ASSERT_TRUE(run(s, text_commands::Command::ToggleWrapStyle2, ascii(), widths).has_value());
    EXPECT_TRUE(selectionWrapsAtBreaksOnly(s));
    EXPECT_EQ(texts(s)[0], u8R"({\q2}hello world)");
    ASSERT_TRUE(run(s, text_commands::Command::ToggleWrapStyle2, ascii(), widths).has_value());
    EXPECT_EQ(texts(s)[0], u8"hello world");
}

TEST(TextCommands, BalancedBreakMeasuresWithTheLineStyle)
{
    auto s = session();
    selectFirstTwo(s);
    CharacterWidths widths;
    ASSERT_TRUE(run(s, text_commands::Command::InsertBalancedBreak, ascii(), widths).has_value());
    EXPECT_EQ(texts(s)[0], u8R"(hello\Nworld)");
}

TEST(TextCommands, NothingSelectedIsRefused)
{
    auto s = session();
    s.setSelection({});
    CharacterWidths widths;
    EXPECT_FALSE(run(s, text_commands::Command::Lowercase, ascii(), widths).has_value());
}
