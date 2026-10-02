// C4-ssa: Style values as the legacy Styles::parseStyle reads ASS and SSA v4.

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"

#include <QFile>
#include <gtest/gtest.h>

#include <cstring>

using namespace hikari::core;

namespace {

std::vector<std::byte> readFile(const QString &path)
{
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    const QByteArray d = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(d.size()));
    std::memcpy(out.data(), d.constData(), out.size());
    return out;
}

std::string text(const std::vector<std::byte> &b)
{
    std::string s(reinterpret_cast<const char *>(b.data()), b.size());
    std::erase(s, '\r'); // the legacy save writes CRLF; the inputs use LF
    return s;
}

std::vector<std::byte> input(const char *name)
{
    return readFile(QStringLiteral(HIKARI_CAPTURE_INPUTS "/") + QLatin1String(name));
}

std::string legacyOutput(const char *name)
{
    return text(readFile(QStringLiteral(HIKARI_C4SSA_OUTPUTS "/") + QLatin1String(name)));
}

// The Dialogue/Comment lines of a saved file, in order.
std::vector<std::string> eventLines(const std::string &file)
{
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < file.size()) {
        const std::size_t end = std::min(file.find('\n', pos), file.size());
        const std::string line = file.substr(pos, end - pos);
        if (line.starts_with("Dialogue: ") || line.starts_with("Comment: "))
            out.push_back(line);
        pos = end + 1;
    }
    return out;
}

void editEveryLine(Document &doc)
{
    for (const auto *line : doc.lines())
        ASSERT_TRUE(doc.setLineText(line->id, line->text));
}

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

} // namespace

TEST(Style, AssLayout)
{
    const auto v = legacy::decodeStyle(
        u8"Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,-1,0,0,0,100,100,0,0,3,2,0,2,10,10,10,1",
        false);
    EXPECT_TRUE(v.complete);
    EXPECT_EQ(v.name, u8"Default");
    EXPECT_EQ(v.primary, (Colour{255, 255, 255, 0}));
    EXPECT_EQ(v.secondary, (Colour{255, 0, 0, 0}));
    EXPECT_EQ(v.back, (Colour{0, 0, 0, 0x80}));
    EXPECT_TRUE(v.bold);            // any token but "0"
    EXPECT_TRUE(v.borderStyle);     // only "3"
    EXPECT_EQ(v.alignment, u8"2");
    EXPECT_FALSE(v.ssaTertiaryColour);
}

TEST(Style, SsaV4CaptureInput)
{
    // tools/legacy-capture/inputs/ssa-v4.ssa: decimal and hex colours, SSA
    // alignment numbering, Tertiary colour and AlphaLevel with no ASS field.
    const auto doc = loadAss(readFile(QStringLiteral(HIKARI_CAPTURE_INPUTS "/ssa-v4.ssa"))).document;
    const auto styles = decodeStyles(doc);
    ASSERT_EQ(styles.size(), 3u);
    const auto &d = styles[0];
    EXPECT_TRUE(d.complete);
    EXPECT_EQ(d.primary, (Colour{255, 255, 255, 0}));   // 16777215
    EXPECT_EQ(d.secondary, (Colour{255, 255, 0, 0}));   // 65535
    EXPECT_EQ(d.outline, (Colour{255, 0, 0, 0}));       // SSA BackColour 255
    EXPECT_EQ(d.back, d.outline);
    EXPECT_EQ(d.ssaTertiaryColour, u8"65280");
    EXPECT_EQ(d.ssaAlphaLevel, u8"0");
    EXPECT_TRUE(d.bold);
    EXPECT_FALSE(d.italic);
    EXPECT_FALSE(d.underline);
    EXPECT_EQ(d.scaleX, u8"100");
    EXPECT_EQ(d.marginLeft, u8"10");
    EXPECT_EQ(d.marginRight, u8"20");
    EXPECT_EQ(d.marginVertical, u8"30");
    EXPECT_EQ(d.encoding, u8"0");
    EXPECT_EQ(styles[1].alignment, u8"8"); // SSA 6, top centre
    EXPECT_TRUE(styles[1].borderStyle);
    EXPECT_EQ(styles[1].back, (Colour{0x80, 0, 0, 0}));
    EXPECT_EQ(styles[2].alignment, u8"4"); // SSA 9, middle left
}

TEST(Style, SsaLayoutStaysOnForLaterSections)
{
    // The legacy loader never switches back once it has seen "[V4 Styles]".
    const auto doc = loadAss(bytesOf("[V4 Styles]\nStyle: A,Arial,20,1,2,3,4,0,0,1,2,2,6,1,2,3,0,0\n"
                                     "[V4+ Styles]\nStyle: B,Arial,20,1,2,3,4,0,0,1,2,2,6,1,2,3,0,0\n"))
                         .document;
    const auto styles = decodeStyles(doc);
    ASSERT_EQ(styles.size(), 2u);
    EXPECT_TRUE(styles[1].complete);
    EXPECT_EQ(styles[1].alignment, u8"8");
}

TEST(Style, ShortLinesAndNames)
{
    const auto shortLine = legacy::decodeStyle(u8"Style: S,Arial,20,&H00FFFFFF&", false);
    EXPECT_FALSE(shortLine.complete);
    EXPECT_EQ(shortLine.fontname, u8"Arial");
    EXPECT_EQ(legacy::decodeStyle(u8"Style:NoSpace,Arial", false).name, u8"");     // AfterFirst(' ')
    EXPECT_EQ(legacy::decodeStyle(u8"Style:  Two", false).name, u8" Two");
    // A trailing comma adds no empty token, so the line is one field short.
    EXPECT_FALSE(legacy::decodeStyle(
                     u8"Style: T,Arial,20,1,2,3,4,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,", false)
                     .complete);
}

TEST(Style, LegacyColourParsing)
{
    EXPECT_EQ(legacy::colour(u8"&H0000FF&"), (Colour{255, 0, 0, 0}));
    EXPECT_EQ(legacy::colour(u8"&h40ff0000"), (Colour{0, 0, 255, 0x40}));
    EXPECT_EQ(legacy::colour(u8"#FF8000"), (Colour{255, 0x80, 0, 0})); // HTML order
    EXPECT_EQ(legacy::colour(u8"-"), (Colour{}));                      // IsNumber, ToLong fails
}

// Legacy capture run 37014681128 (c4-ssa-edge-families): with every Line
// regenerated, the rewrite writes what the old app saved.

TEST(LegacyCapture, EventEdgeFamilies)
{
    auto doc = loadAss(input("ass-event-edges.ass")).document;
    editEveryLine(doc);
    // The legacy save moves the Script Info Dialogue into Events; the rewrite
    // keeps it in place, so compare the Lines in order.
    ASSERT_EQ(eventLines(legacyOutput("saved-ass-event-edges.ass")).size(), 12u);
    EXPECT_EQ(eventLines(text(encodeAss(doc))), eventLines(legacyOutput("saved-ass-event-edges.ass")));
}

TEST(LegacyCapture, TLModeEdgeFamilies)
{
    auto doc = loadAss(input("tlmode-edges.ass")).document;
    EXPECT_FALSE(doc.lines()[0]->unconfirmed); // form-feed+D is not read back
    editEveryLine(doc);
    const auto events = [](const std::string &s) { return s.substr(s.rfind("Format: Layer")); };
    ASSERT_EQ(eventLines(legacyOutput("saved-tlmode-edges.ass")).size(), 6u);
    EXPECT_EQ(events(text(encodeAss(doc))), events(legacyOutput("saved-tlmode-edges.ass")));
}

TEST(LegacyCapture, SsaStylesDecodeAsTheLegacyConversion)
{
    auto doc = loadAss(input("ssa-v4.ssa")).document;
    const auto ours = decodeStyles(doc);
    const std::string legacy = legacyOutput("saved-ssa-v4.ssa.ass");
    const auto saved = decodeStyles(loadAss(bytesOf(legacy)).document);
    ASSERT_EQ(ours.size(), 3u);
    ASSERT_EQ(ours.size(), saved.size());
    for (std::size_t i = 0; i < ours.size(); ++i) {
        StyleValues a = ours[i];
        a.ssaTertiaryColour.reset(); // no ASS field: dropped by the conversion
        a.ssaAlphaLevel.reset();
        StyleValues b = saved[i];
        // Booleans are written as -1/0 and read back as "not 0".
        EXPECT_EQ(a.name, b.name);
        EXPECT_EQ(a.primary, b.primary);
        EXPECT_EQ(a.secondary, b.secondary);
        EXPECT_EQ(a.outline, b.outline);
        EXPECT_EQ(a.back, b.back);
        EXPECT_EQ(a.bold, b.bold);
        EXPECT_EQ(a.italic, b.italic);
        EXPECT_EQ(a.borderStyle, b.borderStyle);
        EXPECT_EQ(a.alignment, b.alignment) << i;
        EXPECT_EQ(a.marginLeft, b.marginLeft);
        EXPECT_EQ(a.marginVertical, b.marginVertical);
        EXPECT_EQ(a.encoding, b.encoding);
        EXPECT_TRUE(b.complete);
    }
    editEveryLine(doc);
    ASSERT_EQ(eventLines(legacy).size(), 2u);
    EXPECT_EQ(eventLines(text(encodeAss(doc))), eventLines(legacy));
}
