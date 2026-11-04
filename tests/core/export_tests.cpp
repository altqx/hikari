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

constexpr std::string_view kDefaultStyle =
    "Style: Default,Arial,72,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,60,60,54,1\n";

Document assWithStyles(std::string_view styles, std::string_view events)
{
    const std::string file =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n"
        "\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
        "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
        "MarginL, MarginR, MarginV, Encoding\n" +
        std::string(styles) + "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
        std::string(events);
    return loadAss(bytes(file)).document;
}

Document ass(std::string_view events)
{
    return assWithStyles(kDefaultStyle, events);
}

std::string exported(const Document &d, ExportFormat f, const ExportOptions &o = {})
{
    return text(exportDocument(d, f, o).bytes);
}

struct Golden {
    ExportFormat format;
    std::string_view extension;
    ExportOptions options = {};
};

ExportOptions ttml(TtmlProfile profile, bool frames)
{
    ExportOptions o;
    o.ttmlProfile = profile;
    o.ttmlFrames = frames;
    o.language = u8"en";
    return o;
}

const Golden kGoldens[] = {
    {ExportFormat::Srt, "srt"},
    {ExportFormat::WebVtt, "vtt"},
    {ExportFormat::Sbv, "sbv"},
    {ExportFormat::PlainText, "txt"},
    {ExportFormat::MicroDvd, "sub"},
    {ExportFormat::Mpl2, "mpl2.txt"},
    {ExportFormat::TMPlayer, "tmp.txt"},
    {ExportFormat::Ttml, "ttml", ttml(TtmlProfile::Generic, false)},
    {ExportFormat::Dfxp, "dfxp", ttml(TtmlProfile::Generic, true)},
    {ExportFormat::Ttml, "netflix.ttml", ttml(TtmlProfile::Netflix, false)},
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

std::vector<std::string_view> elements(std::string_view xml, std::string_view name)
{
    std::vector<std::string_view> out;
    const std::string open = "<" + std::string(name) + " ";
    for (auto at = xml.find(open); at != std::string_view::npos; at = xml.find(open, at + 1))
        out.push_back(xml.substr(at, xml.find('>', at) - at));
    return out;
}

std::string_view attributeOf(std::string_view element, std::string_view name)
{
    const std::string key = " " + std::string(name) + "=\"";
    const auto at = element.find(key);
    if (at == std::string_view::npos)
        return {};
    const auto start = at + key.size();
    return element.substr(start, element.find('"', start) - start);
}

// "a% b%" as two numbers; nothing unless both are non-negative percentages.
std::vector<double> percentages(std::string_view value)
{
    std::vector<double> out;
    std::istringstream in{std::string(value)};
    std::string part;
    while (in >> part) {
        if (part.size() < 2 || part.back() != '%' || part.front() == '-')
            return {};
        out.push_back(std::stod(part.substr(0, part.size() - 1)));
    }
    return out.size() == 2 ? out : std::vector<double>{};
}

// What the schema can't check: regions in percentages, inside the root, never overlapping (IMSC1 9.3),
// and outlines of at most a tenth of the font size.
void expectImscLayout(std::string_view ttml)
{
    struct Box {
        double x, y, w, h;
    };
    std::vector<Box> boxes;
    for (const auto region : elements(ttml, "region")) {
        const auto origin = percentages(attributeOf(region, "tts:origin"));
        const auto extent = percentages(attributeOf(region, "tts:extent"));
        ASSERT_EQ(origin.size(), 2u) << region;
        ASSERT_EQ(extent.size(), 2u) << region;
        EXPECT_LE(origin[0] + extent[0], 100.0) << region;
        EXPECT_LE(origin[1] + extent[1], 100.0) << region;
        boxes.push_back({origin[0], origin[1], extent[0], extent[1]});
    }
    EXPECT_LE(boxes.size(), 4u);
    for (std::size_t i = 0; i < boxes.size(); ++i)
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            const Box &a = boxes[i], &b = boxes[j];
            EXPECT_TRUE(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y)
                << "regions " << i << " and " << j << " overlap";
        }
    for (const auto style : elements(ttml, "style"))
        if (const auto outline = attributeOf(style, "tts:textOutline"); !outline.empty()) {
            ASSERT_TRUE(outline.ends_with("em")) << style;
            const auto thickness = outline.substr(outline.rfind(' ') + 1);
            EXPECT_LE(std::stod(std::string(thickness.substr(0, thickness.size() - 2))), 0.1) << style;
        }
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
            const auto out = exported(document, g.format, g.options);
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
        exportDocument(document, g.format, g.options);
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

TEST(Export, TtmlCarriesStylesRegionsAndInlineFormatting)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}One{\\i0} & <two>\\N{\\c&H0000FF&}red\n");
    ExportOptions o;
    o.language = u8"en";
    EXPECT_EQ(exported(d, ExportFormat::Ttml, o),
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:ttp=\"http://www.w3.org/ns/ttml#parameter\" "
              "xmlns:tts=\"http://www.w3.org/ns/ttml#styling\" xml:lang=\"en\" "
              "ttp:profile=\"http://www.w3.org/ns/ttml/profile/imsc1/text\">\n"
              "  <head>\n"
              "    <styling>\n"
              "      <style xml:id=\"Default\" tts:fontFamily=\"Arial,proportionalSansSerif\" tts:fontSize=\"100%\" "
              "tts:color=\"#FFFFFFFF\"/>\n"
              "    </styling>\n"
              "    <layout>\n"
              "      <region xml:id=\"bottom\" tts:origin=\"3.12% 65%\" tts:extent=\"93.75% 30%\" "
              "tts:displayAlign=\"after\"/>\n"
              "    </layout>\n"
              "  </head>\n"
              "  <body>\n"
              "    <div>\n"
              "      <p begin=\"00:00:01.000\" end=\"00:00:02.000\" region=\"bottom\" style=\"Default\" "
              "tts:textAlign=\"center\"><span tts:fontStyle=\"italic\">One</span> &amp; &lt;two&gt;<br/>"
              "<span tts:color=\"#FF0000\">red</span></p>\n"
              "    </div>\n"
              "  </body>\n"
              "</tt>\n");
    EXPECT_EQ(exportDocument(d, ExportFormat::Ttml, o).notes, std::vector<ExportLineNote>{});
}

TEST(Export, DfxpLeavesOutTheImscProfile)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,One\n");
    std::string ttml = exported(d, ExportFormat::Ttml);
    const std::string profile = " ttp:profile=\"http://www.w3.org/ns/ttml/profile/imsc1/text\"";
    ASSERT_NE(ttml.find(profile), std::string::npos);
    ttml.erase(ttml.find(profile), profile.size());
    EXPECT_EQ(exported(d, ExportFormat::Dfxp), ttml);
    EXPECT_NE(ttml.find(" xml:lang=\"\""), std::string::npos);
}

TEST(Export, TtmlTimesInFramesFollowTheFrameRate)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,One\n");
    ExportOptions o;
    o.ttmlFrames = true;
    const auto check = [&](double fps, std::string_view rate, std::string_view times) {
        o.fps = fps;
        const auto out = exported(d, ExportFormat::Ttml, o);
        EXPECT_NE(out.find(rate), std::string::npos) << fps << "\n" << out;
        EXPECT_NE(out.find(times), std::string::npos) << fps << "\n" << out;
    };
    check(25, " ttp:frameRate=\"25\">", "begin=\"25f\" end=\"50f\"");
    check(23.976, " ttp:frameRate=\"24\" ttp:frameRateMultiplier=\"1000 1001\">", "begin=\"24f\" end=\"48f\"");
    check(30000.0 / 1001.0, " ttp:frameRate=\"30\" ttp:frameRateMultiplier=\"1000 1001\">", "begin=\"30f\" end=\"60f\"");
    check(12.5, " ttp:frameRate=\"13\" ttp:frameRateMultiplier=\"25 26\">", "begin=\"13f\" end=\"25f\"");
    o.ttmlFrames = false;
    EXPECT_EQ(exported(d, ExportFormat::Ttml, o).find("ttp:frameRate"), std::string::npos);
}

TEST(Export, TtmlPlacesLinesInRegionsThatNeverOverlap)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Bottom\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}Top\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an4}Middle left\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an3\\pos(1800,500)}Placed\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,200,0,0,,Own margin\n");
    const auto r = exportDocument(d, ExportFormat::Ttml, {});
    const auto out = text(r.bytes);
    expectImscLayout(out);
    const auto ps = elements(out, "p");
    ASSERT_EQ(ps.size(), 5u);
    const std::pair<std::string_view, std::string_view> want[] = {
        {"bottom", "center"}, {"top", "center"}, {"middle", "left"}, {"middle", "right"}, {"bottom", "center"}};
    for (std::size_t i = 0; i < ps.size(); ++i) {
        EXPECT_EQ(attributeOf(ps[i], "region"), want[i].first) << i;
        EXPECT_EQ(attributeOf(ps[i], "tts:textAlign"), want[i].second) << i;
    }
    const auto lines = d.lines();
    for (std::size_t i = 0; i < 3; ++i)
        EXPECT_EQ(lossesOf(r, lines[i]->id), 0u) << i;
    EXPECT_EQ(lossesOf(r, lines[3]->id), bit(ExportLoss::Position));
    EXPECT_EQ(lossesOf(r, lines[4]->id), bit(ExportLoss::Position));
}

TEST(Export, TtmlGoldensKeepTheImscLayoutRules)
{
    for (const auto name : {"plain", "karaoke", "typesetting"})
        for (const auto extension : {"ttml", "dfxp", "netflix.ttml"}) {
            SCOPED_TRACE(std::string(name) + "." + extension);
            expectImscLayout(readFile(kFixtures / "golden" / (std::string(name) + "." + extension)));
        }
}

TEST(Export, NetflixTtmlKeepsItalicsOnly)
{
    const auto d = ass("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\b1}bold{\\b0} {\\i1}it{\\i0} {\\c&H0000FF&}red\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}Top\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an5}Middle\n");
    ExportOptions o;
    o.ttmlProfile = TtmlProfile::Netflix;
    o.fps = 25;
    const auto r = exportDocument(d, ExportFormat::Ttml, o);
    const auto out = text(r.bytes);
    expectImscLayout(out);
    EXPECT_NE(out.find(" ttp:frameRate=\"25\">"), std::string::npos);
    EXPECT_NE(out.find("<span tts:fontStyle=\"italic\">it</span>"), std::string::npos);
    for (const auto absent : {"tts:color", "tts:fontWeight", "tts:fontFamily", "tts:fontSize", "<style", "middle"})
        EXPECT_EQ(out.find(absent), std::string::npos) << absent;
    const auto ps = elements(out, "p");
    ASSERT_EQ(ps.size(), 3u);
    EXPECT_EQ(attributeOf(ps[1], "region"), "top");
    EXPECT_EQ(attributeOf(ps[2], "region"), "bottom");
    const auto lines = d.lines();
    EXPECT_EQ(lossesOf(r, lines[0]->id), bit(ExportLoss::Colour) | bit(ExportLoss::Formatting));
    EXPECT_EQ(lossesOf(r, lines[1]->id), 0u);
    EXPECT_EQ(lossesOf(r, lines[2]->id), bit(ExportLoss::Position));
}

TEST(Export, TtmlIdsAndFontNamesAreEscaped)
{
    const auto d = assWithStyles(
        "Style: top,Times New Roman,72,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,0,0,0,1\n"
        "Style: 1 x,My \"Font\",72,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,0,0,0,1\n",
        "Dialogue: 0,0:00:01.00,0:00:02.00,top,,0,0,0,,a\n"
        "Dialogue: 0,0:00:03.00,0:00:04.00,1 x,,0,0,0,,b\n");
    const auto out = exported(d, ExportFormat::Ttml);
    const auto styles = elements(out, "style");
    ASSERT_EQ(styles.size(), 2u);
    EXPECT_EQ(attributeOf(styles[0], "xml:id"), "top_2");
    EXPECT_EQ(attributeOf(styles[0], "tts:fontFamily"), "&quot;Times New Roman&quot;,proportionalSansSerif");
    EXPECT_EQ(attributeOf(styles[1], "xml:id"), "s1_x");
    EXPECT_EQ(attributeOf(styles[1], "tts:fontFamily"), "&quot;My \\&quot;Font\\&quot;&quot;,proportionalSansSerif");
}
