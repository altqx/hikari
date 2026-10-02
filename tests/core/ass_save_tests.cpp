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
