#include "hikari/application/karaoke_layout.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::karaoke_layout;

namespace {

class CodePointMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        ++calls;
        if (fail)
            return std::nullopt;
        double points = 0;
        for (const char c : text)
            points += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
        return TextExtents{10 * points, std::stod(style.at(2)), 0, 0};
    }
    int calls = 0;
    bool fail = false;
};

constexpr std::string_view kScript =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n\n[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\k20} ka {\\k30}ra{\\k50}น้ำ\n"
    "Dialogue: 0,0:00:04.00,0:00:06.00,Default,,0,0,0,,{\\k20} ka {\\k30}ra{\\k50}น้ำ\n";

core::Document document()
{
    std::vector<std::byte> bytes(kScript.size());
    std::memcpy(bytes.data(), kScript.data(), kScript.size());
    return core::loadAss(bytes).document;
}

struct Laid {
    CodePointMeasure port;
    ExtentsCache extents{port};
    core::Document doc = document();
    Layout layout{doc, extents, std::nullopt};
    std::vector<std::string> problems;

    karaoke_layout::Line line(std::size_t at, LineOptions options = {})
    {
        return layout.line(macroLineOf(*doc.lines()[at]), 10, options, problems);
    }
};

std::vector<std::string> texts(const std::vector<Unit> &units)
{
    std::vector<std::string> out;
    for (const Unit &u : units)
        out.push_back(u.text);
    return out;
}

}

TEST(KaraokeLayout, SyllablesSitWhereKaraskelPutsThem)
{
    Laid laid;
    const auto l = laid.line(0);
    ASSERT_EQ(l.syls.size(), 4u);
    EXPECT_EQ(l.syln, 3);
    EXPECT_EQ(l.line.left, 275);
    EXPECT_EQ(l.line.width, 90);
    EXPECT_EQ(l.line.top, 20);
    const Unit &ka = l.syls[1];
    EXPECT_EQ(ka.text, " ka ");
    EXPECT_EQ(ka.spaceStripped, "ka");
    EXPECT_EQ(ka.prespace, " ");
    EXPECT_EQ(ka.postspace, " ");
    EXPECT_EQ(ka.prespaceWidth, 10);
    EXPECT_EQ(ka.postspaceWidth, 10);
    EXPECT_EQ(ka.left, 285);
    EXPECT_EQ(ka.center, 295);
    EXPECT_EQ(ka.right, 305);
    EXPECT_EQ(ka.start, 0);
    EXPECT_EQ(ka.end, 200);
    EXPECT_EQ(l.syls[2].left, 315);
    EXPECT_EQ(l.syls[3].left, 335);
    EXPECT_TRUE(laid.problems.empty());
}

TEST(KaraokeLayout, CharsAreGraphemeClustersUnlessCodePointsAreAskedFor)
{
    Laid laid;
    const auto clusters = laid.line(0, {.chars = true});
    ASSERT_EQ(clusters.chars.size(), 4u);
    EXPECT_EQ(texts(clusters.chars[3]), (std::vector<std::string>{"น้ำ"}));
    EXPECT_EQ(clusters.chars[3][0].width, 30);
    EXPECT_EQ(texts(clusters.chars[1]), (std::vector<std::string>{" ", "k", "a", " "}));

    const auto points = laid.line(0, {.chars = true, .split = CharSplit::CodePoints});
    EXPECT_EQ(texts(points.chars[3]), (std::vector<std::string>{"น", "้", "ำ"}));
    EXPECT_EQ(points.chars[3][2].left, 355);
    EXPECT_NE(points.signature, clusters.signature);
    EXPECT_TRUE(laid.line(0).chars.empty());
}

TEST(KaraokeLayout, TheMacroSnapshotLaysOutAsTheDocumentDoes)
{
    Laid laid;
    const MacroSnapshot snapshot = macroSnapshotOf(laid.doc, Selection{});
    CodePointMeasure port;
    ExtentsCache extents(port);
    Layout fromSnapshot(snapshot.info, snapshot.styles, extents, std::nullopt);
    std::vector<std::string> problems;
    const auto a = laid.line(0, {.chars = true});
    const auto b = fromSnapshot.line(snapshot.dialogues[0], 10, {.chars = true}, problems);
    EXPECT_EQ(a.signature, b.signature);
    EXPECT_EQ(a.line.left, b.line.left);
    EXPECT_EQ(a.syls[3].center, b.syls[3].center);
    EXPECT_EQ(fromSnapshot.furiganaStylesAdded(), laid.layout.furiganaStylesAdded());
    EXPECT_EQ(fromSnapshot.addedStyle(u8"Default-furigana"), laid.layout.addedStyle(u8"Default-furigana"));
}

TEST(KaraokeLayout, EachStyleAndTextIsMeasuredOncePerRun)
{
    Laid laid;
    laid.line(0, {.chars = true});
    const int first = laid.port.calls;
    EXPECT_EQ(static_cast<std::size_t>(first), laid.extents.measured());
    laid.line(1, {.chars = true});
    EXPECT_EQ(laid.port.calls, first);
}

TEST(KaraokeLayout, TextThatCannotBeMeasuredIsAProblemOnce)
{
    Laid laid;
    laid.port.fail = true;
    laid.line(0);
    ASSERT_FALSE(laid.problems.empty());
    EXPECT_EQ(laid.problems.front(), "could not measure text in the Style \"Default\"");
    laid.problems.clear();
    const auto again = laid.line(1);
    EXPECT_TRUE(laid.problems.empty());
    EXPECT_EQ(again.syls[1].width, 0);
}
