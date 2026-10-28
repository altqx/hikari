// C4-microdvd, C4-mpl2, C4-tmplayer, C4-text: legacy line-based formats.

#include "hikari/core/line_formats.h"

#include <QFile>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace hikari::core;

namespace {

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::vector<std::byte> fixture(const char *name)
{
    QFile f(QStringLiteral(HIKARI_FIXTURE_DIR "/inputs/") + QLatin1String(name));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    return bytesOf(std::string_view(d.constData(), static_cast<std::size_t>(d.size())));
}

std::string s8(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

} // namespace

TEST(MicroDvd, KeepsRawFramesUntilTheDocumentHasARate)
{
    auto r = loadLineFormats(bytesOf("{24}{48}Hello|world\n{-5}{}x\n"));
    EXPECT_EQ(r.document.format(), SubtitleFormat::MicroDvd);
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(*lines[0]->startFrame, 24);
    EXPECT_EQ(*lines[0]->endFrame, 48);
    EXPECT_EQ(s8(lines[0]->text), "Hello|world");
    EXPECT_EQ(*lines[1]->startFrame, 0); // legacy clamps negative frames
    EXPECT_EQ(lines[0]->start.value, DocumentTime(0)); // unresolved: no shared 23.976 fallback
    EXPECT_FALSE(r.document.frameRate());
}

TEST(MicroDvd, C01FpsIsolationRatesArePerDocument)
{
    // fps-isolation.json: raw 24/48 frames; A at 24 fps, B at 24000/1001 then 25.
    auto a = loadLineFormats(fixture("rate-A.sub"));
    auto b = loadLineFormats(fixture("rate-B.sub"));
    ASSERT_EQ(a.document.lines().size(), 1u);
    a.document.setFrameRate(*FrameRate::make(24, 1));
    b.document.setFrameRate(*FrameRate::make(24000, 1001));
    EXPECT_EQ(a.document.lines()[0]->start.value, DocumentTime(1'000'000));
    EXPECT_EQ(a.document.lines()[0]->end.value, DocumentTime(2'000'000));
    EXPECT_EQ(b.document.lines()[0]->start.value, DocumentTime(1'001'000)); // 1001/1000 s
    EXPECT_EQ(b.document.lines()[0]->end.value, DocumentTime(2'002'000));
    b.document.setFrameRate(*FrameRate::make(25, 1));
    EXPECT_EQ(b.document.lines()[0]->start.value, DocumentTime(960'000)); // 24/25 s
    EXPECT_EQ(a.document.lines()[0]->start.value, DocumentTime(1'000'000)); // A unchanged
    EXPECT_EQ(*b.document.lines()[0]->startFrame, 24);                      // raw frames retained
}

TEST(Mpl2, DecisecondTimes)
{
    auto r = loadLineFormats(bytesOf("[10][25] text one\n[30][] two\n"));
    EXPECT_EQ(r.document.format(), SubtitleFormat::Mpl2);
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0]->start.value, DocumentTime(1'000'000));
    EXPECT_EQ(lines[0]->end.value, DocumentTime(2'500'000));
    EXPECT_EQ(s8(lines[0]->text), "text one"); // left-trimmed (legacy)
    EXPECT_EQ(lines[1]->end.value, DocumentTime(0));
}

TEST(TMPlayer, PositionalSecondsAndSeparators)
{
    auto r = loadLineFormats(bytesOf("00:01:02:Hello\n01;00;30 World\n0:5:3,short groups\n"));
    EXPECT_EQ(r.document.format(), SubtitleFormat::TMPlayer);
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0]->start.value, DocumentTime(62'000'000));
    EXPECT_EQ(s8(lines[1]->text), "World");
    EXPECT_EQ(lines[1]->start.value, DocumentTime(3'630'000'000));
    // "0:5:3" read positionally like the legacy parser: minutes "5:", seconds "" -> 5 min, 0 s.
    EXPECT_EQ(s8(lines[2]->start.lexeme), "0:5:3");
    EXPECT_EQ(lines[2]->start.value, DocumentTime(300'000'000));
}

TEST(PlainText, UntimedLinesAndBlankLineRules)
{
    // LF blank lines vanish (not tokens); a CRLF blank line is an empty Line.
    auto lf = loadLineFormats(bytesOf("first line\n\nsecond line\n"));
    EXPECT_EQ(lf.document.format(), SubtitleFormat::PlainText);
    ASSERT_EQ(lf.document.lines().size(), 2u);
    EXPECT_EQ(lf.document.lines()[1]->start.value, DocumentTime(0));
    auto crlf = loadLineFormats(bytesOf("first\r\n\r\nsecond\r\n"));
    ASSERT_EQ(crlf.document.lines().size(), 3u);
    EXPECT_EQ(s8(crlf.document.lines()[1]->text), "");
    // ';' and single-brace lines are hidden non-dialogue records, not Lines.
    auto comments = loadLineFormats(bytesOf("; note\n{comment}\nreal\n"));
    EXPECT_EQ(comments.document.lines().size(), 1u);
}

TEST(Detection, AssAndSrtContentReloadWithTheirLoaders)
{
    auto srt = loadLineFormats(bytesOf("1\n00:00:01,000 --> 00:00:02,000\nHi\n"));
    EXPECT_EQ(srt.document.format(), SubtitleFormat::Srt);
    EXPECT_EQ(s8(srt.document.lines()[0]->text), "Hi");
    auto ass = loadLineFormats(bytesOf("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hey\n"));
    EXPECT_EQ(ass.document.format(), SubtitleFormat::Ass);
    // The first timed Line decides: TMPlayer before MicroDVD stays TMPlayer.
    auto mixed = loadLineFormats(bytesOf("plain\n00:00:01:tmp\n{1}{2}mdvd\n"));
    EXPECT_EQ(mixed.document.format(), SubtitleFormat::TMPlayer);
}

TEST(LineFormatSave, RoundTripsAndRegeneratesEditedLines)
{
    for (const auto &input : {bytesOf("{24}{48}Hello\r\n{50}{60}x"), bytesOf("[10][25] a\n"),
                              bytesOf("00:01:02:Hello\n"), bytesOf("one\r\n\r\ntwo\n"), fixture("rate-A.sub")})
        EXPECT_EQ(encodeLineFormat(loadLineFormats(input).document), input);

    auto mdvd = loadLineFormats(bytesOf("{24}{48}Hello\r\n{50}{60}x\r\n"));
    mdvd.document.setLineText(mdvd.document.lines()[0]->id, u8"Changed");
    EXPECT_EQ(text(encodeLineFormat(mdvd.document)), "{24}{48}Changed\r\n{50}{60}x\r\n");
    auto mpl2 = loadLineFormats(bytesOf("[10][25] a\n"));
    mpl2.document.setLineText(mpl2.document.lines()[0]->id, u8"b");
    EXPECT_EQ(text(encodeLineFormat(mpl2.document)), "[10][25]b\n");
    auto tmp = loadLineFormats(bytesOf("0:1:2:Hello\n"));
    tmp.document.setLineText(tmp.document.lines()[0]->id, u8"Bye");
    EXPECT_EQ(text(encodeLineFormat(tmp.document)), "00:01:00:Bye\n"); // legacy positional read of "0:1:2": 1 min
}
