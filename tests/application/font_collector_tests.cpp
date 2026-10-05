// Y8: the font collector's scan, review and output rules against the legacy
// FontCollector (FontCollector.cpp at 20d647c4), over a font service that
// answers like a renderer from a table of faces. The renderer-verified
// collection itself (libass, fontconfig, the files on disk) is tested in
// tests/backends/font_collector_tests.cpp.
#include "hikari/application/font_collector.h"
#include "fake_font_service.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <set>

using namespace hikari;
using namespace hikari::application;
using collector::Block;
using collector::Note;
using hikari::testing::Face;
using hikari::testing::FakeFonts;

namespace {

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// styles: "<name>,<family>,<bold>,<italic>"; events: "<style>,<text>" or
// "#<style>,<text>" for a Comment.
std::string script(std::initializer_list<std::string> styles, std::initializer_list<std::string> events)
{
    std::string s = "[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
                    "SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
                    "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
    for (const auto &style : styles) {
        const std::size_t a = style.find(','), b = style.find(',', a + 1), c = style.find(',', b + 1);
        s += "Style: " + style.substr(0, a) + "," + style.substr(a + 1, b - a - 1) +
             ",40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000," + style.substr(b + 1, c - b - 1) + "," +
             style.substr(c + 1) + ",0,0,100,100,0,0,1,0,0,7,10,10,10,1\n";
    }
    s += "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (std::string event : events) {
        const bool comment = event.starts_with('#');
        if (comment)
            event.erase(0, 1);
        const std::size_t comma = event.find(',');
        s += std::string(comment ? "Comment" : "Dialogue") + ": 0,0:00:00.00,0:00:01.00," + event.substr(0, comma) +
             ",,0,0,0,," + event.substr(comma + 1) + "\n";
    }
    return s;
}

CollectorDocument tab(int index, const std::string &text)
{
    CollectorDocument d;
    d.tab = index;
    d.script = bytesOf(text);
    d.document = std::make_shared<core::Document>(core::loadAss(d.script).document);
    return d;
}

// Records what Apply writes.
class MemoryOutput final : public CollectorOutput {
public:
    bool openOk = true, failFolder = false;
    std::set<std::u16string> failing;
    std::vector<std::u16string> written;
    std::optional<std::string> labelText;
    bool committed = false, discarded = false, opened = false;
    std::atomic<bool> *cancelAfterPut = nullptr;
    bool open() override
    {
        opened = true;
        return openOk;
    }
    bool folderFailed() const override { return failFolder; }
    bool put(const std::u16string &name, const std::vector<std::byte> &) override
    {
        if (failing.contains(name))
            return false;
        written.push_back(name);
        if (cancelAfterPut)
            cancelAfterPut->store(true); // the user cancels while this file is written
        return true;
    }
    bool label(const std::string &text) override
    {
        labelText = text;
        return true;
    }
    bool unlabel() override
    {
        labelText.reset();
        return true;
    }
    bool commit() override { return committed = true; }
    void discard() override { discarded = true; }
};

std::vector<Note::Kind> kinds(const std::vector<Note> &notes)
{
    std::vector<Note::Kind> out;
    for (const auto &n : notes)
        out.push_back(n.kind);
    return out;
}

FakeFonts arialAndTimes()
{
    FakeFonts f;
    f.faces = {{"Arial", 400, false, "/fonts/arial.ttf"},
               {"Arial", 700, false, "/fonts/arialbd.ttf"},
               {"Times", 400, false, "/fonts/times.ttf"}};
    return f;
}

} // namespace

// ParseTags with {"fn","b","i","p"} and plainText (SubsDialogue.cpp:1086-1158).
TEST(FontCollectorTags, LegacyParseTagsOrderAndValues)
{
    const auto tags = collector::parseTags(U"{\\fnArial\\b1\\bord2}Hi {\\i1\\pos(1,2)}there");
    std::vector<std::pair<std::u32string, std::u32string>> got;
    for (const auto &t : tags)
        got.emplace_back(t.name, t.value);
    EXPECT_EQ(got, (std::vector<std::pair<std::u32string, std::u32string>>{
                       {U"fn", U"Arial"}, {U"b", U"1"}, {U"plain", U"Hi "}, {U"i", U"1"}, {U"plain", U"there"}}));
    // A value that is not a number keeps its leading number part; \b(...) its parentheses' content.
    const auto odd = collector::parseTags(U"{\\b700x\\i(1)}a");
    ASSERT_EQ(odd.size(), 3u);
    EXPECT_EQ(odd[0].value, U"700");
    EXPECT_EQ(odd[1].value, U"1");
    // A drawing's text is "pvector", not plain.
    const auto drawing = collector::parseTags(U"{\\p1}m 0 0 l 1 1{\\p0}x");
    ASSERT_EQ(drawing.size(), 4u);
    EXPECT_EQ(drawing[1].name, U"pvector");
    EXPECT_EQ(drawing[2].value, U"0");
    EXPECT_EQ(drawing[3].name, U"plain");
}

// GetAssFonts (FontCollector.cpp:572-695): Styles by tab, Lines only where an
// \fn starts the font, characters by family, comments skipped.
TEST(FontCollectorScan, StylesLinesAndCharactersAsLegacyGetAssFonts)
{
    const auto a = tab(0, script({"Default,Arial,0,0", "Sign,Missing,0,0"},
                                 {"Default,Hello", "Default,{\\fnTimes}Ab", "#Default,{\\fnComment}skip",
                                  "Sign,{\\b1}x\\Ny\\hz"}));
    const auto b = tab(1, script({"Default,Arial,-1,0"}, {"Default,{\\i2}It"}));
    const auto s = collector::scan({a, b}, [](const std::u16string &f) { return f != u"Missing"; });
    ASSERT_TRUE(s.found.contains(u"Arial"));
    EXPECT_EQ(s.found.at(u"Arial").styles.at(u"Default"), (std::vector<int>{0, 1}));
    EXPECT_TRUE(s.found.at(u"Arial").lines.empty());
    EXPECT_EQ(s.found.at(u"Times").lines.at(1), std::vector<int>{0});
    EXPECT_EQ(s.notFound.at(u"Missing").styles.at(u"Sign"), std::vector<int>{0});
    EXPECT_FALSE(s.found.contains(u"Comment"));
    EXPECT_FALSE(s.notFound.contains(u"Comment"));
    // \N and \n are dropped, \h is a space.
    EXPECT_EQ(s.chars.at(u"Missing"), (std::set<char32_t>{U'x', U'y', U' ', U'z'}));
    EXPECT_EQ(s.chars.at(u"Times"), (std::set<char32_t>{U'A', U'b'}));
    // foundFonts keys: the Style's 0/1, a tag's value as written (\i2 is its own key).
    std::vector<std::u16string> keys;
    for (const auto &[k, v] : s.variants)
        keys.push_back(k);
    EXPECT_EQ(keys, (std::vector<std::u16string>{u"arial00", u"arial10", u"arial12", u"times00"}));
    EXPECT_EQ(s.variants.at(u"arial12").bold, 700);
    EXPECT_TRUE(s.variants.at(u"arial12").italic);
}

// CheckPathAndGlyphs in check mode counts families (FontCollector.cpp:1234-1235);
// an unused Style's font is skipped before any other message (1093-1100).
TEST(FontCollectorReview, CheckModeCountsFamiliesAndWarnsAsLegacy)
{
    auto fonts = arialAndTimes();
    FontCollector collector(fonts);
    const auto review = collector.prepare(
        {tab(0, script({"Default,Arial,0,0", "Bold,Arial,1,1", "Unused,Times,0,0", "Gone,Nowhere,0,0"},
                       {"Default,Hello", "Bold,World"}))},
        CollectorAction::Check);
    ASSERT_TRUE(review);
    EXPECT_FALSE(review->retrievedFonts); // only the copy modes list the files
    EXPECT_EQ(review->foundCount, 1);
    EXPECT_EQ(review->notFoundCount, 0); // the missing font has no text: not counted
    const Block &arial = review->found.at(u"Arial");
    // arial11 has no bold italic face: "is missing bold italics."
    EXPECT_EQ(kinds(arial.warnings), std::vector<Note::Kind>{Note::Kind::MissingBoldItalic});
    const Block &times = review->found.at(u"Times");
    ASSERT_EQ(times.warnings.size(), 1u);
    EXPECT_EQ(times.warnings[0].kind, Note::Kind::UnusedStyle);
    EXPECT_FALSE(times.warnings[0].flag); // no "Will not be copied." when checking
    const Block &nowhere = review->notFound.at(u"Nowhere");
    EXPECT_EQ(nowhere.header, Block::Header::NotFound);
    EXPECT_EQ(kinds(nowhere.warnings), std::vector<Note::Kind>{Note::Kind::UnusedStyle});
    EXPECT_TRUE(review->files.empty());
    EXPECT_TRUE(review->reimports.empty());
    EXPECT_TRUE(review->complete());
    EXPECT_TRUE(collector.result(*review).complete);
}

// Characters the requested face does not draw: legacy's warning, and the
// renderer's fallback file named with what it drew (fonts.md).
TEST(FontCollectorReview, FallbackGlyphsAreReportedAndIncomplete)
{
    auto fonts = arialAndTimes();
    fonts.faces[0].glyphs = U"Helo ";
    fonts.fallback = Face{"Noto", 400, false, "/fonts/noto.ttf", U"中"};
    fonts.document.fallbackGlyphs = {0x4e2d};
    FontCollector collector(fonts);
    const auto review =
        collector.prepare({tab(0, script({"Default,Arial,0,0"}, {"Default,Hello \xe4\xb8\xad"}))}, CollectorAction::Check);
    ASSERT_TRUE(review);
    const Block &arial = review->found.at(u"Arial");
    ASSERT_EQ(kinds(arial.warnings), (std::vector<Note::Kind>{Note::Kind::MissingCharacters, Note::Kind::FallbackCharacters}));
    EXPECT_EQ(arial.warnings[0].a, u"中");
    EXPECT_EQ(arial.warnings[1].b, u"/fonts/noto.ttf");
    EXPECT_FALSE(review->allGlyphs);
    EXPECT_EQ(review->foundCount, 1);
    EXPECT_FALSE(review->complete());
    EXPECT_NE(FontCollector::labelText(*review, nullptr).find("U+4E2D"), std::string::npos);
}

// The copy modes: the provider's files are listed once, each variant's file
// is found by its bytes and written once, the count is of files written.
TEST(FontCollectorApply, CopyCountsFilesAndWritesEachFileOnce)
{
    auto fonts = arialAndTimes();
    FontCollector collector(fonts);
    const std::vector<CollectorDocument> docs{
        tab(0, script({"Default,Arial,0,0", "Bold,Arial,1,0", "Fake,Times,1,0"}, {"Default,a", "Bold,b", "Fake,c"}))};
    const auto review = collector.prepare(docs, CollectorAction::CopyToFolder);
    ASSERT_TRUE(review);
    EXPECT_EQ(review->retrievedFonts, std::optional<std::size_t>(3));
    std::vector<std::u16string> names;
    for (const auto &f : review->files)
        names.push_back(f.name);
    // times10 has no bold face: the regular file again, written once.
    EXPECT_EQ(names, (std::vector<std::u16string>{u"arial.ttf", u"arialbd.ttf", u"times.ttf"}));
    EXPECT_EQ(kinds(review->found.at(u"Times").warnings), std::vector<Note::Kind>{Note::Kind::MissingBold});
    // The reimport checks the collected set alone.
    ASSERT_EQ(fonts.reimported.size(), 1u);
    EXPECT_EQ(fonts.reimported[0], (std::vector<std::string>{"arial.ttf", "arialbd.ttf", "times.ttf"}));
    EXPECT_TRUE(review->complete());

    MemoryOutput out;
    const auto r = collector.apply(*review, out, false);
    EXPECT_EQ(out.written, names);
    EXPECT_EQ(r.foundCount, 3);
    EXPECT_EQ(r.notCopiedCount, 0);
    EXPECT_TRUE(r.complete);
    EXPECT_FALSE(out.labelText); // the label written first is removed at the end
    EXPECT_FALSE(r.labelled);
    EXPECT_EQ(kinds(r.found.at(u"Arial").infos),
              (std::vector<Note::Kind>{Note::Kind::FoundFile, Note::Kind::FoundFile, Note::Kind::Copied, Note::Kind::Copied}));

    // fontSizes is kept: the second run does not list the files again.
    const auto again = collector.prepare(docs, CollectorAction::Zip);
    ASSERT_TRUE(again);
    EXPECT_FALSE(again->retrievedFonts);
}

// "Cannot retrieve the font file sizes and names" ends a copy run.
TEST(FontCollectorApply, NoProviderFilesCancelsCopying)
{
    FakeFonts fonts; // no faces
    FontCollector collector(fonts);
    const auto review = collector.prepare({tab(0, script({"Default,Arial,0,0"}, {"Default,a"}))}, CollectorAction::Zip);
    ASSERT_TRUE(review);
    EXPECT_TRUE(review->retrieveFailed);
    EXPECT_EQ(fonts.collects, 0);
    MemoryOutput out;
    collector.apply(*review, out, true);
    EXPECT_FALSE(out.opened);
}

// Partial output: refused without acknowledgment, labelled with it
// (surface-decision-routing #54).
TEST(FontCollectorApply, IncompleteOutputNeedsAcknowledgmentAndIsLabelled)
{
    auto fonts = arialAndTimes();
    FontCollector collector(fonts);
    const auto review = collector.prepare(
        {tab(0, script({"Default,Arial,0,0", "Sign,Nowhere,0,0"}, {"Default,a", "Sign,b"}))}, CollectorAction::Zip);
    ASSERT_TRUE(review);
    EXPECT_EQ(review->notFoundCount, 1);
    EXPECT_FALSE(review->complete());

    MemoryOutput refused;
    const auto no = collector.apply(*review, refused, false);
    EXPECT_TRUE(no.refused);
    EXPECT_FALSE(refused.opened);
    EXPECT_FALSE(no.written);

    MemoryOutput out;
    const auto yes = collector.apply(*review, out, true);
    EXPECT_EQ(out.written, std::vector<std::u16string>{u"arial.ttf"});
    EXPECT_TRUE(out.committed);
    EXPECT_TRUE(yes.written);
    EXPECT_TRUE(yes.labelled);
    EXPECT_FALSE(yes.complete);
    ASSERT_TRUE(out.labelText);
    EXPECT_NE(out.labelText->find("INCOMPLETE"), std::string::npos);
    EXPECT_NE(out.labelText->find("Fonts not found:\n  Nowhere\n"), std::string::npos);
}

// A file that cannot be written: "Cannot copy font", counted apart, and the
// output is labelled; never success.
TEST(FontCollectorApply, AFailedCopyIsCountedAndLabelled)
{
    auto fonts = arialAndTimes();
    FontCollector collector(fonts);
    const auto review = collector.prepare({tab(0, script({"Default,Arial,0,0", "T,Times,0,0"}, {"Default,a", "T,b"}))},
                                          CollectorAction::CopyToFolder);
    ASSERT_TRUE(review);
    MemoryOutput out;
    out.failing = {u"times.ttf"};
    const auto r = collector.apply(*review, out, false);
    EXPECT_EQ(r.foundCount, 1);
    EXPECT_EQ(r.notCopiedCount, 1);
    EXPECT_FALSE(r.complete);
    EXPECT_TRUE(r.labelled);
    EXPECT_EQ(r.found.at(u"Times").warnings.back().kind, Note::Kind::CannotCopy);
    ASSERT_TRUE(out.labelText);
    EXPECT_NE(out.labelText->find("Fonts that could not be written:\n  times.ttf\n"), std::string::npos);
}

// Cancellation: a folder keeps its label, an archive is never published.
TEST(FontCollectorApply, CancellationLeavesNothingUnlabelled)
{
    auto fonts = arialAndTimes();
    FontCollector collector(fonts);
    const std::vector<CollectorDocument> docs{tab(0, script({"Default,Arial,0,0"}, {"Default,a"}))};
    std::atomic<bool> cancel{true};
    const auto folder = collector.prepare(docs, CollectorAction::CopyToFolder);
    ASSERT_TRUE(folder);
    MemoryOutput out;
    const auto r = collector.apply(*folder, out, false, &cancel);
    EXPECT_TRUE(r.cancelled);
    EXPECT_TRUE(out.written.empty());
    ASSERT_TRUE(out.labelText);
    EXPECT_NE(out.labelText->find("Writing was cancelled."), std::string::npos);
    EXPECT_FALSE(r.complete);

    const auto zip = collector.prepare(docs, CollectorAction::Zip);
    MemoryOutput archive;
    const auto z = collector.apply(*zip, archive, false, &cancel);
    EXPECT_TRUE(z.cancelled);
    EXPECT_TRUE(archive.discarded);
    EXPECT_FALSE(archive.committed);
    EXPECT_FALSE(z.written);

    // Cancelled while the review is prepared: no review at all.
    const auto none = collector.prepare(docs, CollectorAction::Zip, &cancel);
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error(), FontError::Cancelled);
}

// Cancelled after the first font is written: the folder holds that font
// and the label says the writing was cancelled; the archive with an entry
// already added is discarded, never published.
TEST(FontCollectorApply, CancellationMidWriteLeavesNothingUnlabelled)
{
    auto fonts = arialAndTimes();
    FontCollector collector(fonts);
    const std::vector<CollectorDocument> docs{
        tab(0, script({"Default,Arial,0,0", "T,Times,0,0"}, {"Default,a", "T,b"}))};
    std::atomic<bool> cancel{false};
    const auto folder = collector.prepare(docs, CollectorAction::CopyToFolder);
    ASSERT_TRUE(folder);
    ASSERT_EQ(folder->files.size(), 2u);
    ASSERT_TRUE(folder->complete());
    MemoryOutput out;
    out.cancelAfterPut = &cancel;
    const auto r = collector.apply(*folder, out, false, &cancel);
    EXPECT_TRUE(r.cancelled);
    EXPECT_EQ(out.written, std::vector<std::u16string>{u"arial.ttf"});
    EXPECT_EQ(r.foundCount, 1);
    EXPECT_FALSE(r.complete);
    EXPECT_TRUE(r.labelled);
    ASSERT_TRUE(out.labelText);
    EXPECT_NE(out.labelText->find("Writing was cancelled."), std::string::npos);

    cancel = false;
    const auto zip = collector.prepare(docs, CollectorAction::Zip);
    ASSERT_TRUE(zip);
    MemoryOutput archive;
    archive.cancelAfterPut = &cancel;
    const auto z = collector.apply(*zip, archive, false, &cancel);
    EXPECT_TRUE(z.cancelled);
    EXPECT_EQ(archive.written, std::vector<std::u16string>{u"arial.ttf"});
    EXPECT_TRUE(archive.discarded);
    EXPECT_FALSE(archive.committed);
    EXPECT_FALSE(z.written);
    EXPECT_FALSE(archive.labelText);
}

// The renderer's own files beyond the variants (here a fallback) are
// reported with their role and not collected; the clean reimport then
// differs and the collection is incomplete.
TEST(FontCollectorReview, RendererOnlyFilesAreReportedNotCollected)
{
    auto fonts = arialAndTimes();
    Face noto{"Noto", 400, false, "/fonts/noto.ttf", U"中"};
    FakeFonts::add(fonts.document, noto, SelectionStage::Fallback, 0x4e2d, "Arial", NameMatch::None);
    fonts.document.fallbackGlyphs = {0x4e2d};
    fonts.reimportIdentical = false;
    FontCollector collector(fonts);
    const auto review =
        collector.prepare({tab(0, script({"Default,Arial,0,0"}, {"Default,a"}))}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(review);
    ASSERT_EQ(review->rendererFiles.size(), 1u);
    EXPECT_EQ(review->rendererFiles[0].shown, u"/fonts/noto.ttf");
    EXPECT_EQ(review->rendererFiles[0].roles, std::vector<std::string>{"fallback"});
    EXPECT_EQ(review->files.size(), 1u);
    EXPECT_EQ(kinds(review->found.at(u"Arial").infos),
              (std::vector<Note::Kind>{Note::Kind::FoundFile, Note::Kind::RendererFile}));
    EXPECT_FALSE(review->rendererComplete());
    const std::string label = FontCollector::labelText(*review, nullptr);
    EXPECT_NE(label.find("/fonts/noto.ttf (fallback), not collected"), std::string::npos);
    EXPECT_NE(label.find("tab 1: 1 of 1 frames differ"), std::string::npos);
}

// A Document the renderer cannot read is not verified: the collection is
// incomplete in every mode, never "Completed Successfully" by default, and
// the copy is written only after acknowledgment, labelled.
TEST(FontCollectorReview, AnUnreadableDocumentIsIncomplete)
{
    auto fonts = arialAndTimes();
    fonts.documentUnreadable = true;
    FontCollector collector(fonts);
    const std::vector<CollectorDocument> docs{tab(1, script({"Default,Arial,0,0"}, {"Default,a"}))};
    const auto check = collector.prepare(docs, CollectorAction::Check);
    ASSERT_TRUE(check);
    EXPECT_EQ(check->unrendered, std::vector<int>{1});
    EXPECT_EQ(check->notFoundCount, 0);
    EXPECT_FALSE(check->rendererComplete());
    EXPECT_FALSE(collector.result(*check).complete);

    const auto copy = collector.prepare(docs, CollectorAction::CopyToFolder);
    ASSERT_TRUE(copy);
    EXPECT_TRUE(copy->reimports.empty());
    EXPECT_FALSE(copy->complete());
    MemoryOutput refused;
    EXPECT_TRUE(collector.apply(*copy, refused, false).refused);
    EXPECT_FALSE(refused.opened);
    MemoryOutput out;
    const auto r = collector.apply(*copy, out, true);
    EXPECT_EQ(out.written, std::vector<std::u16string>{u"arial.ttf"});
    EXPECT_FALSE(r.complete);
    EXPECT_TRUE(r.labelled);
    ASSERT_TRUE(out.labelText);
    EXPECT_NE(out.labelText->find("Documents the renderer could not read:\n  tab 2\n"), std::string::npos);
}

// Two different fonts with one file name (here in two folders): legacy
// writes both under that name (wxCopyFile overwrites, PutNextEntry adds a
// second entry, FontCollector.cpp:877-911), so the output is not the set the
// renderer verified and the collection is incomplete.
TEST(FontCollectorApply, TwoFontsWithOneFileNameAreIncomplete)
{
    FakeFonts fonts;
    fonts.faces = {{"Arial", 400, false, "/a/arial.ttf"}, {"Sans", 400, false, "/b/ARIAL.TTF"}};
    FontCollector collector(fonts);
    const auto review = collector.prepare(
        {tab(0, script({"Default,Arial,0,0", "S,Sans,0,0"}, {"Default,a", "S,b"}))}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(review);
    ASSERT_EQ(review->files.size(), 2u);
    EXPECT_TRUE(review->rendererComplete());
    EXPECT_EQ(review->nameClashes, std::vector<std::u16string>{u"ARIAL.TTF"});
    EXPECT_FALSE(review->complete());
    MemoryOutput refused;
    EXPECT_TRUE(collector.apply(*review, refused, false).refused);
    MemoryOutput out;
    const auto r = collector.apply(*review, out, true);
    EXPECT_EQ(out.written, (std::vector<std::u16string>{u"arial.ttf", u"ARIAL.TTF"}));
    EXPECT_FALSE(r.complete);
    EXPECT_TRUE(r.labelled);
    ASSERT_TRUE(out.labelText);
    EXPECT_NE(out.labelText->find("Different fonts with the same file name (only one is kept in a folder):\n  ARIAL.TTF\n"),
              std::string::npos);

    // The same bytes reached through two families are one file, no clash.
    FakeFonts same;
    same.faces = {{"Arial", 400, false, "/a/arial.ttf"}, {"Sans", 400, false, "/a/arial.ttf"}};
    FontCollector again(same);
    const auto one = again.prepare({tab(0, script({"Default,Arial,0,0", "S,Sans,0,0"}, {"Default,a", "S,b"}))},
                                   CollectorAction::CopyToFolder);
    ASSERT_TRUE(one);
    EXPECT_EQ(one->files.size(), 1u);
    EXPECT_TRUE(one->nameClashes.empty());
    EXPECT_TRUE(one->complete());
}

// The output cannot be opened: "Path is not available", nothing written.
TEST(FontCollectorApply, PathNotAvailable)
{
    auto fonts = arialAndTimes();
    FontCollector collector(fonts);
    const auto review =
        collector.prepare({tab(0, script({"Default,Arial,0,0"}, {"Default,a"}))}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(review);
    MemoryOutput out;
    out.openOk = false;
    out.failFolder = true;
    const auto r = collector.apply(*review, out, false);
    EXPECT_TRUE(r.pathNotAvailable);
    EXPECT_TRUE(r.cannotCreateFolder);
    EXPECT_TRUE(out.written.empty());
    EXPECT_FALSE(r.complete);
}

// A Type 1 file names its pair and copies the path it cut the extension
// from, which fails (FontCollector.cpp:1206-1219).
TEST(FontCollectorApply, Type1PairFailsAsLegacy)
{
    FakeFonts fonts;
    fonts.faces = {{"Courier", 400, false, "/fonts/COURIER.PFB"}};
    FontCollector collector(fonts);
    const auto review =
        collector.prepare({tab(0, script({"Default,Courier,0,0"}, {"Default,a"}))}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(review);
    ASSERT_EQ(review->files.size(), 2u);
    EXPECT_EQ(review->found.at(u"Courier").infos[1].a, u"/fonts/COURIER.PFM");
    MemoryOutput out;
    const auto r = collector.apply(*review, out, false);
    EXPECT_EQ(out.written, std::vector<std::u16string>{u"COURIER.PFB"});
    EXPECT_EQ(r.foundCount, 1);
    EXPECT_EQ(r.notCopiedCount, 1);
    EXPECT_EQ(r.found.at(u"Courier").warnings.back().a, u"COURIER.");
}
