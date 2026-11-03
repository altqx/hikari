#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/export.h"
#include "hikari/core/subtitle_load.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

using namespace hikari::core;

namespace {

const std::filesystem::path kFixtures = HIKARI_EXPORT_FIXTURES;

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return b;
}

std::string text(const std::vector<std::byte> &b)
{
    return {reinterpret_cast<const char *>(b.data()), b.size()};
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

Document fixture(std::string_view name)
{
    return loadAss(bytes(readFile(kFixtures / (std::string(name) + ".ass")))).document;
}

Document ass(std::string_view events, std::string_view header = "PlayResX: 1920\nPlayResY: 1080\n")
{
    const std::string file =
        "[Script Info]\nScriptType: v4.00+\n" + std::string(header) +
        "\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
        "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
        "MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,72,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,60,60,54,1\n"
        "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
        std::string(events);
    return loadAss(bytes(file)).document;
}

std::string exported(const Document &d, ExportFormat f, const ExportOptions &o = {})
{
    return text(exportDocument(d, f, o).bytes);
}

struct Golden {
    ExportFormat format;
    std::string_view extension;
};

constexpr Golden kGoldens[] = {
    {ExportFormat::Srt, "srt"},       {ExportFormat::WebVtt, "vtt"}, {ExportFormat::Sbv, "sbv"},
    {ExportFormat::PlainText, "txt"}, {ExportFormat::MicroDvd, "sub"}, {ExportFormat::Mpl2, "mpl2.txt"},
    {ExportFormat::TMPlayer, "tmp.txt"},
};

std::uint32_t lossesOf(const ExportResult &r, LineId id)
{
    for (const auto &n : r.notes)
        if (n.line == id)
            return n.losses;
    return 0;
}

constexpr std::uint32_t bit(ExportLoss l)
{
    return static_cast<std::uint32_t>(l);
}

}

// HIKARI_WRITE_EXPORT_GOLDENS=1 rewrites tests/fixtures/export/golden.
TEST(Export, FixturesMatchTheirGoldens)
{
    const bool write = std::getenv("HIKARI_WRITE_EXPORT_GOLDENS") != nullptr;
    for (const auto name : {"plain", "karaoke", "typesetting"}) {
        const auto document = fixture(name);
        for (const auto &g : kGoldens) {
            const auto path = kFixtures / "golden" / (std::string(name) + "." + std::string(g.extension));
            const auto out = exported(document, g.format);
            if (write)
                std::ofstream(path, std::ios::binary) << out;
            EXPECT_EQ(out, readFile(path)) << path;
        }
    }
}

TEST(Export, TheDocumentStaysAsItWas)
{
    const auto document = fixture("typesetting");
    const auto before = text(encodeAss(document));
    for (const auto &g : kGoldens)
        exportDocument(document, g.format, {});
    EXPECT_EQ(text(encodeAss(document)), before);
}

TEST(Export, SrtReadsBackWithTheSameTimesAndText)
{
    const auto document = fixture("plain");
    const auto srt = exportDocument(document, ExportFormat::Srt, {}).bytes;
    const auto back = loadSubtitle(srt, u8"srt");
    ASSERT_TRUE(back);
    const auto lines = back->document.lines();
    ASSERT_EQ(lines.size(), 6u);
    EXPECT_EQ(lines[0]->start.value.microseconds(), 1'000'000);
    EXPECT_EQ(lines[0]->end.value.microseconds(), 3'500'000);
    EXPECT_EQ(lines[1]->text, u8"You're late <i>again</i>.\\NWe start at eight.");
    EXPECT_EQ(lines[5]->end.value.microseconds(), 17'250'000);
}

TEST(Export, FrameAndLineFormatsReadBack)
{
    const auto document = fixture("plain");
    ExportOptions o;
    o.fps = 25;
    for (const auto format : {ExportFormat::MicroDvd, ExportFormat::Mpl2, ExportFormat::TMPlayer}) {
        const auto back = loadSubtitle(exportDocument(document, format, o).bytes, u8"txt");
        ASSERT_TRUE(back) << static_cast<int>(format);
        const auto lines = back->document.lines();
        ASSERT_FALSE(lines.empty());
        EXPECT_EQ(back->document.format(), format == ExportFormat::MicroDvd ? SubtitleFormat::MicroDvd
                                           : format == ExportFormat::Mpl2   ? SubtitleFormat::Mpl2
                                                                            : SubtitleFormat::TMPlayer);
        EXPECT_NE(std::u8string(lines[0]->text).find(u8"Good morning."), std::u8string::npos);
    }
}

TEST(Export, VttAndSbvCuesReadBack)
{
    const auto document = fixture("plain");
    std::istringstream sbv(exported(document, ExportFormat::Sbv));
    std::string line;
    std::getline(sbv, line);
    EXPECT_EQ(line, "0:00:01.000,0:00:03.500\r");
    std::getline(sbv, line);
    EXPECT_EQ(line, "Good morning.\r");

    const auto vtt = exported(document, ExportFormat::WebVtt);
    EXPECT_TRUE(vtt.starts_with("WEBVTT\n"));
    EXPECT_NE(vtt.find("\n00:00:08.500 --> 00:00:10.000\n<c.Default><b>Hurry</b> &amp; bring the &lt;notes&gt;!</c>\n"),
              std::string::npos);
}

TEST(Export, CommentsAndEmptyLinesAreSkippedAndCounted)
{
    const auto r = exportDocument(fixture("plain"), ExportFormat::Srt, {});
    EXPECT_EQ(r.commentsSkipped, 1);
    EXPECT_EQ(r.emptySkipped, 1);
    EXPECT_EQ(r.linesWritten, 6);
}

TEST(Export, OnlyTheChosenLines)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,One\n"
                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Two\n");
    ExportOptions o;
    o.lines = std::set{d.lines()[1]->id};
    EXPECT_EQ(exported(d, ExportFormat::Srt, o), "1\r\n00:00:03,000 --> 00:00:04,000\r\nTwo\r\n\r\n");
}

TEST(Export, SrtTagsNestAndColoursAreOptional)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\b1}a{\\i1}b{\\b0}c{\\c&H0000FF&}d\n");
    EXPECT_EQ(exported(d, ExportFormat::Srt), "1\r\n00:00:01,000 --> 00:00:02,000\r\n<b>a<i>b</i></b><i>cd</i>\r\n\r\n");
    ExportOptions o;
    o.srtFontColour = true;
    EXPECT_EQ(exported(d, ExportFormat::Srt, o),
              "1\r\n00:00:01,000 --> 00:00:02,000\r\n<b>a<i>b</i></b><i>c<font color=\"#FF0000\">d</font></i>\r\n\r\n");
    EXPECT_TRUE(lossesOf(exportDocument(d, ExportFormat::Srt, {}), d.lines()[0]->id) & bit(ExportLoss::Colour));
    EXPECT_FALSE(lossesOf(exportDocument(d, ExportFormat::Srt, o), d.lines()[0]->id) & bit(ExportLoss::Colour));
}

TEST(Export, SrtAlignmentTagsAreOptional)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}Top\n");
    EXPECT_EQ(exported(d, ExportFormat::Srt), "1\r\n00:00:01,000 --> 00:00:02,000\r\n{\\an8}Top\r\n\r\n");
    ExportOptions o;
    o.srtAlignmentTags = false;
    const auto r = exportDocument(d, ExportFormat::Srt, o);
    EXPECT_EQ(text(r.bytes), "1\r\n00:00:01,000 --> 00:00:02,000\r\nTop\r\n\r\n");
    EXPECT_EQ(lossesOf(r, d.lines()[0]->id), bit(ExportLoss::Position));
}

TEST(Export, OverlapsMergeIntoOneCuePerStretch)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,Bottom\n"
                       "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\an8}Top\n");
    ExportOptions o;
    o.mergeOverlaps = true;
    const auto r = exportDocument(d, ExportFormat::Srt, o);
    EXPECT_EQ(text(r.bytes), "1\r\n00:00:01,000 --> 00:00:02,000\r\nBottom\r\n\r\n"
                             "2\r\n00:00:02,000 --> 00:00:03,000\r\nTop\r\nBottom\r\n\r\n"
                             "3\r\n00:00:03,000 --> 00:00:04,000\r\nBottom\r\n\r\n");
    EXPECT_TRUE(lossesOf(r, d.lines()[0]->id) & bit(ExportLoss::Merged));
    EXPECT_TRUE(lossesOf(r, d.lines()[1]->id) & bit(ExportLoss::Merged));
    EXPECT_EQ(exported(d, ExportFormat::Srt).find("Top\r\nBottom"), std::string::npos);
}

TEST(Export, VttCuesArePlacedFromAlignmentPositionAndMargins)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Bottom\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}Top\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an1}Left\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an9\\pos(1440,270)}Placed\n");
    ExportOptions o;
    o.vttStyles = false;
    EXPECT_EQ(exported(d, ExportFormat::WebVtt, o),
              "WEBVTT\n"
              "\n00:00:01.000 --> 00:00:02.000\nBottom\n"
              "\n00:00:01.000 --> 00:00:02.000 line:5%,start\nTop\n"
              "\n00:00:01.000 --> 00:00:02.000 line:95%,end position:3.12%,line-left align:left\nLeft\n"
              "\n00:00:01.000 --> 00:00:02.000 line:25%,start position:75%,line-right align:right\nPlaced\n");
    o.vttPositions = false;
    const auto r = exportDocument(d, ExportFormat::WebVtt, o);
    EXPECT_EQ(text(r.bytes).find("line:"), std::string::npos);
    EXPECT_EQ(lossesOf(r, d.lines()[1]->id), bit(ExportLoss::Position));
}

TEST(Export, VttKaraokeTimestampsAreOptional)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\k20}ka{\\k30}ra{\\k50}oke\n");
    ExportOptions o;
    o.vttStyles = false;
    EXPECT_EQ(exported(d, ExportFormat::WebVtt, o), "WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nkaraoke\n");
    EXPECT_EQ(lossesOf(exportDocument(d, ExportFormat::WebVtt, o), d.lines()[0]->id), bit(ExportLoss::Karaoke));
    o.vttKaraoke = true;
    const auto r = exportDocument(d, ExportFormat::WebVtt, o);
    EXPECT_EQ(text(r.bytes), "WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nka<00:00:01.200>ra<00:00:01.500>oke\n");
    EXPECT_EQ(lossesOf(r, d.lines()[0]->id), 0u);
}

TEST(Export, VttStylesBecomeCueClasses)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Plain {\\c&H00FF00&}green\n");
    EXPECT_EQ(exported(d, ExportFormat::WebVtt),
              "WEBVTT\n\nSTYLE\n"
              "::cue(.Default) { color: #FFFFFF; font-family: \"Arial\"; background-color: transparent; }\n"
              "::cue(.c00ff00) { color: #00FF00; }\n"
              "\n00:00:01.000 --> 00:00:02.000\n<c.Default>Plain <c.c00ff00>green</c></c>\n");
}

TEST(Export, PlainTextTimesAndActorsAreOptional)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,Aoi,0,0,0,,Two\\Nrows\n");
    EXPECT_EQ(exported(d, ExportFormat::PlainText), "Two rows\n");
    ExportOptions o;
    o.textTimes = o.textActors = true;
    EXPECT_EQ(exported(d, ExportFormat::PlainText, o), "0:00:01.000\t0:00:02.000\tAoi: Two rows\n");
}

TEST(Export, MicroDvdUsesTheFrameRate)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}One{\\i0}\\NTwo\n");
    ExportOptions o;
    o.fps = 25;
    EXPECT_EQ(exported(d, ExportFormat::MicroDvd, o), "{25}{50}{y:i}One|Two\r\n");
    o.fps = 0;
    EXPECT_EQ(exported(d, ExportFormat::MicroDvd, o), "{24}{48}{y:i}One|Two\r\n");
}

TEST(Export, TmPlayerClearsTheScreenAfterALine)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,One\n"
                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Two\n");
    EXPECT_EQ(exported(d, ExportFormat::TMPlayer), "00:00:01:One\r\n00:00:03:Two\r\n00:00:04:\r\n");
}

TEST(Export, TheReportListsEveryApproximation)
{
    const auto d = fixture("typesetting");
    const auto r = exportDocument(d, ExportFormat::WebVtt, {});
    const auto lines = d.lines();
    EXPECT_EQ(lossesOf(r, lines[0]->id), 0u);
    EXPECT_EQ(lossesOf(r, lines[1]->id), bit(ExportLoss::Font) | bit(ExportLoss::Rotation));
    EXPECT_EQ(lossesOf(r, lines[2]->id), bit(ExportLoss::Movement) | bit(ExportLoss::Fade));
    EXPECT_EQ(lossesOf(r, lines[3]->id), bit(ExportLoss::Clip) | bit(ExportLoss::Transform) | bit(ExportLoss::Outline));
    EXPECT_EQ(lossesOf(r, lines[4]->id), bit(ExportLoss::Drawing));
    EXPECT_EQ(lossesOf(r, lines[6]->id), bit(ExportLoss::Effect));

    const auto sbv = exportDocument(d, ExportFormat::Sbv, {});
    EXPECT_EQ(lossesOf(sbv, lines[0]->id), bit(ExportLoss::Position));
    EXPECT_EQ(lossesOf(sbv, lines[2]->id),
              bit(ExportLoss::Movement) | bit(ExportLoss::Fade) | bit(ExportLoss::Colour) | bit(ExportLoss::Position));
    EXPECT_EQ(lossesOf(sbv, lines[7]->id), bit(ExportLoss::Formatting) | bit(ExportLoss::Position));
}
