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

std::vector<std::byte> readFile(const QString &path)
{
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    const QByteArray d = f.readAll();
    return bytesOf(std::string_view(d.constData(), static_cast<std::size_t>(d.size())));
}

TEST(SrtLoad, BlankLinesAddNoBreakC82)
{
    // Legacy capture run 37011681370: the old app read a CRLF blank line as a
    // "\r" token, so cue 1 became "Hello\N" and its save gained a blank line.
    // Approved C82-srt-blank-break: blank lines add nothing to the cue text.
    const auto input = readFile(QStringLiteral(HIKARI_CAPTURE_INPUTS "/crlf-two-cues.srt"));
    auto r = loadSrt(input);
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(s8(lines[0]->text), "Hello");
    EXPECT_EQ(s8(lines[1]->text), "Bye");

    // Regenerating cue 1 now gives back the input exactly. The old app's save
    // differs by the extra line break (plus its BOM and final blank line).
    ASSERT_TRUE(r.document.setLineText(lines[0]->id, lines[0]->text));
    EXPECT_EQ(text(encodeSrt(r.document)), text(input));
    std::string legacy = text(readFile(QStringLiteral(HIKARI_CRLF_OBSERVATION)));
    ASSERT_TRUE(legacy.starts_with("\xEF\xBB\xBF") && legacy.ends_with("\r\n\r\n"));
    legacy = legacy.substr(3, legacy.size() - 5);
    const std::string extra = "Hello\r\n\r\n\r\n";
    legacy.replace(legacy.find(extra), extra.size(), "Hello\r\n\r\n");
    EXPECT_EQ(text(input), legacy);
}

TEST(SrtLoad, WhitespaceOnlyLinesAddNoBreakC82)
{
    // The same rule for LF files and whitespace-only lines, including the
    // cue-number trimming of the following cue.
    const auto r = loadSrt(bytesOf("1\n00:00:01,000 --> 00:00:02,000\nA\n  \n\t\n"
                                   "2\n00:00:03,000 --> 00:00:04,000\nB\n \nC\n"));
    const auto lines = r.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(s8(lines[0]->text), "A");
    EXPECT_EQ(s8(lines[1]->text), "B\\NC");
    EXPECT_EQ(*lines[1]->cueNumber, u8"2");
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
