// C4-srt: SRT load and same-format save, reproducing legacy SubsLoader::LoadSRT.

#include "hikari/core/srt.h"

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

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
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

constexpr std::string_view kLf = "1\n00:00:01,000 --> 00:00:02,500\nHello\nworld\n\n"
                                 "2\n00:00:03,000 --> 00:00:04,000\nSecond\n";

} // namespace

TEST(SrtLoad, CuesNumbersTimesAndJoinedText)
{
    const auto r = loadSrt(bytesOf(kLf));
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(s8(lines[0]->text), "Hello\\Nworld");
    EXPECT_EQ(s8(*lines[0]->cueNumber), "1");
    EXPECT_EQ(lines[0]->start.value, DocumentTime(1'000'000));
    EXPECT_EQ(lines[0]->end.value, DocumentTime(2'500'000));
    EXPECT_EQ(s8(lines[1]->text), "Second");
    EXPECT_EQ(s8(lines[1]->end.lexeme), "00:00:04,000");
}

TEST(SrtLoad, CrlfBlankLinesKeepTheLegacyTrailingBreak)
{
    // Legacy reads CRLF untouched; a CRLF blank line is a "\r" token, so every
    // cue but the last ends with "\N". Reproduced as-is (a defect candidate,
    // not an approved departure; legacy capture pending).
    const auto r = loadSrt(bytesOf("1\r\n00:00:01,000 --> 00:00:02,000\r\nHello\r\n\r\n"
                                   "2\r\n00:00:03,000 --> 00:00:04,000\r\nBye\r\n"));
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(s8(lines[0]->text), "Hello\\N");
    EXPECT_EQ(s8(lines[1]->text), "Bye");
}

TEST(SrtLoad, PrecisionFixtureAndMillisecondFields)
{
    const auto r = loadSrt(fixture("precision.srt"));
    ASSERT_EQ(r.document.lines().size(), 1u);
    EXPECT_EQ(r.document.lines()[0]->start.value, DocumentTime(1'009'000));
    EXPECT_EQ(r.document.lines()[0]->end.value, DocumentTime(2'009'000));
    EXPECT_EQ(legacy::srtTimeMilliseconds(u8"01:02:03,456"), 3'723'456);
    EXPECT_EQ(legacy::srtTimeText(3'723'456), u8"01:02:03,456");
    EXPECT_EQ(legacy::srtTimeText(1009), u8"00:00:01,009");
}

TEST(SrtLoad, CueWithoutNumberAndNumericTextQuirk)
{
    // No number line before the second cue: the first cue keeps all its text.
    auto r = loadSrt(bytesOf("00:00:01,000 --> 00:00:02,000\nA\n00:00:03,000 --> 00:00:04,000\nB\n"));
    ASSERT_EQ(r.document.lines().size(), 2u);
    EXPECT_EQ(s8(r.document.lines()[0]->text), "A");
    EXPECT_FALSE(r.document.lines()[1]->cueNumber);
    // A digits-only last text line is taken as the next cue's number (legacy).
    r = loadSrt(bytesOf("00:00:01,000 --> 00:00:02,000\nThe answer\n42\n00:00:03,000 --> 00:00:04,000\nB\n"));
    EXPECT_EQ(s8(r.document.lines()[0]->text), "The answer");
    EXPECT_EQ(s8(*r.document.lines()[1]->cueNumber), "42");
}

TEST(SrtSave, UnchangedFilesRoundTripExactly)
{
    for (const auto &input : {bytesOf(kLf), fixture("precision.srt"),
                              bytesOf("\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,000\r\nA\r\n\r\n\r\n"),
                              bytesOf("junk before\n\n1\n00:00:01,000 --> 00:00:02,000\nA")})
        EXPECT_EQ(encodeSrt(loadSrt(input).document), input);
}

TEST(SrtSave, EditedCueKeepsItsNumberNewlinesAndSeparator)
{
    auto r = loadSrt(bytesOf(kLf));
    r.document.setLineText(r.document.lines()[0]->id, u8"Changed\\Ntwo lines");
    EXPECT_EQ(text(encodeSrt(r.document)), "1\n00:00:01,000 --> 00:00:02,500\nChanged\ntwo lines\n\n"
                                           "2\n00:00:03,000 --> 00:00:04,000\nSecond\n");
}
