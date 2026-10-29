#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <QFile>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace hikari::core;

namespace {

std::vector<std::byte> readFile(const QString &path)
{
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    const QByteArray data = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(data.size()));
    std::memcpy(out.data(), data.constData(), out.size());
    return out;
}

std::vector<std::byte> fixture(const char *name)
{
    return readFile(QStringLiteral(HIKARI_FIXTURE_DIR "/inputs/") + QLatin1String(name));
}

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

}

TEST(AssSave, UnchangedDocumentsEncodeToTheirExactInput)
{
    for (const char *name : {"unknown-sections.ass", "conversion-source.ass", "PROTOTYPE_A.ass", "PROTOTYPE_B.ass",
                             "tlmode-pairs.ass"}) {
        const auto input = fixture(name);
        EXPECT_EQ(encodeAss(loadAss(input).document), input) << name;
    }
}

TEST(AssSave, C03EditingOneLineKeepsEverythingElse)
{
    const auto input = fixture("unknown-sections.ass");
    auto loaded = loadAss(input);
    const LineId first = loaded.document.lines().front()->id;
    ASSERT_TRUE(loaded.document.setLineText(first, u8"Gate edited"));
    const std::string out = text(encodeAss(loaded.document));

    std::string expected = text(input);
    const std::string before = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\r\n";
    const auto at = expected.find(before);
    ASSERT_NE(at, std::string::npos);
    expected.replace(at, before.size(), "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate edited\r\n");
    EXPECT_EQ(out, expected);

    EXPECT_NE(out.find("[Hikari Synthetic Opaque]\r\n; Preserve order and exact whitespace"), std::string::npos);
    EXPECT_NE(out.find("[Hikari Synthetic Opaque]\r\nToken: repeated-section\r\n[Events]"), std::string::npos);
    EXPECT_NE(out.find("Token:  first:second  \r\n"), std::string::npos);
}

TEST(AssSave, C03LegacyObservationDroppedWhatWeKeep)
{
    const std::string legacy = text(readFile(QStringLiteral(HIKARI_LEGACY_OUTPUTS "/C03-preservation/saved-unknown-sections.ass")));
    EXPECT_EQ(legacy.find("[Hikari Synthetic Opaque]"), std::string::npos);
    const std::string ours = text(encodeAss(loadAss(fixture("unknown-sections.ass")).document));
    EXPECT_NE(ours.find("[Hikari Synthetic Opaque]"), std::string::npos);
    EXPECT_NE(legacy, ours);
}

TEST(AssSave, EditedLinesUseTheLegacySerialization)
{
    auto loaded = loadAss(bytesOf("[Events]\n"
                                  "Dialogue: Marked=0,0:00:01.009,0:00:02.00,Sign, Narrator ,0010,20,30,fx,old\n"
                                  "Comment: 2,1:02:03.45,10:00:00.00,Default,,0,0,0,,keep\n"));
    const auto lines = loaded.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    loaded.document.setLineText(lines[0]->id, u8"new");
    EXPECT_EQ(text(encodeAss(loaded.document)),
              "[Events]\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,Narrator,10,20,30,fx,new\n"
              "Comment: 2,1:02:03.45,10:00:00.00,Default,,0,0,0,,keep\n");
}

TEST(AssSave, LegacyTimeText)
{
    EXPECT_EQ(legacy::assTimeText(0), u8"0:00:00.00");
    EXPECT_EQ(legacy::assTimeText(1009), u8"0:00:01.00");
    EXPECT_EQ(legacy::assTimeText(3'723'450), u8"1:02:03.45");
    EXPECT_EQ(legacy::assTimeText(36'000'000), u8"10:00:00.00");
}

TEST(AssSave, UnknownLineIdChangesNothing)
{
    auto loaded = loadAss(fixture("unknown-sections.ass"));
    EXPECT_FALSE(loaded.document.setLineText(LineId{999}, u8"x"));
    EXPECT_EQ(encodeAss(loaded.document), fixture("unknown-sections.ass"));
}

TEST(AssSave, EditedLinesRewriteMarkersInLegacyOrder)
{
    auto r = loadAss(bytesOf("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,D,[hidden][bookmark]Bob,0,0,0,,a\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,D,[tree_closed]G,0,0,0,,b\n"
                             "Dial: x\n"));
    for (const auto *line : r.document.lines())
        ASSERT_TRUE(r.document.setLineText(line->id, line->text));
    EXPECT_EQ(text(encodeAss(r.document)),
              "[Events]\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,D,[bookmark][hidden]Bob,0,0,0,,a\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,D,[tree_closed]G,0,0,0,,b\n"
              "Dialogue: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,,Dial: x\n");
}

TEST(AssLoad, TLModePairsReadAsOneLine)
{
    const auto r = loadAss(fixture("tlmode-pairs.ass"));
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0]->text, u8"Gate");
    EXPECT_EQ(lines[0]->translation, u8"");
    EXPECT_EQ(lines[0]->style, u8"Default");
    EXPECT_FALSE(lines[0]->comment);
    EXPECT_TRUE(lines[0]->originalSpan);
    EXPECT_EQ(lines[1]->text, u8"Harbor");
    EXPECT_EQ(lines[1]->translation, u8"港");
    EXPECT_EQ(text(encodeAss(r.document)), text(fixture("tlmode-pairs.ass")));
}

TEST(AssSave, EditedTLModePairsMatchTheLegacySave)
{
    auto r = loadAss(fixture("tlmode-pairs.ass"));
    for (const auto *line : r.document.lines())
        ASSERT_TRUE(r.document.setLineText(line->id, line->text));
    const std::string ours = text(encodeAss(r.document));
    std::string legacy = text(readFile(QStringLiteral(HIKARI_LEGACY_OUTPUTS "/supplement-TLMode/saved-tlmode-pairs.ass")));
    std::erase(legacy, '\r');
    const auto events = [](const std::string &s) { return s.substr(s.find("\nDialogue: ") + 1); };
    EXPECT_EQ(events(ours), events(legacy));
}

TEST(AssSave, EditedUnconfirmedPairWritesTheOriginalWithFormFeedD)
{
    auto r = loadAss(bytesOf("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                             "Comment: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,orig\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,fx,\n"));
    const auto *line = r.document.lines().at(0);
    EXPECT_FALSE(line->unconfirmed);
    ASSERT_TRUE(r.document.setLineUnconfirmed(line->id, true));
    EXPECT_EQ(text(encodeAss(r.document)),
              "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,orig\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,fx,\n");
    EXPECT_NE(text(encodeAss(r.document, AssSaveOptions{.hideOriginalOnVideo = true}))
                  .find("Comment: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,orig\n"),
              std::string::npos);
}

TEST(AssSave, HideOriginalOnVideoCommentsUnchangedOriginals)
{
    const std::string input = "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                              "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,one\r\n"
                              "Dialogue: 0,0:00:01.00,0:00:02.00,T,,1,2,3,fx,uno\r\n"
                              "Comment: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,two\n"
                              "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,,dos\n"
                              "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,\fD,three\n"
                              "Dialogue: 0,0:00:05.00,0:00:06.00,T,,0,0,0,,\n"
                              "Dialogue: 0,0:00:07.00,0:00:08.00,O,,0,0,0,,four\n"
                              "Dialogue: 0,0:00:07.00,0:00:08.00,T,,0,0,0,,\n";
    const auto r = loadAss(bytesOf(input));
    ASSERT_EQ(r.document.lines().size(), 4u);
    EXPECT_EQ(text(encodeAss(r.document)), input);
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.hideOriginalOnVideo = true})),
              "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
              "Comment: 0,0:00:01.00,0:00:02.00,O,,1,2,3,fx,one\r\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,T,,1,2,3,fx,uno\r\n"
              "Comment: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,two\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,,dos\n"
              "Comment: 0,0:00:05.00,0:00:06.00,O,,0,0,0,\fD,three\n"
              "Dialogue: 0,0:00:05.00,0:00:06.00,T,,0,0,0,,\n"
              "Dialogue: 0,0:00:07.00,0:00:08.00,O,,0,0,0,,four\n"
              "Dialogue: 0,0:00:07.00,0:00:08.00,T,,0,0,0,,\n");
    const auto translated = loadAss(bytesOf("[Script Info]\nTLMode: Translated\n[Events]\n"
                                            "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,uno\n"));
    EXPECT_EQ(text(encodeAss(translated.document, AssSaveOptions{.hideOriginalOnVideo = true})),
              "[Script Info]\nTLMode: Translated\n[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,uno\n");
}

TEST(AssSave, RendererScriptFollowsGetVisible)
{
    const std::string input = "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                              "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,one\n"
                              "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,fx,uno\n"
                              "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,\fD,two\n"
                              "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,fx,\n"
                              "Dialogue: 0,0:00:05.00,0:00:06.00,T,,0,0,0,,plain\n";
    auto r = loadAss(bytesOf(input));
    ASSERT_EQ(r.document.lines().size(), 3u);
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.renderer = true})),
              "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,fx,one\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,fx,uno\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,fx,two\n"
              "Dialogue: 0,0:00:05.00,0:00:06.00,T,,0,0,0,,plain\n");
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.hideOriginalOnVideo = true, .renderer = true})),
              "[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,fx,uno\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,fx,two\n"
              "Dialogue: 0,0:00:05.00,0:00:06.00,T,,0,0,0,,plain\n");
    const auto first = r.document.lines()[0]->id;
    ASSERT_TRUE(r.document.setLineUnconfirmed(first, true));
    EXPECT_NE(text(encodeAss(r.document, AssSaveOptions{.renderer = true}))
                  .find("Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,one\n"
                        "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,fx,uno\n"),
              std::string::npos);
    const std::string plain = "[Script Info]\n[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,x\n";
    EXPECT_EQ(text(encodeAss(loadAss(bytesOf(plain)).document,
                             AssSaveOptions{.hideOriginalOnVideo = true, .renderer = true})),
              plain);
}

TEST(AssSave, RendererScriptPutsEveryLineUnderEvents)
{
    const std::string input = "[Script Info]\r\nTitle: t\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,info\r\n"
                              "\r\n[Events]\r\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,events\r\n"
                              "\r\n[Aegisub Project Garbage]\r\nComment: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,after";
    auto r = loadAss(bytesOf(input));
    ASSERT_EQ(r.document.lines().size(), 3u);
    EXPECT_EQ(text(encodeAss(r.document)), input);
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.renderer = true})),
              "[Script Info]\r\nTitle: t\r\n"
              "\r\n[Events]\r\n"
              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,info\r\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,events\r\n"
              "\r\n[Aegisub Project Garbage]\r\n"
              "\r\n[Events]\r\nComment: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,after\r\n");
}

TEST(AssSave, RendererScriptPutsHeldLinesAfterTheEventsFormat)
{
    const std::string input = "[Script Info]\r\nTitle: t\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,info\r\n"
                              "\r\n[Events]\r\n; made by hand\r\n\r\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,events\r\n";
    auto r = loadAss(bytesOf(input));
    ASSERT_EQ(r.document.lines().size(), 2u);
    EXPECT_EQ(text(encodeAss(r.document)), input);
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.renderer = true})),
              "[Script Info]\r\nTitle: t\r\n"
              "\r\n[Events]\r\n; made by hand\r\n\r\n"
              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,info\r\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,events\r\n");
    const std::string unformatted = "[Script Info]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,info\n"
                                    "[Events]\n; made by hand\nDialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,events\n";
    r = loadAss(bytesOf(unformatted));
    ASSERT_EQ(r.document.lines().size(), 2u);
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.renderer = true})),
              "[Script Info]\n[Events]\n; made by hand\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,info\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,events\n");
}

TEST(AssSave, RendererScriptKeepsTheOrderOfLinesBeforeTheEventsFormat)
{
    const std::string input = "[Script Info]\nTitle: t\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
                              "\n[Events]\nDialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                              "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,C\n";
    auto r = loadAss(bytesOf(input));
    ASSERT_EQ(r.document.lines().size(), 3u);
    EXPECT_EQ(text(encodeAss(r.document)), input);
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.renderer = true})),
              "[Script Info]\nTitle: t\n"
              "\n[Events]\n"
              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n"
              "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,C\n");
    const std::string own = "[Script Info]\nTitle: t\n\n[Events]\nDialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n"
                            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    r = loadAss(bytesOf(own));
    EXPECT_EQ(text(encodeAss(r.document, AssSaveOptions{.renderer = true})), own);
}

TEST(AssLoad, TLModePairingEdges)
{
    const auto r = loadAss(bytesOf("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,one\n"
                                   "\n"
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,uno\n"
                                   "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,,unpaired\n"
                                   "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,,last\n"));
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0]->translation, u8"uno");
    EXPECT_EQ(lines[0]->span.length, std::string_view("Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,one\n\n"
                                                      "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,uno").size());
    EXPECT_FALSE(lines[1]->originalSpan);
    EXPECT_EQ(lines[2]->text, u8"last");
    EXPECT_EQ(lines[2]->style, u8"Default");
    EXPECT_EQ(lines[2]->start.value, DocumentTime(0));
    bool malformed = false;
    for (const auto &d : r.diagnostics)
        malformed |= d.kind == Diagnostic::Kind::MalformedPair;
    EXPECT_TRUE(malformed);
    EXPECT_EQ(text(encodeAss(r.document)).size(), r.document.source().bytes.size());
}

TEST(AssLoad, UnconfirmedRoundTripsC87)
{
    auto r = loadAss(bytesOf("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,A\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,D,b\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,,B\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0, \fD ,c\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,T,,0,0,0,,C\n"));
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_FALSE(lines[1]->unconfirmed);
    EXPECT_FALSE(lines[2]->unconfirmed);
    ASSERT_TRUE(r.document.setLineUnconfirmed(lines[0]->id, true));
    const auto saved = encodeAss(r.document);
    const auto reloadedDocument = loadAss(saved).document;
    const auto reloaded = reloadedDocument.lines();
    ASSERT_EQ(reloaded.size(), 3u);
    EXPECT_TRUE(reloaded[0]->unconfirmed);
    EXPECT_EQ(reloaded[0]->translation, u8"A");
}

TEST(AssLoad, CommentAfterAnOriginalIsNotItsTranslationC87)
{
    for (const std::string nl : {"\n", "\r\n"}) {
        const std::string input = "[Script Info]" + nl + "TLMode: Yes" + nl + "TLMode Style: O" + nl + "[Events]" + nl +
                                  "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,first" + nl + "; note" + nl +
                                  "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,second" + nl + "{block}" + nl;
        auto r = loadAss(bytesOf(input));
        const auto lines = r.document.lines();
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_FALSE(lines[0]->originalSpan);
        EXPECT_EQ(lines[0]->style, u8"O");
        EXPECT_EQ(lines[0]->start.value, DocumentTime(1'000'000));
        EXPECT_EQ(lines[1]->text, u8"second");
        for (const auto *line : lines)
            ASSERT_TRUE(r.document.setLineText(line->id, line->text));
        const auto saved = encodeAss(r.document);
        EXPECT_EQ(text(saved), input);
        EXPECT_EQ(loadAss(saved).document.lines().size(), 2u);
    }
}

TEST(AssSave, InsertedLinesTakeTheNewlineStyleAroundThem)
{
    for (const std::string nl : {"\n", "\r\n"}) {
        const std::string input = "[Events]" + nl + "Dialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,a" + nl;
        auto r = loadAss(bytesOf(input));
        LineRecord added = *r.document.lines()[0];
        added.text = u8"b";
        ASSERT_TRUE(r.document.insertLineAfter(added.id, added));
        EXPECT_EQ(text(encodeAss(r.document)), input + "Dialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,b" + nl);
        ASSERT_EQ(r.document.lines().size(), 2u);
        EXPECT_TRUE(r.document.lines()[1]->inserted);
    }
    auto r = loadAss(bytesOf("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,a"));
    LineRecord added = *r.document.lines()[0];
    added.text = u8"b";
    ASSERT_TRUE(r.document.insertLineAfter(added.id, added));
    EXPECT_EQ(text(encodeAss(r.document)), "[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,a\n"
                                           "Dialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,b");
}

TEST(AssSave, ScriptInfoAndStyleEdits)
{
    const std::string input = "[Script Info]\r\nTitle: t\r\nPlayResX: 640\r\n\r\n[V4+ Styles]\r\n"
                              "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\r\n"
                              "\r\n[Events]\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x";
    auto doc = loadAss(bytesOf(input)).document;
    ASSERT_TRUE(doc.setScriptInfo(u8"Title", u8"new"));
    ASSERT_TRUE(doc.setScriptInfo(u8"TLMode", u8"Yes"));
    ASSERT_TRUE(doc.removeScriptInfo(u8"PlayResX"));
    EXPECT_FALSE(doc.removeScriptInfo(u8"Missing"));
    ASSERT_TRUE(doc.appendStyle({u8"TL", u8"Arial", u8"20"}));
    EXPECT_EQ(doc.scriptInfo(u8"Title"), u8"new");
    EXPECT_EQ(text(encodeAss(doc)), "[Script Info]\r\nTitle: new\r\nTLMode: Yes\r\n\r\n[V4+ Styles]\r\n"
                                    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\r\n"
                                    "Style: TL,Arial,20\r\n"
                                    "\r\n[Events]\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x");
    ASSERT_TRUE(doc.removeStyle(u8"Default"));
    EXPECT_EQ(text(encodeAss(doc)).find("Style: Default"), std::string::npos);
}

TEST(AssSave, ScriptInfoAddedToADocumentWithoutTheSection)
{
    for (const std::string nl : {"\n", "\r\n"}) {
        const std::string input = "[V4+ Styles]" + nl + "Style: Default,Arial,20" + nl + nl + "[Events]" + nl +
                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x" + nl;
        auto doc = loadAss(bytesOf(input)).document;
        ASSERT_FALSE(doc.scriptInfo(u8"TLMode"));
        ASSERT_TRUE(doc.setScriptInfo(u8"TLMode", u8"Yes"));
        ASSERT_TRUE(doc.setScriptInfo(u8"TLMode Style", u8"TLmode"));
        ASSERT_TRUE(doc.setScriptInfo(u8"TLMode", u8"Yes"));
        EXPECT_EQ(doc.scriptInfo(u8"TLMode Style"), u8"TLmode");
        EXPECT_EQ(doc.sections().front().kind, SectionKind::ScriptInfo);
        EXPECT_EQ(text(encodeAss(doc)), "[Script Info]" + nl + "TLMode: Yes" + nl + "TLMode Style: TLmode" + nl + nl + input);
        ASSERT_TRUE(doc.removeScriptInfo(u8"TLMode"));
        ASSERT_TRUE(doc.removeScriptInfo(u8"TLMode Style"));
        EXPECT_EQ(text(encodeAss(doc)), "[Script Info]" + nl + nl + input);
    }
    const std::string bom = "\xEF\xBB\xBF[Events]\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x";
    auto doc = loadAss(bytesOf(bom)).document;
    ASSERT_TRUE(doc.setScriptInfo(u8"TLMode", u8"Yes"));
    EXPECT_EQ(text(encodeAss(doc)), "\xEF\xBB\xBF[Script Info]\r\nTLMode: Yes\r\n\r\n" + bom.substr(3));
}
