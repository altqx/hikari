// R1: subtitle comparison against legacy SubsGrid::SubsComparison,
// CompareTexts, RemoveComparison and GetCommonStyles (SubsGridBase.cpp) and
// the Notebook tab menu's handlers (Notebook.cpp) at 20d647c4. Expected
// values are worked through the legacy code by hand; each case names the
// lines it follows. LegacyComparisonCapture replays the legacy probe's
// observations (tools/legacy-capture/comparison_capture.cpp).

#include "hikari/application/subtitle_comparison.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kStyles =
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n";
constexpr std::string_view kStyleLine =
    ",Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n";

std::string styles(std::initializer_list<const char *> names)
{
    std::string out(kStyles);
    for (const char *name : names)
        out += std::string("Style: ") + name + std::string(kStyleLine);
    return out;
}

// The first Document (legacy CG1, the active tab).
const std::string kFirst = styles({"Default", "Sign"}) +
                           "\n[Events]\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,two\n"
                           "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n";
// The second (CG2, the tab the menu was opened on).
const std::string kSecond = styles({"Sign", "Other", "Default"}) +
                            "\n[Events]\n"
                            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                            "Dialogue: 0,0:00:03.50,0:00:04.00,Sign,,0,0,0,,too\n"
                            "Dialogue: 0,0:00:05.00,0:00:06.00,Sign,,0,0,0,,three\n"
                            "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,four\n";

// A row's state as the Grid paints it (SubsGridWindow.cpp:419-426): '=' the
// match colour, 'x' the mismatch colour, '.' neither.
std::string states(const std::vector<LineComparison> &rows)
{
    std::string out;
    for (const auto &row : rows)
        out += row.mismatch() ? 'x' : row.match() ? '=' : '.';
    return out;
}

// The pairs as "i-j" for each row of the first that has a partner.
std::string pairs(const ComparisonResult &result)
{
    std::string out;
    for (std::size_t i = 0; i < result.first.size(); ++i)
        if (result.first[i].matchedRow) {
            if (!out.empty())
                out += ' ';
            out += std::to_string(i) + '-' + std::to_string(*result.first[i].matchedRow);
            EXPECT_EQ(result.second[*result.first[i].matchedRow].matchedRow, i);
        }
    return out;
}

struct ComparisonTest : ::testing::Test {
    core::Document a = load(kFirst);
    core::Document b = load(kSecond);
    ComparedDocument first() const { return {&a, selectedA, false}; }
    ComparedDocument second() const { return {&b, selectedB, false}; }
    std::set<core::LineId> selectedA, selectedB;
    core::LineId row(const core::Document &d, std::size_t i) const { return d.lines()[i]->id; }
    void hide(core::Document &d, std::size_t i)
    {
        d.editLine(row(d, i), [](core::LineRecord &l) { l.visibility = core::LineVisibility::Hidden; });
    }
};

std::u16string u(const char16_t *s)
{
    return s;
}

// SubsGridBase.cpp:1796-1884 transcribed line by line (wxString as UTF-16 or
// UTF-32, wxArrayInt as a vector, the size guards left out), to check the
// port against on many texts.
template <typename Text>
void legacyCompareTexts(LineComparison &firstCompare, LineComparison &secondCompare, const Text &first,
                        const Text &second)
{
    if (first == second) {
        firstCompare.differences = false;
        secondCompare.differences = false;
        return;
    }
    firstCompare.marks.push_back(1);
    secondCompare.marks.push_back(1);
    size_t l1 = first.length(), l2 = second.length();
    size_t cellCount = (l1 + 1) * (l2 + 1);
    size_t w = l2 + 1;
    size_t i1, i2;
    std::vector<size_t> dpt;
    dpt.assign(cellCount, 0);
    for (i1 = 1; i1 <= l1; i1++) {
        for (i2 = 1; i2 <= l2; i2++) {
            if (first[l1 - i1] == second[l2 - i2]) {
                dpt[w * i1 + i2] = dpt[w * (i1 - 1) + (i2 - 1)] + 1;
            } else if (dpt[w * (i1 - 1) + i2] > dpt[w * i1 + (i2 - 1)]) {
                dpt[w * i1 + i2] = dpt[w * (i1 - 1) + i2];
            } else {
                dpt[w * i1 + i2] = dpt[w * i1 + (i2 - 1)];
            }
        }
    }
    int sfirst = -1, ssecond = -1;
    i1 = l1;
    i2 = l2;
    for (;;) {
        if ((i1 > 0) && (i2 > 0) && (first[l1 - i1] == second[l2 - i2])) {
            if (sfirst >= 0) {
                firstCompare.marks.push_back(sfirst);
                firstCompare.marks.push_back(static_cast<int>((l1 - i1) - 1));
                sfirst = -1;
            }
            if (ssecond >= 0) {
                secondCompare.marks.push_back(ssecond);
                secondCompare.marks.push_back(static_cast<int>((l2 - i2) - 1));
                ssecond = -1;
            }
            i1--;
            i2--;
            continue;
        } else {
            if (i1 > 0 && (i2 == 0 || dpt[w * (i1 - 1) + i2] >= dpt[w * i1 + (i2 - 1)])) {
                if (sfirst == -1) {
                    sfirst = static_cast<int>(l1 - i1);
                }
                i1--;
                continue;
            } else if (i2 > 0 && (i1 == 0 || dpt[w * (i1 - 1) + i2] < dpt[w * i1 + (i2 - 1)])) {
                if (ssecond == -1) {
                    ssecond = static_cast<int>(l2 - i2);
                }
                i2--;
                continue;
            }
        }
        break;
    }
    if (sfirst >= 0) {
        firstCompare.marks.push_back(sfirst);
        firstCompare.marks.push_back(static_cast<int>((l1 - i1) - 1));
    }
    if (ssecond >= 0) {
        secondCompare.marks.push_back(ssecond);
        secondCompare.marks.push_back(static_cast<int>((l2 - i2) - 1));
    }
}

} // namespace

// SubsGridBase.cpp:1796-1801: equal texts are a match without ranges.
TEST(CompareTexts, EqualTextsMatch)
{
    LineComparison x, y;
    compareTexts(x, y, u(u"same"), u(u"same"));
    EXPECT_FALSE(x.differences);
    EXPECT_FALSE(y.differences);
    EXPECT_TRUE(x.marks.empty());
    EXPECT_TRUE(x.match());
    EXPECT_FALSE(x.mismatch());
}

// SubsGridBase.cpp:1803-1883: the leading 1, then inclusive ranges of the
// characters outside the common subsequence, in each text's own offsets.
TEST(CompareTexts, RangesOfTheDifferingCharacters)
{
    LineComparison x, y;
    compareTexts(x, y, u(u"cat"), u(u"cut"));
    EXPECT_EQ(x.marks, (std::vector<int>{1, 1, 1}));
    EXPECT_EQ(y.marks, (std::vector<int>{1, 1, 1}));
    EXPECT_TRUE(x.differences);
    EXPECT_TRUE(x.mismatch());

    // Several runs, of other lengths in each text.
    LineComparison p, q;
    compareTexts(p, q, u(u"I saw a cat."), u(u"I sawed the cat!"));
    EXPECT_EQ(p.marks, (std::vector<int>{1, 6, 6, 11, 11}));
    EXPECT_EQ(q.marks, (std::vector<int>{1, 5, 6, 8, 10, 15, 15}));
}

// A text that is a prefix of the other has no range of its own, but it is
// still a mismatch: the leading 1 alone (legacy size() > 0).
TEST(CompareTexts, ATextContainedInTheOtherIsStillAMismatch)
{
    LineComparison x, y;
    compareTexts(x, y, u(u"abc"), u(u"abcde"));
    EXPECT_EQ(x.marks, (std::vector<int>{1}));
    EXPECT_EQ(y.marks, (std::vector<int>{1, 3, 4}));
    EXPECT_TRUE(x.mismatch());

    LineComparison e, f;
    compareTexts(e, f, u(u""), u(u"x"));
    EXPECT_EQ(e.marks, (std::vector<int>{1}));
    EXPECT_EQ(f.marks, (std::vector<int>{1, 0, 0}));
}

// wxString compares UTF-16 code units on Windows: a character outside the
// BMP is two units, and a range covers both.
TEST(CompareTexts, OffsetsAreUtf16CodeUnits)
{
    LineComparison x, y;
    compareTexts(x, y, u(u"a\U0001F600b"), u(u"ab"));
    EXPECT_EQ(x.marks, (std::vector<int>{1, 1, 2}));
    EXPECT_EQ(y.marks, (std::vector<int>{1}));
    // U+1F600 and U+1F601 share their high surrogate: only the low one differs.
    LineComparison p, q;
    compareTexts(p, q, u(u"\U0001F600"), u(u"\U0001F601"));
    EXPECT_EQ(p.marks, (std::vector<int>{1, 1, 1}));
}

// wxString compares code points on Linux (wxGTK): a character outside the BMP
// is one position.
TEST(CompareTexts, OffsetsAreCodePointsOnTheLinuxBuild)
{
    LineComparison x, y;
    compareTexts(x, y, std::u32string_view(U"a\U0001F600b"), std::u32string_view(U"ab"));
    EXPECT_EQ(x.marks, (std::vector<int>{1, 1, 1}));
    EXPECT_EQ(y.marks, (std::vector<int>{1}));
    LineComparison p, q;
    compareTexts(p, q, std::u32string_view(U"\U0001F600"), std::u32string_view(U"\U0001F601"));
    EXPECT_EQ(p.marks, (std::vector<int>{1, 0, 0}));
}

// R5-per-platform: each platform compares as its own legacy build did.
TEST(CompareTexts, EachPlatformCountsAsItsLegacyBuild)
{
#ifdef _WIN32
    EXPECT_EQ(kLegacyTextUnits, TextUnits::Utf16);
#else
    EXPECT_EQ(kLegacyTextUnits, TextUnits::CodePoints);
#endif
    const auto a = load("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\xF0\x9F\x98\x80y\n");
    const auto b = load("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\xF0\x9F\x98\x81y\n");
    const ComparedDocument first{&a, {}, false}, second{&b, {}, false};
    EXPECT_EQ(compareSubtitles(first, second, 0, {}, TextUnits::Utf16).first[0].marks, (std::vector<int>{1, 2, 2}));
    EXPECT_EQ(compareSubtitles(first, second, 0, {}, TextUnits::CodePoints).first[0].marks,
              (std::vector<int>{1, 1, 1}));
    EXPECT_EQ(compareSubtitles(first, second, 0, {}).first[0],
              compareSubtitles(first, second, 0, {}, kLegacyTextUnits).first[0]);
}

// SubsGridWindow.cpp:510-525: a range is SubString(start, end) of the shown
// text, in the platform's units, here as UTF-16 offsets for drawing; a run
// starting past the text is empty and skipped, one running past it is cut.
TEST(CompareTexts, MarkedRunsInTheShownText)
{
    const std::u16string_view shown = u"a\U0001F600b\U0001F601";
    // Code points: a, U+1F600, b, U+1F601.
    EXPECT_EQ(markedRun(shown, 1, 1, TextUnits::CodePoints), (MarkedRun{1, 2}));
    EXPECT_EQ(markedRun(shown, 2, 3, TextUnits::CodePoints), (MarkedRun{3, 3}));
    EXPECT_EQ(markedRun(shown, 0, 9, TextUnits::CodePoints), (MarkedRun{0, 6}));
    EXPECT_EQ(markedRun(shown, 3, 3, TextUnits::CodePoints), (MarkedRun{4, 2}));
    EXPECT_FALSE(markedRun(shown, 4, 4, TextUnits::CodePoints));
    // UTF-16 units: the same offsets index the text directly.
    EXPECT_EQ(markedRun(shown, 1, 1, TextUnits::Utf16), (MarkedRun{1, 1}));
    EXPECT_EQ(markedRun(shown, 2, 3, TextUnits::Utf16), (MarkedRun{2, 2}));
    EXPECT_EQ(markedRun(shown, 4, 9, TextUnits::Utf16), (MarkedRun{4, 2}));
    EXPECT_FALSE(markedRun(shown, 6, 6, TextUnits::Utf16));
    EXPECT_FALSE(markedRun(u"", 0, 0, TextUnits::CodePoints));
    EXPECT_FALSE(markedRun(u"", 0, 0, TextUnits::Utf16));
}

// The port gives the transcription's tables on random texts over a small
// alphabet (so that runs of equal and differing characters both occur).
TEST(CompareTexts, AgreesWithTheLegacyTranscription)
{
    std::mt19937 random(193);
    std::uniform_int_distribution<int> length(0, 12), letter(0, 3);
    const char16_t alphabet[] = u"ab {";
    for (int n = 0; n < 4000; ++n) {
        std::u16string a, b;
        for (int i = length(random); i > 0; --i)
            a += alphabet[letter(random)];
        for (int i = length(random); i > 0; --i)
            b += alphabet[letter(random)];
        LineComparison x, y, lx, ly;
        compareTexts(x, y, a, b);
        legacyCompareTexts(lx, ly, a, b);
        ASSERT_EQ(x, lx) << n;
        ASSERT_EQ(y, ly) << n;
    }
    // The Linux build's form, with characters outside the BMP.
    const char32_t astral[] = U"a\U0001F600\U0001F601 ";
    for (int n = 0; n < 4000; ++n) {
        std::u32string a, b;
        for (int i = length(random); i > 0; --i)
            a += astral[letter(random)];
        for (int i = length(random); i > 0; --i)
            b += astral[letter(random)];
        LineComparison x, y, lx, ly;
        compareTexts(x, y, std::u32string_view(a), std::u32string_view(b));
        legacyCompareTexts(lx, ly, a, b);
        ASSERT_EQ(x, lx) << n;
        ASSERT_EQ(y, ly) << n;
    }
}

// SubsGridBase.cpp:1780-1781: the translation in translation mode when there
// is one, otherwise the text.
TEST(CompareTexts, TheTranslationIsComparedInTranslationMode)
{
    core::LineRecord line;
    line.text = u8"original";
    EXPECT_EQ(comparedText(line, true), u"original");
    line.translation = u8"translated";
    EXPECT_EQ(comparedText(line, true), u"translated");
    EXPECT_EQ(comparedText(line, false), u"original");
}

// SubsGridBase.cpp:1757-1790 with no criterion: Lines pair in order; the
// second's extra Line has no partner and keeps its usual colour.
TEST_F(ComparisonTest, WithoutCriteriaLinesPairInOrder)
{
    const auto r = compareSubtitles(first(), second(), 0, {});
    EXPECT_EQ(pairs(r), "0-0 1-1 2-2");
    EXPECT_EQ(states(r.first), "=x=");
    EXPECT_EQ(states(r.second), "=x=.");
    EXPECT_EQ(r.first[1].marks, (std::vector<int>{1, 1, 1})); // "two" / "too"
    EXPECT_EQ(r.second[1].marks, (std::vector<int>{1, 2, 2}));
    EXPECT_FALSE(r.second[3].matchedRow);
}

// SubsGridBase.cpp:1769: Start and End must both be equal (in milliseconds).
// A Line without a partner leaves the search where it was (lastJ).
TEST_F(ComparisonTest, ByTimes)
{
    const auto r = compareSubtitles(first(), second(), compare_by::Times, {});
    EXPECT_EQ(pairs(r), "0-0 2-2");
    EXPECT_EQ(states(r.first), "=.=");
    EXPECT_EQ(states(r.second), "=.=.");
}

// SubsGridBase.cpp:1771: the same style. "three" (Default) passes over the
// second's "three" (Sign) to "four".
TEST_F(ComparisonTest, ByStyles)
{
    const auto r = compareSubtitles(first(), second(), compare_by::Styles, {});
    EXPECT_EQ(pairs(r), "0-0 1-1 2-3");
    EXPECT_EQ(states(r.first), "=xx");
    EXPECT_EQ(states(r.second), "=x.x");
}

// SubsGridBase.cpp:1773: with chosen styles, only Lines of a chosen style
// pair, with a Line of the same style.
TEST_F(ComparisonTest, BySelectedStyles)
{
    const auto r = compareSubtitles(first(), second(), 0, {u8"Sign"});
    EXPECT_EQ(pairs(r), "1-1");
    EXPECT_EQ(states(r.first), ".x.");
    EXPECT_EQ(states(r.second), ".x..");
    // The list is read, not the ChosenStyles bit (SubsGridBase.cpp:1743).
    EXPECT_EQ(pairs(compareSubtitles(first(), second(), compare_by::ChosenStyles, {})), "0-0 1-1 2-2");
}

// SubsGridBase.cpp:1763-1767: hidden Lines of either Document are passed over.
TEST_F(ComparisonTest, ByVisibleLines)
{
    hide(a, 1);
    auto r = compareSubtitles(first(), second(), compare_by::Visible, {});
    EXPECT_EQ(pairs(r), "0-0 2-1");
    EXPECT_EQ(states(r.first), "=.x");
    hide(b, 1);
    r = compareSubtitles(first(), second(), compare_by::Visible, {});
    EXPECT_EQ(pairs(r), "0-0 2-2");
    EXPECT_EQ(states(r.second), "=.=.");
    // Without the criterion hidden Lines pair as any other.
    EXPECT_EQ(pairs(compareSubtitles(first(), second(), 0, {})), "0-0 1-1 2-2");
    // A revealed block (VISIBLE_BLOCK) is visible: the first's "two" now
    // pairs with the second's next visible Line, and "three" with "four".
    a.editLine(row(a, 1), [](core::LineRecord &l) { l.visibility = core::LineVisibility::VisibleBlock; });
    EXPECT_EQ(pairs(compareSubtitles(first(), second(), compare_by::Visible, {})), "0-0 1-2 2-3");
}

// SubsGridBase.cpp:1775: both Lines selected.
TEST_F(ComparisonTest, BySelections)
{
    selectedA = {row(a, 1), row(a, 2)};
    selectedB = {row(b, 0), row(b, 2)};
    const auto r = compareSubtitles(first(), second(), compare_by::Selections, {});
    EXPECT_EQ(pairs(r), "1-0 2-2");
    EXPECT_EQ(states(r.first), ".x=");
    EXPECT_EQ(states(r.second), "x.=.");
}

// The criteria combine: every one must pass.
TEST_F(ComparisonTest, CriteriaCombine)
{
    EXPECT_EQ(pairs(compareSubtitles(first(), second(), compare_by::Times | compare_by::Styles, {})), "0-0");
    selectedA = {row(a, 0), row(a, 2)};
    selectedB = {row(b, 0), row(b, 1), row(b, 2), row(b, 3)};
    EXPECT_EQ(pairs(compareSubtitles(first(), second(), compare_by::Selections | compare_by::Times, {})), "0-0 2-2");
    EXPECT_EQ(pairs(compareSubtitles(first(), second(), compare_by::Selections | compare_by::Styles, {})),
              "0-0 2-3");
    hide(b, 3);
    EXPECT_EQ(pairs(compareSubtitles(first(), second(),
                                     compare_by::Selections | compare_by::Styles | compare_by::Visible, {})),
              "0-0");
    EXPECT_EQ(pairs(compareSubtitles(first(), second(), compare_by::Times, {u8"Default", u8"Sign"})), "0-0");
}

// Translation mode on either side compares that side's translations.
TEST_F(ComparisonTest, TranslationModeComparesTheTranslations)
{
    a.editLine(row(a, 1), [](core::LineRecord &l) { l.translation = u8"too"; });
    auto withTl = first();
    withTl.translationMode = true;
    const auto r = compareSubtitles(withTl, second(), 0, {});
    EXPECT_EQ(states(r.first), "===");
    // Off, the text is compared even with a translation.
    EXPECT_EQ(states(compareSubtitles(first(), second(), 0, {}).first), "=x=");
}

// Comparing never changes either Document.
TEST_F(ComparisonTest, TheDocumentsAreNotChanged)
{
    const auto before = b.lines().size();
    const auto text = b.lines()[1]->text;
    (void)compareSubtitles(first(), second(), compare_by::Times | compare_by::Visible, {u8"Sign"});
    EXPECT_EQ(b.lines().size(), before);
    EXPECT_EQ(b.lines()[1]->text, text);
    EXPECT_FALSE(b.lines()[1]->edited);
}

// SubsGrid::GetCommonStyles: the first's Styles, in its order, that the
// second has too.
TEST_F(ComparisonTest, CommonStyles)
{
    EXPECT_EQ(commonStyles(a, b), (std::vector<std::u8string>{u8"Default", u8"Sign"}));
    EXPECT_EQ(commonStyles(b, a), (std::vector<std::u8string>{u8"Sign", u8"Default"}));
}

// Notebook.cpp:922-928 and 97-123: the menu shows the shared styles checked
// as SUBS_COMPARISON_STYLES has them and adds those to compareStyles each
// time it opens; legacy never clears the list.
TEST(ComparisonMenu, OpeningTheMenuAddsTheCheckedStylesAgain)
{
    SubtitleComparison c;
    const std::vector<std::u8string> common{u8"Default", u8"Sign"};
    auto items = c.openMenu(common, {u8"Sign", u8"Missing"});
    ASSERT_EQ(items.size(), 2u);
    EXPECT_FALSE(items[0].checked);
    EXPECT_TRUE(items[1].checked);
    EXPECT_TRUE(c.chosenStylesShown());
    EXPECT_EQ(c.chosenStyles(), (std::vector<std::u8string>{u8"Sign"}));
    // Unchecking in the same menu removes it; the bit follows the list.
    int type = compare_by::ChosenStyles;
    type = c.toggleStyle(type, u8"Sign", false);
    EXPECT_TRUE(c.chosenStyles().empty());
    EXPECT_EQ(type, 0);
    type = c.toggleStyle(type, u8"Default", true);
    EXPECT_EQ(c.chosenStyles(), (std::vector<std::u8string>{u8"Default"}));
    EXPECT_EQ(type, compare_by::ChosenStyles);

    // Opened twice, the style is in the list twice; unchecking removes one,
    // so it stays chosen (and is written back to SUBS_COMPARISON_STYLES).
    SubtitleComparison twice;
    twice.openMenu(common, {u8"Sign"});
    twice.openMenu(common, {u8"Sign"});
    EXPECT_EQ(twice.chosenStyles(), (std::vector<std::u8string>{u8"Sign", u8"Sign"}));
    type = twice.toggleStyle(compare_by::ChosenStyles | compare_by::Times, u8"Sign", false);
    EXPECT_EQ(twice.chosenStyles(), (std::vector<std::u8string>{u8"Sign"}));
    EXPECT_EQ(type, compare_by::ChosenStyles | compare_by::Times);

    // A style from another pair of tabs stays in the list (never cleared).
    SubtitleComparison kept;
    kept.openMenu(common, {u8"Sign"});
    kept.openMenu({u8"Other"}, {u8"Sign"});
    EXPECT_EQ(kept.chosenStyles(), (std::vector<std::u8string>{u8"Sign"}));
}

// Notebook.cpp:98-100: a set bit with an empty list is cleared first, so a
// checked item sets it again; MENU_COMPARE + 1..4 flip their bit.
TEST(ComparisonMenu, BitsFlip)
{
    SubtitleComparison c;
    EXPECT_EQ(c.toggleStyle(compare_by::ChosenStyles, u8"X", false), 0);
    EXPECT_EQ(c.toggleStyle(compare_by::ChosenStyles, u8"X", true), compare_by::ChosenStyles);
    EXPECT_EQ(SubtitleComparison::toggleBit(compare_by::Times, compare_by::Times), 0);
    EXPECT_EQ(SubtitleComparison::toggleBit(compare_by::Times, compare_by::Visible),
              compare_by::Times | compare_by::Visible);
}

// Notebook.cpp:947-956, RemoveComparison and Clearing: the tables of each
// grid, kept per Document.
TEST_F(ComparisonTest, TablesFollowCompareRemoveAndReplace)
{
    SubtitleComparison c;
    const DocumentId one{1}, two{2}, three{3}, four{4};
    EXPECT_FALSE(c.active());
    c.compare(one, two, first(), second(), 0);
    EXPECT_TRUE(c.active());
    ASSERT_TRUE(c.table(one));
    ASSERT_TRUE(c.table(two));
    EXPECT_EQ(states(*c.table(one)), "=x=");
    EXPECT_EQ(states(*c.table(two)), "=x=.");

    // R1-stale-table: a new pair drops the earlier pair's tables (legacy
    // Notebook.cpp:948-951 left them on their grids, never compared again),
    // and Turn off leaves no table.
    c.compare(one, three, first(), second(), compare_by::Times);
    EXPECT_EQ(c.first(), one);
    EXPECT_TRUE(c.table(one));
    EXPECT_FALSE(c.table(two));
    c.compare(three, four, first(), second(), compare_by::Times);
    EXPECT_FALSE(c.table(one));
    EXPECT_TRUE(c.table(three));
    c.remove();
    EXPECT_FALSE(c.active());
    EXPECT_TRUE(c.tabled().empty());
    EXPECT_FALSE(c.first());
    // Again: nothing.
    c.remove();
    EXPECT_TRUE(c.tabled().empty());
    c.compare(one, two, first(), second(), 0);
    c.forget(one);
    EXPECT_FALSE(c.table(one));
    c.remove();

    // Clearing a compared tab (other subtitles loaded into it) removes its
    // table; the pair now names the replacement and comparing again fills it.
    c.compare(one, two, first(), second(), 0);
    c.replaced(one, three);
    EXPECT_FALSE(c.table(one));
    EXPECT_FALSE(c.table(three));
    EXPECT_TRUE(c.table(two));
    EXPECT_EQ(c.first(), three);
    EXPECT_TRUE(c.active());
    c.recompare(first(), second(), 0);
    EXPECT_TRUE(c.table(three));
}

// ---- the legacy probe's observations -----------------------------------------

namespace {

// inputs/comparison-cases.txt (the format is comparison_capture.cpp's).
struct CaseLine {
    int start = 0, end = 0, visibility = 1;
    bool selected = false;
    std::string style, text, translation;
};
struct CaseRun {
    int type = 0;
    std::vector<std::u8string> styles;
    bool tl1 = false, tl2 = false;
};
struct Case {
    std::string name;
    std::vector<CaseLine> first, second;
    std::vector<CaseRun> runs;
};

std::vector<std::string> fields(const std::string &line, char separator)
{
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto at = line.find(separator, from);
        out.push_back(line.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos)
            return out;
        from = at + 1;
    }
}

std::u8string u8(const std::string &s)
{
    return {s.begin(), s.end()};
}

std::vector<Case> readCases(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<Case> cases;
    std::string line;
    bool tl1 = false, tl2 = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        auto f = fields(line, '\t');
        if (f.size() == 1 && line.find(' ') != std::string::npos)
            f = {line.substr(0, line.find(' ')), line.substr(line.find(' ') + 1)};
        if (f[0] == "case") {
            cases.push_back({f.at(1), {}, {}, {}});
            tl1 = tl2 = false;
        } else if (f[0] == "first" || f[0] == "second") {
            CaseLine l{std::stoi(f.at(1)), std::stoi(f.at(2)), std::stoi(f.at(4)), f.at(5) == "1", f.at(3), f.at(6),
                       f.size() > 7 ? f[7] : std::string()};
            (f[0] == "first" ? cases.back().first : cases.back().second).push_back(l);
        } else if (f[0] == "tl") {
            tl1 = f.at(1) == "1";
            tl2 = f.at(2) == "1";
        } else if (f[0] == "run") {
            CaseRun r{std::stoi(f.at(1)), {}, tl1, tl2};
            if (f.size() > 2 && !f[2].empty())
                for (const auto &style : fields(f[2], ','))
                    r.styles.push_back(u8(style));
            cases.back().runs.push_back(r);
        }
    }
    return cases;
}

std::string assTime(int ms)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%d:%02d:%02d.%02d", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60,
                  ms % 1000 / 10);
    return buffer;
}

// A Document with the case's Lines: times through the ASS loader (the cases
// use whole centiseconds), the other fields set as the probe sets them on
// legacy's Dialogue.
core::Document caseDocument(const std::vector<CaseLine> &lines, std::set<core::LineId> &selected)
{
    std::string ass = "[Events]\n";
    for (const auto &l : lines)
        ass += "Dialogue: 0," + assTime(l.start) + "," + assTime(l.end) + ",Default,,0,0,0,,x\n";
    core::Document d = load(ass);
    EXPECT_EQ(d.lines().size(), lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const CaseLine &l = lines[i];
        const auto id = d.lines()[i]->id;
        d.editLine(id, [&](core::LineRecord &r) {
            r.style = u8(l.style);
            r.text = u8(l.text);
            r.translation = u8(l.translation);
            r.visibility = l.visibility == 0   ? core::LineVisibility::Hidden
                           : l.visibility == 2 ? core::LineVisibility::VisibleBlock
                                               : core::LineVisibility::Visible;
        });
        if (l.selected)
            selected.insert(id);
    }
    return d;
}

// A table as the probe writes it: [secondComparedLine, differences, lineCompare...].
QJsonArray observedForm(const std::vector<LineComparison> &rows)
{
    QJsonArray out;
    for (const auto &row : rows) {
        QJsonArray r{row.matchedRow ? static_cast<int>(*row.matchedRow) : -1, row.differences ? 1 : 0};
        for (const int mark : row.marks)
            r.append(mark);
        out.append(r);
    }
    return out;
}

std::string compact(const QJsonValue &value)
{
    return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

// Every run of the probe (SubsComparison and CompareTexts compiled from the
// legacy source unchanged) replayed through compareSubtitles: the pairs, the
// match state and the ranges of both tables are legacy's. Each run is held to
// both legacy builds' tables (R5-per-platform): counted in UTF-16 units it
// gives the "windows" tables (wxMSW's wxString), counted in code points the
// "linux" ones (wxGTK's); the default on each platform is that build's.
TEST(LegacyComparisonCapture, ReplaysTheLegacyObservations)
{
    const auto cases = readCases(HIKARI_COMPARISON_CASES);
    ASSERT_GE(cases.size(), 14u);
    QFile file(QStringLiteral(HIKARI_COMPARISON_OBSERVATIONS));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    std::map<std::string, QJsonArray> observed;
    for (const QByteArray &line : file.readAll().split('\n'))
        if (!line.trimmed().isEmpty()) {
            const auto object = QJsonDocument::fromJson(line).object();
            observed[object["case"].toString().toStdString()] = object["runs"].toArray();
        }
    ASSERT_EQ(observed.size(), cases.size());

    int runs = 0, rows = 0;
    std::set<std::string> widthsDiffer;
    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        const QJsonArray &legacyRuns = observed.at(c.name);
        ASSERT_EQ(legacyRuns.size(), qsizetype(c.runs.size()));
        std::set<core::LineId> selected1, selected2;
        const core::Document a = caseDocument(c.first, selected1);
        const core::Document b = caseDocument(c.second, selected2);
        for (std::size_t n = 0; n < c.runs.size(); ++n) {
            const CaseRun &run = c.runs[n];
            const QJsonObject legacy = legacyRuns[qsizetype(n)].toObject();
            SCOPED_TRACE("run " + std::to_string(n) + " type " + std::to_string(run.type));
            ASSERT_EQ(legacy["type"].toInt(), run.type);
            ASSERT_EQ(legacy["styles"].toArray().size(), qsizetype(run.styles.size()));
            const QJsonObject onLinux = legacy["linux"].toObject();
            const QJsonObject onWindows =
                legacy["windows"].isString() ? onLinux : legacy["windows"].toObject();
            if (!legacy["windows"].isString())
                widthsDiffer.insert(c.name);

            const ComparedDocument first{&a, selected1, run.tl1}, second{&b, selected2, run.tl2};
            for (const auto &[units, build] :
                 {std::pair{TextUnits::Utf16, onWindows}, std::pair{TextUnits::CodePoints, onLinux}}) {
                SCOPED_TRACE(units == TextUnits::Utf16 ? "windows" : "linux");
                // Comparing changed neither file, and a second SubsComparison
                // (the tables cleared and refilled) gave the same tables.
                EXPECT_TRUE(build["unchanged"].toBool());
                EXPECT_TRUE(build["repeatable"].toBool());
                const auto result = compareSubtitles(first, second, run.type, run.styles, units);
                EXPECT_EQ(compact(observedForm(result.first)), compact(build["first"]));
                EXPECT_EQ(compact(observedForm(result.second)), compact(build["second"]));
                EXPECT_EQ(compareSubtitles(first, second, run.type, run.styles, units).first, result.first);
                if (units == kLegacyTextUnits)
                    EXPECT_EQ(compareSubtitles(first, second, run.type, run.styles).first, result.first);
                // The Grid's colour per row (SubsGridWindow.cpp:419-426): the
                // mismatch background when the legacy array is not empty, the
                // match background when differences is false.
                for (const auto &[rowsOf, table] : {std::pair{&result.first, build["first"].toArray()},
                                                    std::pair{&result.second, build["second"].toArray()}}) {
                    ASSERT_EQ(qsizetype(rowsOf->size()), table.size());
                    for (std::size_t i = 0; i < rowsOf->size(); ++i) {
                        const QJsonArray legacyRow = table[qsizetype(i)].toArray();
                        EXPECT_EQ((*rowsOf)[i].mismatch(), legacyRow.size() > 2);
                        EXPECT_EQ((*rowsOf)[i].match(), legacyRow[1].toInt() == 0);
                    }
                }
                ++runs;
                rows += static_cast<int>(result.first.size() + result.second.size());
            }
        }
        for (std::size_t i = 0; i < c.first.size(); ++i)
            EXPECT_EQ(a.lines()[i]->text, u8(c.first[i].text));
        for (std::size_t i = 0; i < c.second.size(); ++i)
            EXPECT_EQ(b.lines()[i]->text, u8(c.second[i].text));
    }
    EXPECT_EQ(runs, 2 * 207);
    EXPECT_EQ(rows, 2 * 4239);
    // Only the cases with characters outside the BMP count differently on
    // the two legacy builds.
    EXPECT_EQ(widthsDiffer, (std::set<std::string>{"rand-astral", "texts"}));
}
