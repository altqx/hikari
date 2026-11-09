#include "hikari/application/qc_notes.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::string saved(const core::Document &document)
{
    const auto bytes = core::encodeAss(document);
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::u8string u8(std::string_view s)
{
    return std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size());
}

QcNote note(std::int64_t ms, std::string_view section, std::string_view text, bool done = false)
{
    return {ms, u8(section), done, u8(text)};
}

constexpr std::string_view kScript = "[Script Info]\n"
                                     "ScriptType: v4.00+\n"
                                     "PlayResX: 1920\n"
                                     "PlayResY: 1080\n"
                                     "\n"
                                     "[V4+ Styles]\n"
                                     "Format: Name, Fontname, Fontsize\n"
                                     "Style: Default,Arial,20\n"
                                     "\n"
                                     "[Events]\n"
                                     "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                     "Dialogue: 0,0:03:15.20,0:03:17.80,Default,,0,0,0,,Did you see that?\n"
                                     "Dialogue: 0,0:03:23.62,0:03:25.90,Default,,0,0,0,,This is yabai\n"
                                     "Comment: 0,0:03:24.00,0:03:30.00,Default,,0,0,0,,a comment\n"
                                     "Dialogue: 0,0:03:26.00,0:03:31.02,Default,,0,0,0,,Run!\n"
                                     "Dialogue: 0,0:03:27.40,0:03:31.02,Sign,,0,0,0,,EXIT\n";

std::string fixture(const char *name)
{
    std::ifstream file(std::string(HIKARI_EXTRADATA_FIXTURES) + "/" + name, std::ios::binary);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

std::vector<const core::LineRecord *> lines(const core::Document &d)
{
    return d.lines();
}

std::uint64_t lineAt(const core::Document &d, std::size_t i)
{
    return d.lines()[i]->id.value;
}

}

TEST(QcNotes, ReadsNoteBrowsersDocumentedExample)
{
    const auto notes = parseQcNoteText(u8("0:01 - General note 1\n"
                                          "   More explanation for that note\n"
                                          "\n"
                                          "   Even more explanation\n"
                                          "1:50 - General note 2\n"
                                          "\n"
                                          "[TLC]\n"
                                          "3:24 - yabai should be translated as \"terrible\" instead.\n"
                                          "\n"
                                          "[Timing]\n"
                                          "1:55 - Scene bleed\n"
                                          "2:10 - Flashing subs\n"));
    const std::vector<QcNote> expected{
        note(1000, "General", "General note 1\\NMore explanation for that note\\N\\NEven more explanation"),
        note(110000, "General", "General note 2"),
        note(115000, "Timing", "Scene bleed"),
        note(130000, "Timing", "Flashing subs"),
        note(204000, "TLC", "yabai should be translated as \"terrible\" instead."),
    };
    EXPECT_EQ(notes, expected);
}

TEST(QcNotes, KeepsFractionsAndHoursAndSkipsStrayText)
{
    const auto notes = parseQcNoteText(u8("Notes for ep01\r\n"
                                          "1:02:03.4 - late one\r\n"
                                          "0:07:58.40 Honorific dropped here.\r\n"
                                          "0:00:01.345 – dash\r\n"
                                          "[]\r\n"
                                          "0:05 - 2 frames early\r\n"
                                          "[A, B]\r\n"
                                          "0:06:\r\n"));
    ASSERT_EQ(notes.size(), 5u);
    EXPECT_EQ(notes[0], note(1345, "General", "dash"));
    EXPECT_EQ(notes[1], note(5000, "General", "2 frames early"));
    EXPECT_EQ(notes[2], note(6000, "A  B", ""));
    EXPECT_EQ(notes[3], note(478400, "General", "Honorific dropped here."));
    EXPECT_EQ(notes[4], note(3723400, "General", "late one"));
}

TEST(QcNotes, ReadsMpvQc)
{
    const auto notes = parseQcNoteText(u8("[FILE]\n"
                                          "date      : 2026-11-04 10:00:00\n"
                                          "generator : mpvQC 0.9.0\n"
                                          "path      : /video/ep01.mkv\n"
                                          "\n"
                                          "[DATA]\n"
                                          "[00:03:24] [Translation] yabai should be \"terrible\"\n"
                                          "[00:00:41] [Timing] Ends too early\n"
                                          "# total lines: 2\n"));
    const std::vector<QcNote> expected{note(41000, "Timing", "Ends too early"),
                                       note(204000, "Translation", "yabai should be \"terrible\"")};
    EXPECT_EQ(notes, expected);
}

TEST(QcNotes, ExportReadsBackTheSame)
{
    const std::vector<QcNote> notes{note(204000, "TLC", "first\\Nsecond\\N\\Nthird"), note(41000, "General", "OP"),
                                    note(204400, "Timing", "flash")};
    const auto text = formatQcNoteText(notes);
    EXPECT_EQ(text, u8("[TLC]\n"
                       "0:03:24 - first\n"
                       "    second\n"
                       "\n"
                       "    third\n"
                       "\n"
                       "[General]\n"
                       "0:00:41 - OP\n"
                       "\n"
                       "[Timing]\n"
                       "0:03:24.40 - flash\n"));
    const std::vector<QcNote> expected{notes[1], notes[0], notes[2]};
    EXPECT_EQ(parseQcNoteText(text), expected);
}

TEST(QcNotes, NormalisesAsTheFileKeepsIt)
{
    EXPECT_EQ(normalisedQcNote(note(1345, " A,B ", " one\r\ntwo\n\nthree ")), note(1340, "A B", "one\\Ntwo\\N\\Nthree"));
    EXPECT_EQ(normalisedQcNote(note(0, "", "x")).section, u8"General");
}

TEST(QcNotes, StoresNotesAsScriptInfoKeysAndReadsThemBack)
{
    auto document = load(kScript);
    EXPECT_TRUE(documentQcNotes(document).empty());
    ASSERT_TRUE(setDocumentQcNotes(document, {note(204000, "TLC", "yabai, \"terrible\""), note(41009, "A,B", "OP"),
                                              note(204400, "Timing", "two\\Nlines", true)}));
    const std::string text = saved(document);
    EXPECT_NE(text.find("PlayResY: 1080\n"
                        "Hikari QC 1: 0:00:41.00,A B,open,OP\n"
                        "Hikari QC 2: 0:03:24.00,TLC,open,yabai, \"terrible\"\n"
                        "Hikari QC 3: 0:03:24.40,Timing,done,two\\Nlines\n"
                        "\n[V4+ Styles]"),
              std::string::npos)
        << text;
    const auto reloaded = documentQcNotes(load(text));
    const std::vector<QcNote> expected{note(41000, "A B", "OP"), note(204000, "TLC", "yabai, \"terrible\""),
                                       note(204400, "Timing", "two\\Nlines", true)};
    EXPECT_EQ(reloaded, expected);
}

TEST(QcNotes, RewritingKeepsOtherPropertiesAndUnchangedNotes)
{
    auto document = load("[Script Info]\n"
                         "ScriptType:   v4.00+\n"
                         "Hikari QC 1: 0:00:01.00,General,open,first\n"
                         "Hikari QC 7: not a note\n"
                         "Title:  spaced\n"
                         "\n[Events]\n");
    ASSERT_TRUE(setDocumentQcNotes(document, {note(1000, "General", "first"), note(2000, "General", "second")}));
    EXPECT_EQ(saved(document), "[Script Info]\n"
                               "ScriptType:   v4.00+\n"
                               "Hikari QC 7: not a note\n"
                               "Title:  spaced\n"
                               "Hikari QC 1: 0:00:01.00,General,open,first\n"
                               "Hikari QC 2: 0:00:02.00,General,open,second\n"
                               "\n[Events]\n");
    ASSERT_TRUE(setDocumentQcNotes(document, {}));
    EXPECT_EQ(saved(document), "[Script Info]\n"
                               "ScriptType:   v4.00+\n"
                               "Hikari QC 7: not a note\n"
                               "Title:  spaced\n"
                               "\n[Events]\n");
}

TEST(QcNotes, NumbersSkipKeysThatAreNotNotes)
{
    auto document = load("[Script Info]\nHikari QC 2: broken\n\n[Events]\n");
    ASSERT_TRUE(setDocumentQcNotes(document, {note(1000, "", "a"), note(2000, "", "b")}));
    EXPECT_EQ(saved(document), "[Script Info]\n"
                               "Hikari QC 2: broken\n"
                               "Hikari QC 1: 0:00:01.00,General,open,a\n"
                               "Hikari QC 3: 0:00:02.00,General,open,b\n"
                               "\n[Events]\n");
}

TEST(QcNotes, ImportAddsSkippingNotesAlreadyThereOrReplaces)
{
    const std::vector<QcNote> current{note(1000, "TLC", "a", true), note(5000, "General", "b")};
    const std::vector<QcNote> incoming{note(1000, "TLC", "a"), note(3000, "Timing", "c"), note(3000, "Timing", "c")};
    const auto added = importQcNotes(current, incoming, false);
    EXPECT_EQ(added.added, 2u);
    EXPECT_EQ(added.skipped, 1u);
    const std::vector<QcNote> merged{note(1000, "TLC", "a", true), note(3000, "Timing", "c"), note(3000, "Timing", "c"),
                                     note(5000, "General", "b")};
    EXPECT_EQ(added.notes, merged);
    const auto replaced = importQcNotes(current, incoming, true);
    EXPECT_EQ(replaced.added, 3u);
    EXPECT_EQ(replaced.skipped, 0u);
    EXPECT_EQ(replaced.notes.size(), 3u);
}

TEST(QcNotes, MarksTheLineOnScreenThenTheNearestStart)
{
    const auto document = load(kScript);
    const auto all = lines(document);
    EXPECT_EQ(qcNoteLine(all, 204000)->value, lineAt(document, 1));
    EXPECT_EQ(qcNoteLine(all, 208000)->value, lineAt(document, 4));
    EXPECT_EQ(qcNoteLine(all, 221500)->value, lineAt(document, 4));
    EXPECT_EQ(qcNoteLine(all, 200000)->value, lineAt(document, 1));
    EXPECT_EQ(qcNoteLine(all, 199000)->value, lineAt(document, 0));
    EXPECT_EQ(qcNoteLine(all, 191000)->value, lineAt(document, 0));
    EXPECT_FALSE(qcNoteLine(all, 180200));
    EXPECT_FALSE(qcNoteLine(all, 1307000));
}

TEST(QcNotes, TimeText)
{
    EXPECT_EQ(qcNoteTimeText(204000), u8"0:03:24");
    EXPECT_EQ(qcNoteTimeText(204009), u8"0:03:24");
    EXPECT_EQ(qcNoteTimeText(204400), u8"0:03:24.40");
    EXPECT_EQ(qcNoteTimeText(3723400), u8"1:02:03.40");
}

TEST(QcNotes, SurviveAnAegisubSave)
{
    const std::string written = fixture("qc-notes-hikari.ass");
    auto document = load(written);
    const auto notes = documentQcNotes(document);
    ASSERT_EQ(notes.size(), 5u);
    EXPECT_EQ(notes[1], note(2400, "TLC", "\"Is anyone there?\" → \"Hello? Anyone?\"\\Nreads more naturally."));
    EXPECT_TRUE(notes[2].done);
    EXPECT_EQ(notes[4], note(3723040, "TS", "EXIT sign: wrong font; 日本語"));
    ASSERT_TRUE(setDocumentQcNotes(document, {}));
    ASSERT_TRUE(setDocumentQcNotes(document, notes));
    EXPECT_EQ(saved(document), written);

    const std::string aegisub = fixture("qc-notes-hikari.aegisub.ass");
    EXPECT_NE(aegisub.find("first (edited)"), std::string::npos);
    EXPECT_EQ(documentQcNotes(load(aegisub)), notes);
}
