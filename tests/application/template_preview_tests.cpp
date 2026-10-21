// KE2: the template editor's preview of the karaoke templater on one Line:
// which Lines the templater reads, the reduced snapshot a preview runs on,
// the Lines read back from its result, and the Document the video draws.

#include "hikari/application/template_preview.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
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

// Two info lines and two Styles: dialogue script indices start at 5.
constexpr std::string_view kSong =
    "[Script Info]\n"
    "Title: song\n"
    "ScriptType: v4.00+\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Romaji,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
    "Style: Kanji,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,code once,x = 1\n"                      // 0
    "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,Template syl noblank,{\\pos($x,$y)}\n"   // 1
    "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,templates,not a template\n"            // 2
    "Dialogue: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,template,a Dialogue is no template\n" // 3
    "Comment: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,karaoke,{\\k50}ha{\\k50}ru\n"           // 4
    "Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,{\\pos(1,2)}ha\n"                   // 5
    "Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,{\\pos(3,2)}ru\n"                   // 6
    "Dialogue: 0,0:00:04.00,0:00:06.00,Romaji,,0,0,0,,{\\k50}ka{\\k50}ze\n"                 // 7
    "Dialogue: 0,0:00:04.00,0:00:06.00,Romaji,,0,0,0,fx,{\\pos(1,2)}ka\n"                   // 8
    "Comment: 0,0:00:07.00,0:00:09.00,Kanji,,0,0,0,Karaoke,{\\k50}na{\\k50}tsu\n"           // 9
    "Dialogue: 0,0:00:07.00,0:00:09.00,Kanji,,0,0,0,note,plain dialogue\n";                 // 10

class TemplatePreview : public ::testing::Test {
protected:
    void SetUp() override
    {
        doc = load(kSong);
        for (const auto *l : doc.lines())
            ids.push_back(l->id);
        // A run marked Lines 5 and 6 as generated from 4, and 8 from 7.
        mark(5, 4);
        mark(6, 4);
        mark(8, 7);
    }

    void mark(std::size_t line, std::size_t source)
    {
        ASSERT_TRUE(doc.editLine(
            ids[line], [&](core::LineRecord &l) { l.generatedFrom = ids[source]; }, core::ChangeMark::Kept));
    }

    const core::LineRecord &line(std::size_t i) const { return *doc.lines()[i]; }

    std::vector<std::u8string> textsOf(const core::Document &d) const
    {
        std::vector<std::u8string> out;
        for (const auto *l : d.lines())
            out.push_back(l->text);
        return out;
    }

    core::Document doc;
    std::vector<core::LineId> ids;
};

TEST_F(TemplatePreview, TemplateAndCodeLinesAreTheTemplatersOwn)
{
    EXPECT_EQ(templateKindOf(line(0)), TemplateKind::Code);
    EXPECT_EQ(templateKindOf(line(1)), TemplateKind::Template) << "the first word, in any case";
    EXPECT_EQ(templateKindOf(line(2)), std::nullopt) << "a longer first word";
    EXPECT_EQ(templateKindOf(line(3)), std::nullopt) << "only Comments are read";
    EXPECT_EQ(templateKindOf(line(4)), std::nullopt);
}

TEST_F(TemplatePreview, KaraokeAndGeneratedLinesAreTheTemplatersOwn)
{
    EXPECT_TRUE(isKaraokeLine(line(4))) << "a run's commented-out original";
    EXPECT_TRUE(isKaraokeLine(line(7))) << "a Dialogue without Effect";
    EXPECT_TRUE(isKaraokeLine(line(9))) << "Karaoke, capitalised";
    EXPECT_FALSE(isKaraokeLine(line(10)));
    EXPECT_FALSE(isKaraokeLine(line(1)));
    EXPECT_TRUE(isGeneratedLine(line(5)));
    EXPECT_FALSE(isGeneratedLine(line(4)));
    EXPECT_FALSE(isGeneratedLine(line(10)));
}

TEST_F(TemplatePreview, TheSourceFollowsTheActiveKaraokeLine)
{
    EXPECT_EQ(templatePreviewSource(doc, ids[7], ids[4]), ids[7]);
    EXPECT_EQ(templatePreviewSource(doc, ids[8], std::nullopt), ids[7]) << "a generated Line's source";
    EXPECT_EQ(templatePreviewSource(doc, ids[1], ids[9]), ids[9]) << "editing a template keeps the last Line";
    EXPECT_EQ(templatePreviewSource(doc, ids[10], ids[1]), ids[4]) << "the last is no karaoke Line: the first";
    EXPECT_EQ(templatePreviewSource(doc, std::nullopt, std::nullopt), ids[4]);
    core::Document none = load(
        "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,note,plain\n");
    EXPECT_EQ(templatePreviewSource(none, none.lines().front()->id, std::nullopt), std::nullopt);
}

TEST_F(TemplatePreview, TheSnapshotHoldsTheTemplatesAndTheSourceOnly)
{
    const auto s = templatePreviewSnapshot(doc, ids[7], {{ids[1], u8"{\\pos($x,$bottom)}"}});
    ASSERT_TRUE(s);
    ASSERT_EQ(s->info.size(), 2u);
    ASSERT_EQ(s->styles.size(), 2u);
    EXPECT_EQ(s->styles[1].fields.front(), "Kanji");
    // Code and template Lines, then the source; the Dialogue named template
    // and every other Line stay out.
    ASSERT_EQ(s->dialogues.size(), 3u);
    EXPECT_EQ(s->dialogues[0].id, ids[0].value);
    EXPECT_EQ(s->dialogues[1].id, ids[1].value);
    EXPECT_EQ(s->dialogues[1].text, "{\\pos($x,$bottom)}") << "the text not yet in the Document";
    EXPECT_EQ(s->dialogues[2].id, ids[7].value);
    EXPECT_EQ(s->selected, (std::vector<int>{5 + 2}));
    EXPECT_EQ(s->active, 5 + 2);
    EXPECT_EQ(templatePreviewLineOf(*s, 2), ids[1]);
    EXPECT_EQ(templatePreviewLineOf(*s, 3), ids[7]);
    EXPECT_EQ(templatePreviewLineOf(*s, 4), std::nullopt);
    EXPECT_EQ(templatePreviewLineOf(*s, 0), std::nullopt);
    EXPECT_FALSE(templatePreviewSnapshot(doc, core::LineId{999'999}));
}

TEST_F(TemplatePreview, TheSnapshotKeepsTheSourcesPlaceAmongTheTemplates)
{
    // A template after the source stays after it, as the whole run sees it.
    ASSERT_TRUE(doc.moveLine(ids[1], std::nullopt));
    const auto s = templatePreviewSnapshot(doc, ids[7]);
    ASSERT_TRUE(s);
    ASSERT_EQ(s->dialogues.size(), 3u);
    EXPECT_EQ(s->dialogues[1].id, ids[7].value);
    EXPECT_EQ(s->dialogues[2].id, ids[1].value);
    EXPECT_EQ(s->selected, (std::vector<int>{5 + 1}));
}

TEST_F(TemplatePreview, ThePreviewIsWhatTheRunGeneratedFromTheSource)
{
    MacroResult r;
    MacroDialogueLine kept;
    kept.id = ids[7].value;
    MacroDialogueLine own, other;
    own.generatedFrom = ids[7].value;
    own.effect = "fx";
    own.layer = 2;
    own.startMs = 4000;
    own.endMs = 4500;
    own.style = "Romaji";
    own.text = "{\\pos(5,6)}ka";
    other.generatedFrom = ids[4].value;
    other.text = "other";
    r.dialogues = {kept, own, other, own};
    r.dialogues[3].text = "{\\pos(7,6)}ze";
    const auto lines = templatePreviewLines(r, ids[7]);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].text, u8"{\\pos(5,6)}ka");
    EXPECT_EQ(lines[1].text, u8"{\\pos(7,6)}ze");
    EXPECT_EQ(lines[0].effect, u8"fx");
    EXPECT_EQ(lines[0].layer.value, 2);
    EXPECT_EQ(lines[0].start.value.microseconds(), 4'000'000);
    EXPECT_EQ(lines[0].end.value.microseconds(), 4'500'000);
    EXPECT_EQ(lines[0].generatedFrom, ids[7]);
    EXPECT_FALSE(lines[0].comment);
}

TEST_F(TemplatePreview, TheVideoDrawsThePreviewInPlaceOfTheSourcesLines)
{
    core::LineRecord a, b;
    a.text = u8"{\\pos(9,9)}ha";
    a.start = {u8"0:00:01.00", core::DocumentTime(1'000'000)};
    a.end = {u8"0:00:02.00", core::DocumentTime(2'000'000)};
    b = a;
    b.text = u8"{\\pos(9,9)}ru";
    b.start = {u8"0:00:02.00", core::DocumentTime(2'000'000)};
    b.end = {u8"0:00:03.00", core::DocumentTime(3'000'000)};
    // The span of Line 4 and its preview: every Line over it stays.
    const auto out = templatePreviewDocument(doc, ids[4], {a, b}, {}, {1000, 3000});
    EXPECT_EQ(textsOf(out), (std::vector<std::u8string>{u8"{\\pos(9,9)}ha", u8"{\\pos(9,9)}ru"}))
        << "the old generated Lines go; Comments are never drawn, and later Lines are not over the span";
    // One frame: only the Lines shown then.
    const auto frame = templatePreviewDocument(doc, ids[4], {a, b}, {}, {2500, 2500});
    EXPECT_EQ(textsOf(frame), (std::vector<std::u8string>{u8"{\\pos(9,9)}ru"}));
    // Another Line's generated Lines stay.
    const auto later = templatePreviewDocument(doc, ids[4], {a, b}, {}, {5000, 5000});
    EXPECT_EQ(textsOf(later), (std::vector<std::u8string>{u8"{\\k50}ka{\\k50}ze", u8"{\\pos(1,2)}ka"}));
    EXPECT_EQ(doc.lines().size(), 11u) << "the Document itself is untouched";
}

TEST_F(TemplatePreview, UnmarkedLinesFoundToBeTheSourcesAreLeftOutToo)
{
    // A file opened without marks: the Lines a matching run found are left out.
    ASSERT_TRUE(doc.editLine(ids[5], [](core::LineRecord &l) { l.generatedFrom = {}; }));
    ASSERT_TRUE(doc.editLine(ids[6], [](core::LineRecord &l) { l.generatedFrom = {}; }));
    const auto kept = templatePreviewDocument(doc, ids[4], {}, {}, {1500, 1500});
    EXPECT_EQ(kept.lines().size(), 2u) << "unmarked and not found: they stay";
    const auto out = templatePreviewDocument(doc, ids[4], {}, {ids[5], ids[6]}, {1500, 1500});
    EXPECT_TRUE(out.lines().empty());
}

TEST_F(TemplatePreview, PlayingTheLineKeepsOneTrack)
{
    const auto paused = templatePreviewWindow(1500, false, 1000, 3000);
    EXPECT_EQ(paused.fromMs, 1500);
    EXPECT_EQ(paused.toMs, 1500);
    const auto looping = templatePreviewWindow(2200, true, 1000, 3000);
    EXPECT_EQ(looping.fromMs, 1000);
    EXPECT_EQ(looping.toMs, 3000);
    const auto elsewhere = templatePreviewWindow(5000, true, 1000, 3000);
    EXPECT_EQ(elsewhere.fromMs, 5000);
    EXPECT_EQ(elsewhere.toMs, std::numeric_limits<std::int64_t>::max()) << "every Line not over yet";
}

TEST_F(TemplatePreview, PreviewRunsOnlyReadFromTheHost)
{
    for (const auto service : {HostService::TextExtents, HostService::FrameFromMs, HostService::MsFromFrame,
                               HostService::VideoSize, HostService::Keyframes, HostService::ProjectProperties,
                               HostService::AudioSelection, HostService::DecodePath, HostService::FileName,
                               HostService::Gettext})
        EXPECT_TRUE(isPreviewService(service)) << static_cast<int>(service);
    for (const auto service : {HostService::ClipboardGet, HostService::ClipboardSet, HostService::OpenFiles,
                               HostService::SaveFile, HostService::StatusText, HostService::EditorCursor,
                               HostService::SetEditorCursor, HostService::EditorSelection,
                               HostService::SetEditorSelection, HostService::EditorModified, HostService::Frame,
                               HostService::FrequencyPeaks, HostService::ScriptResolution})
        EXPECT_FALSE(isPreviewService(service)) << static_cast<int>(service);
}

} // namespace
