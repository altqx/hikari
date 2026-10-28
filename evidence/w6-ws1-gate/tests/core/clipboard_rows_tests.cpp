// G2: the Grid clipboard text forms against legacy SubsGrid::CopyRows/OnPaste
// and Dialogue::SetRaw/GetRaw/GetCols/Convert at 20d647c4.

#include "hikari/core/ass_load.h"
#include "hikari/core/clipboard_rows.h"
#include "hikari/core/srt.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace hikari::core;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::vector<LineId> ids(const Document &d)
{
    std::vector<LineId> out;
    for (const auto *l : d.lines())
        out.push_back(l->id);
    return out;
}

std::int64_t ms(DocumentTime t)
{
    return t.microseconds() / 1000;
}

constexpr std::string_view kAss = "[Events]\n"
                                  "Dialogue: 0,0:00:01.00,0:00:02.50,Default,,0,0,0,,{\\i1}one{\\i0}\n"
                                  "Comment: 2,0:00:03.00,0:00:04.00,Sign,[bookmark]Bob,1,2,3,fx,two\n";

} // namespace

TEST(ClipboardCopy, AssRowsUseTheLegacyRawForm)
{
    const auto d = loadAss(bytes(kAss)).document;
    EXPECT_EQ(clipboardRows(d, ids(d), false),
              u8"Dialogue: 0,0:00:01.00,0:00:02.50,Default,,0,0,0,,{\\i1}one{\\i0}\r\n"
              u8"Comment: 2,0:00:03.00,0:00:04.00,Sign,[bookmark]Bob,1,2,3,fx,two\r\n");
}

TEST(ClipboardCopy, TranslationModeCopiesTheTranslation)
{
    const auto d = loadAss(bytes("[Script Info]\nTLMode: Yes\nTLMode Style: Orig\n\n[Events]\n"
                                 "Dialogue: 0,0:00:01.00,0:00:02.00,Orig,,0,0,0,,original\n"
                                 "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated\n"))
                       .document;
    ASSERT_EQ(d.lines().size(), 1u);
    EXPECT_EQ(clipboardRows(d, ids(d), true), u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated\r\n");
    EXPECT_EQ(clipboardRows(d, ids(d), false), u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,original\r\n");
}

TEST(ClipboardCopy, SrtCuesAreNumberedByDocumentRow)
{
    const auto d = loadSrt(bytes("1\n00:00:01,000 --> 00:00:02,000\nfirst\n\n"
                                 "2\n00:00:03,250 --> 00:00:04,000\nsecond\nline\n"))
                       .document;
    ASSERT_EQ(d.lines().size(), 2u);
    EXPECT_EQ(clipboardRows(d, {d.lines()[1]->id}, false),
              u8"2\r\n00:00:03,250 --> 00:00:04,000\r\nsecond\r\nline\r\n\r\n");
}

TEST(ClipboardCopy, ColumnsWriteTheChosenFields)
{
    const auto d = loadAss(bytes(kAss)).document;
    EXPECT_EQ(clipboardColumns(d, ids(d), column::Start | column::Text, false),
              u8"0:00:01.00,{\\i1}one{\\i0}\r\n0:00:03.00,two\r\n");
    // Text without tags; Actor is written without its markers.
    EXPECT_EQ(clipboardColumns(d, ids(d), column::Actor | column::TextWithoutTags, false), u8",one\r\nBob,two\r\n");
    EXPECT_EQ(clipboardColumns(d, ids(d), column::Layer | column::Style | column::MarginVertical | column::Effect, false),
              u8"0,Default,0,,\r\n2,Sign,3,fx,\r\n");
}

TEST(ClipboardPaste, AssRowsKeepTheirFields)
{
    const auto lines = parseClipboardRows(u8"  Dialogue: Marked=0,0:00:01.00,0:00:02.50,Sign,[hidden] Ann ,5,6,7, fx ,  hi there  \r\n"
                                          u8"Comment: 3,0:00:03.00,0:00:04.00,Default,,0,0,0,,c\r\n",
                                          SubtitleFormat::Ass);
    ASSERT_EQ(lines.size(), 2u);
    const auto &a = lines[0];
    EXPECT_FALSE(a.comment);
    EXPECT_EQ(a.layer.value, 0); // "Marked=0": the number after the last '='
    EXPECT_EQ(ms(a.start.value), 1000);
    EXPECT_EQ(ms(a.end.value), 2500);
    EXPECT_EQ(a.style, u8"Sign");
    EXPECT_EQ(a.actor, u8"Ann");
    EXPECT_EQ(a.visibility, LineVisibility::Hidden);
    EXPECT_EQ(a.marginLeft.value, 5);
    EXPECT_EQ(a.marginVertical.value, 7);
    EXPECT_EQ(a.effect, u8"fx");
    EXPECT_EQ(a.text, u8"hi there");
    EXPECT_TRUE(lines[1].comment);
    EXPECT_EQ(lines[1].layer.value, 3);
}

TEST(ClipboardPaste, CopiedRowsPasteBackUnchanged)
{
    const auto d = loadAss(bytes(kAss)).document;
    const auto lines = parseClipboardRows(clipboardRows(d, ids(d), false), SubtitleFormat::Ass);
    ASSERT_EQ(lines.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto &a = *d.lines()[i], &b = lines[i];
        EXPECT_EQ(a.comment, b.comment);
        EXPECT_EQ(a.layer.value, b.layer.value);
        EXPECT_EQ(a.start.value, b.start.value);
        EXPECT_EQ(a.end.value, b.end.value);
        EXPECT_EQ(a.style, b.style);
        EXPECT_EQ(a.actor, b.actor);
        EXPECT_EQ(a.bookmark, b.bookmark);
        EXPECT_EQ(a.marginRight.value, b.marginRight.value);
        EXPECT_EQ(a.effect, b.effect);
        EXPECT_EQ(a.text, b.text);
    }
}

TEST(ClipboardPaste, SrtBlocksConvertToAss)
{
    PasteConversion conversion;
    conversion.style = u8"Converted";
    const auto lines = parseClipboardRows(u8"1\r\n00:00:01,234 --> 00:00:02,000\r\n<i>Hi</i>\r\nthere\r\n\r\n"
                                          u8"2\r\n00:00:03,000 --> 00:00:04,000\r\nB<br>C\r\n",
                                          SubtitleFormat::Ass, conversion);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(ms(lines[0].start.value), 1230); // ZEROIT
    EXPECT_EQ(ms(lines[0].end.value), 2000);
    EXPECT_EQ(lines[0].style, u8"Converted");
    EXPECT_EQ(lines[0].text, u8"{\\i1}Hi{\\i0}\\Nthere");
    EXPECT_EQ(lines[1].text, u8"B\\NC");
}

TEST(ClipboardPaste, ALastCueNumberWithNothingAfterItIsDropped)
{
    const auto lines =
        parseClipboardRows(u8"1\n00:00:01,000 --> 00:00:02,000\nA\n2", SubtitleFormat::Srt);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].text, u8"A");
}

TEST(ClipboardPaste, MicroDvdConvertsFramesAndItalics)
{
    const auto lines = parseClipboardRows(u8"{24}{48}/Hello|world", SubtitleFormat::Ass);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(ms(lines[0].start.value), 1000); // 24 / 23.976 s, truncated, then ZEROIT
    EXPECT_EQ(ms(lines[0].end.value), 2000);
    // A wrap after a tag-led segment gets a reset (legacy AddResetOnMDVDWraps).
    EXPECT_EQ(lines[0].text, u8"{\\i1}Hello{\\r}\\Nworld");
    const auto tags = parseClipboardRows(u8"{1}{2}{y:b}{f:Arial}{s:30}{c:$0000FF}x", SubtitleFormat::Ass);
    EXPECT_EQ(tags[0].text, u8"{\\b1\\fnArial\\fs30\\1c&H0000FF&}x");
}

TEST(ClipboardPaste, TmPlayerAndPlainTextKeepTheDefaultEnd)
{
    // A TMPlayer line has no end; the default Dialogue's 5 s end is kept.
    auto lines = parseClipboardRows(u8"0:00:07:Text", SubtitleFormat::Ass);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(ms(lines[0].start.value), 7000);
    EXPECT_EQ(ms(lines[0].end.value), 5000);
    EXPECT_EQ(lines[0].text, u8"Text");
    lines = parseClipboardRows(u8"just words  ", SubtitleFormat::Ass);
    EXPECT_EQ(lines[0].style, u8"Default");
    EXPECT_EQ(ms(lines[0].start.value), 0);
    EXPECT_EQ(ms(lines[0].end.value), 5000);
    EXPECT_EQ(lines[0].text, u8"just words");
}

TEST(ClipboardPaste, NonDialogueLinesBecomeHiddenDialogue)
{
    const auto lines = parseClipboardRows(u8"; a note", SubtitleFormat::Ass);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FALSE(lines[0].comment);
    EXPECT_EQ(lines[0].visibility, LineVisibility::Hidden);
    EXPECT_EQ(lines[0].text, u8"; a note");
}

TEST(ClipboardPaste, AssRowsConvertToSrt)
{
    const auto lines = parseClipboardRows(
        u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}{\\i1}Hi{\\i0}\\Nthere\\hnow", SubtitleFormat::Srt);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].text, u8"<i>Hi</i>\\Nthere now");
    EXPECT_EQ(ms(lines[0].start.value), 1000);
    // A drawing becomes an empty Line.
    EXPECT_EQ(parseClipboardRows(u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p1}m 0 0 l 1 1",
                                 SubtitleFormat::Srt)[0]
                  .text,
              u8"");
}

TEST(ClipboardPaste, TextIntoTranslationCopiesBeforeConversion)
{
    const auto lines = parseClipboardRows(u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,tl", SubtitleFormat::Ass,
                                          {}, true);
    EXPECT_EQ(lines[0].translation, u8"tl");
}
