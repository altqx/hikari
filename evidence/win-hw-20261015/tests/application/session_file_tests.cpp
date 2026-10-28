// P6: legacy session files (Notebook::SaveLastSession, LoadLastSession and
// CheckLastSession at 20d647c4) against fixtures in the legacy format.

#include "hikari/application/session_file.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

using namespace hikari::application;

namespace {

std::string fixture(const char *name)
{
    std::ifstream in(std::string(HIKARI_SESSION_FIXTURES) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Session parse(std::string_view bytes)
{
    const auto text = decodeSessionBytes(bytes);
    EXPECT_TRUE(text);
    const auto session = parseSession(text.value_or(std::string()));
    EXPECT_TRUE(session);
    return session.value_or(Session{});
}

} // namespace

TEST(SessionFile, ReadsALegacyFileOnWindows)
{
    const Session s = parse(fixture("legacy-two-tabs.txt"));
    EXPECT_EQ(s.header, "[HikariSub v0.0.1]");
    EXPECT_TRUE(s.closed);
    ASSERT_EQ(s.tabs.size(), 2u);
    const SessionTab &first = s.tabs[0];
    EXPECT_EQ(first.video, "C:\\Video\\episode 1.mkv");
    EXPECT_EQ(first.position, 1001);
    EXPECT_TRUE(first.ffms2);
    EXPECT_EQ(first.subtitles, "C:\\Subs\\episode 1.ass");
    EXPECT_EQ(first.active, 3);
    EXPECT_EQ(first.scroll, 1);
    EXPECT_TRUE(first.editor);
    EXPECT_EQ(first.audio, "C:\\Audio\\episode 1.flac");
    EXPECT_EQ(first.keyframes, "C:\\Video\\episode 1_keyframes.txt");
    EXPECT_EQ(s.tabs[1].subtitles, "C:\\Subs\\\xC5\xBC\xC3\xB3\xC5\x82w.srt"); // UTF-8 kept
    EXPECT_EQ(s.tabs[1].video, "");
    EXPECT_EQ(s.tabs[1].keyframes, "");
}

// C05-audio-association: the second tab has no "Audio:" line. Legacy
// LoadLastSession never reset its audio variable, so that tab opened
// "C:\Audio\episode 1.flac" too; here it has no audio of its own.
TEST(SessionFile, ALaterTabNeverInheritsAudio)
{
    const Session s = parse(fixture("legacy-two-tabs.txt"));
    ASSERT_EQ(s.tabs.size(), 2u);
    EXPECT_EQ(s.tabs[0].audio, "C:\\Audio\\episode 1.flac");
    EXPECT_EQ(s.tabs[1].audio, "");
}

TEST(SessionFile, ExplicitAudioStaysWithItsOwnTab)
{
    const std::string text = "[HikariSub v0.0.1]\nTab: 0\nSubtitles: a.ass\nTab: 1\nSubtitles: b.ass\nAudio: b.wav\n"
                             "Tab: 2\nSubtitles: c.ass\n";
    const auto s = parseSession(text);
    ASSERT_TRUE(s);
    ASSERT_EQ(s->tabs.size(), 3u);
    EXPECT_EQ(s->tabs[0].audio, "");
    EXPECT_EQ(s->tabs[1].audio, "b.wav");
    EXPECT_EQ(s->tabs[2].audio, ""); // legacy: b.wav again
}

// P6-session-crlf (approved): legacy Linux read without folding CRLF, so
// "Tab: 0\r" finished an empty first tab and every value kept its '\r'. Here
// CRLF folds on every platform: the file the program writes itself (CRLF)
// reads back as written.
TEST(SessionFile, CrlfFoldsOnEveryPlatform)
{
    SessionTab tab;
    tab.video = "/home/u/episode 1.mkv";
    tab.position = 1001;
    tab.subtitles = "/home/u/episode 1.ass";
    tab.audio = "/home/u/episode 1.flac";
    SessionTab second;
    second.subtitles = "/home/u/second.ass";
    const std::string written = writeSession("HikariSub v0.1.0", true, {tab, second});
    ASSERT_NE(written.find("\r\n"), std::string::npos);
    const Session s = parse(written);
    EXPECT_EQ(s.header, "[HikariSub v0.1.0]");
    EXPECT_TRUE(s.closed);
    EXPECT_EQ(s.tabs, (std::vector<SessionTab>{tab, second}));
}

TEST(SessionFile, WritesTheLegacyBytes)
{
    const std::string original = fixture("legacy-two-tabs.txt");
    Session s = parse(original);
    // The second tab's audio was never written, so writing it back gives the same file.
    EXPECT_EQ(writeSession("HikariSub v0.0.1", true, s.tabs), original);
    EXPECT_EQ(writeSession("HikariSub v0.0.1", false, parse(fixture("legacy-crash.txt")).tabs),
              fixture("legacy-crash.txt"));
}

TEST(SessionFile, AudioIsWrittenOnlyWhenItIsNotTheVideo)
{
    SessionTab tab;
    tab.video = "v.mkv";
    tab.audio = "v.mkv";
    EXPECT_EQ(writeSession("HikariSub v1", false, {tab}).find("Audio:"), std::string::npos);
    tab.audio = "a.wav";
    tab.keyframes = "k.txt";
    tab.ffms2 = false;
    tab.editor = false;
    EXPECT_EQ(writeSession("HikariSub v1", false, {tab}),
              "\xEF\xBB\xBF[HikariSub v1]\r\nTab: 0\r\nVideo: v.mkv\r\nPosition: 0\r\nFFMS2: 0\r\nSubtitles: "
              "\r\nActive: 0\r\nScroll: 0\r\nEditor: 0\r\nAudio: a.wav\r\nKeyframes: k.txt\r\n");
}

TEST(SessionFile, ACorruptHeaderIsRefused)
{
    const auto text = decodeSessionBytes(fixture("corrupt.txt"));
    ASSERT_TRUE(text);
    EXPECT_FALSE(parseSession(*text));
    EXPECT_FALSE(parseSession("\n\n"));
    EXPECT_FALSE(decodeSessionBytes("")); // FileOpen read nothing: no session
    EXPECT_FALSE(decodeSessionBytes("\xEF\xBB\xBF"));
}

TEST(SessionFile, TheTokenizerQuirksAreKept)
{
    // A header alone still gives one empty tab (the last token finishes it).
    auto s = parseSession("[HikariSub v0.0.1]");
    ASSERT_TRUE(s);
    EXPECT_EQ(s->tabs, std::vector<SessionTab>{SessionTab{}});
    // Blank lines are skipped, unknown lines ignored, a repeated field's
    // last value wins, numbers read as wxAtoi does, and a trailing "Tab: "
    // line with nothing after it only finishes the tab before it.
    s = parseSession("[HikariSub x]\n\n\nTab: 0\nSubtitles: a.ass\nSubtitles: b.ass\nColour: red\nActive: 12abc\n"
                     "Scroll: x\nFFMS2: 0\nEditor: 0\nPosition: -5\nTab: 7\n");
    ASSERT_TRUE(s);
    ASSERT_EQ(s->tabs.size(), 1u);
    EXPECT_EQ(s->tabs[0].subtitles, "b.ass");
    EXPECT_EQ(s->tabs[0].active, 12);
    EXPECT_EQ(s->tabs[0].scroll, 0);
    EXPECT_FALSE(s->tabs[0].ffms2);
    EXPECT_FALSE(s->tabs[0].editor);
    EXPECT_EQ(s->tabs[0].position, -5);
    // Tab numbers only matter as "0" or not: fields before the first
    // "Tab: 1" belong to the first tab, whatever its number.
    s = parseSession("[HikariSub x]\nSubtitles: a.ass\nTab: 1\nSubtitles: b.ass\nTab: 1\nSubtitles: c.ass");
    ASSERT_TRUE(s);
    ASSERT_EQ(s->tabs.size(), 3u);
    EXPECT_EQ(s->tabs[0].subtitles, "a.ass");
    EXPECT_EQ(s->tabs[1].subtitles, "b.ass");
    EXPECT_EQ(s->tabs[2].subtitles, "c.ass");
    // "[Close session]" is not a field.
    s = parseSession("[HikariSub x]\n[Close session]\nTab: 0\nSubtitles: a.ass\n");
    ASSERT_TRUE(s);
    EXPECT_TRUE(s->closed);
    ASSERT_EQ(s->tabs.size(), 1u);
}

TEST(SessionFile, ClosedSessionsAreRecognisedAsCheckLastSessionDid)
{
    EXPECT_TRUE(sessionClosed("[HikariSub v0.0.1]\r\n[Close session]\r\nTab: 0\r\n"));
    EXPECT_TRUE(sessionClosed("[HikariSub v0.0.1]\r[Close session]\rTab: 0\r"));
    EXPECT_FALSE(sessionClosed(*decodeSessionBytes(fixture("legacy-crash.txt"))));
    EXPECT_FALSE(sessionClosed("[HikariSub v0.0.1]\n[Close session]")); // no line end after it
    // The marker counts only right after a line ending in "]".
    EXPECT_FALSE(sessionClosed("[HikariSub v0.0.1]\nTab: 0\n[Close session]\n"));
}

TEST(SessionFile, DecodesAsWxConvAutoDid)
{
    // UTF-16LE with a BOM.
    const std::string utf16("\xFF\xFE[\0H\0i\0k\0a\0r\0i\0S\0u\0b\0]\0\n\0", 26);
    EXPECT_EQ(decodeSessionBytes(utf16), "[HikariSub]\n");
    // Not UTF-8: ISO-8859-1, wxConvAuto's fallback.
    EXPECT_EQ(decodeSessionBytes("Subtitles: \xE9.ass"), "Subtitles: \xC3\xA9.ass");
    // CRLF folds (Windows text mode; Linux too under P6-session-crlf); a
    // lone CR stays, as text mode left it.
    EXPECT_EQ(decodeSessionBytes("a\r\nb\rc"), "a\nb\rc");
}
