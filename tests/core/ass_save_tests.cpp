// C3: one-Line ASS save preserving unrelated content (C03-preservation).

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

} // namespace

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

    // Only the edited Line differs; its CRLF terminator is kept.
    std::string expected = text(input);
    const std::string before = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\r\n";
    const auto at = expected.find(before);
    ASSERT_NE(at, std::string::npos);
    expected.replace(at, before.size(), "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate edited\r\n");
    EXPECT_EQ(out, expected);

    // Unknown sections and their records survive (approved C03-preservation).
    EXPECT_NE(out.find("[Hikari Synthetic Opaque]\r\n; Preserve order and exact whitespace"), std::string::npos);
    EXPECT_NE(out.find("[Hikari Synthetic Opaque]\r\nToken: repeated-section\r\n[Events]"), std::string::npos);
    EXPECT_NE(out.find("Token:  first:second  \r\n"), std::string::npos); // exact whitespace
}

TEST(AssSave, C03LegacyObservationDroppedWhatWeKeep)
{
    // Legacy run 36591631319 saved the unchanged fixture without either
    // unknown section and with their Token record moved into Script Info.
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
    // Layer, times and margins are written as the legacy writer prints them:
    // integer layer, truncated centiseconds, plain integer margins.
    EXPECT_EQ(text(encodeAss(loaded.document)),
              "[Events]\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,Narrator,10,20,30,fx,new\n"
              "Comment: 2,1:02:03.45,10:00:00.00,Default,,0,0,0,,keep\n");
}

TEST(AssSave, LegacyTimeText)
{
    EXPECT_EQ(legacy::assTimeText(0), u8"0:00:00.00");
    EXPECT_EQ(legacy::assTimeText(1009), u8"0:00:01.00"); // truncates to 10 ms
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
    // The legacy save writes [bookmark] after any group or visibility marker,
    // so a reload reads the bookmark and keeps "[hidden]" as actor text.
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
    EXPECT_EQ(lines[0]->style, u8"Default"); // fields from the translation line
    EXPECT_FALSE(lines[0]->comment);
    EXPECT_TRUE(lines[0]->originalSpan);
    EXPECT_EQ(lines[1]->text, u8"Harbor");
    EXPECT_EQ(lines[1]->translation, u8"港");
    EXPECT_EQ(text(encodeAss(r.document)), text(fixture("tlmode-pairs.ass"))); // unchanged: exact
}

TEST(AssSave, EditedTLModePairsMatchTheLegacySave)
{
    // Legacy run 36591631319 saved tlmode-pairs.ass. Regenerating both pairs
    // gives its Events lines: the untranslated pair collapses to one line in
    // the translation line's fields, the translated one becomes original
    // (TLMode Style, Dialogue) plus translation. The legacy save writes CRLF;
    // regenerated lines keep this file's LF.
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
    // With the legacy "hide original on video" option the original is a Comment.
    EXPECT_NE(text(encodeAss(r.document, AssSaveOptions{.hideOriginalOnVideo = true}))
                  .find("Comment: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,orig\n"),
              std::string::npos);
}

TEST(AssLoad, TLModePairingEdges)
{
    const auto r = loadAss(bytesOf("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,one\n"
                                   "\n" // zero-length: not a token, skipped
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,uno\n"
                                   "Dialogue: 0,0:00:03.00,0:00:04.00,T,,0,0,0,,unpaired\n"
                                   "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,,last\n"));
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0]->translation, u8"uno");
    EXPECT_EQ(lines[0]->span.length, std::string_view("Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,one\n\n"
                                                      "Dialogue: 0,0:00:01.00,0:00:02.00,T,,0,0,0,,uno").size());
    EXPECT_FALSE(lines[1]->originalSpan);
    // No line after the last original: legacy pairs it with an empty plain
    // line, so it keeps its text but takes zero times and the Default style.
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
    // Approved C87-unconfirmed-roundtrip: the form-feed+D the save writes is
    // read back. A bare "D" (or form feed with spaces) is not the marker.
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
    const auto reloaded = loadAss(saved).document.lines();
    ASSERT_EQ(reloaded.size(), 3u);
    EXPECT_TRUE(reloaded[0]->unconfirmed);
    EXPECT_EQ(reloaded[0]->translation, u8"A");
}

TEST(AssLoad, CommentAfterAnOriginalIsNotItsTranslationC87)
{
    // Approved C87-nondialogue-partner, for ';' and lone '{...}' lines in LF
    // and CRLF files: the original stays unpaired, the comment stays put.
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
        // Editing keeps the comment lines and reloads the same way.
        for (const auto *line : lines)
            ASSERT_TRUE(r.document.setLineText(line->id, line->text));
        const auto saved = encodeAss(r.document);
        EXPECT_EQ(text(saved), input);
        EXPECT_EQ(loadAss(saved).document.lines().size(), 2u);
    }
}
