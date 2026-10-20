// Tags from the original (TM1 #232; docs/research/translation-tag-transfer.md
// section 7): what the Original text carries to the translation, its runs,
// and the saved translation's round trip.

#include "hikari/core/tag_transfer.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

using namespace hikari::core;

namespace {

std::string u8(std::u16string_view text)
{
    const auto s = toUtf8(text);
    return {s.begin(), s.end()};
}

TranslationDraft draft(std::u16string body, std::vector<RunPlacement> placements = {})
{
    return {std::move(body), std::move(placements), true};
}

void expectRoundTrip(const OriginalTags &tags, const TranslationDraft &d)
{
    const auto saved = composeTranslation(tags, d);
    const auto read = readTranslation(tags, saved);
    EXPECT_EQ(u8(read.body), u8(d.body)) << u8(saved);
    EXPECT_EQ(read.placements, d.placements) << u8(saved);
    EXPECT_TRUE(read.carried) << u8(saved);
}

} // namespace

TEST(TagTransfer, PlainTextCarriesNothing)
{
    const auto tags = readOriginalTags(u"Hello there");
    EXPECT_TRUE(tags.leading.empty());
    EXPECT_TRUE(tags.trailing.empty());
    EXPECT_TRUE(tags.runs.empty());
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"Hola"))), "Hola");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u""))), "");
}

TEST(TagTransfer, LeadingBlocksAreCarriedAsOneWithoutComments)
{
    const auto tags = readOriginalTags(u"{\\an8\\fad(200,200)}{TS: sign}{\\blur2}Hello");
    EXPECT_EQ(u8(tags.leading), "{\\an8\\fad(200,200)\\blur2}");
    EXPECT_TRUE(tags.runs.empty());
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"Hola"))), "{\\an8\\fad(200,200)\\blur2}Hola");
}

TEST(TagTransfer, AWholeLineWrapperIsLeadingAndTrailing)
{
    const auto tags = readOriginalTags(u"{\\i1}Hello there{\\i0}");
    EXPECT_EQ(u8(tags.leading), "{\\i1}");
    EXPECT_EQ(u8(tags.trailing), "{\\i0}");
    EXPECT_TRUE(tags.runs.empty());
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"Hola"))), "{\\i1}Hola{\\i0}");
}

TEST(TagTransfer, LineWideTagsAnywhereGoToTheFront)
{
    const auto tags = readOriginalTags(u"{\\fs40}Hello {\\pos(10,20)}there{\\clip(0,0,5,5)}");
    EXPECT_EQ(u8(tags.leading), "{\\fs40\\pos(10,20)\\clip(0,0,5,5)}");
    EXPECT_TRUE(tags.trailing.empty());
    EXPECT_TRUE(tags.runs.empty()); // \pos changes no glyph
}

TEST(TagTransfer, InnerRunsComeFromFormattingState)
{
    const auto tags = readOriginalTags(u"text0{\\i1}text1{\\i0\\b1}text2{\\b0}text3");
    ASSERT_EQ(tags.runs.size(), 2u);
    EXPECT_EQ(tags.runs[0].number, 1);
    EXPECT_EQ(u8(tags.runs[0].text), "text1");
    EXPECT_EQ(tags.runs[0].displayStart, 5u);
    EXPECT_EQ(tags.runs[0].displayEnd, 10u);
    EXPECT_EQ(u8(tags.runs[1].text), "text2");
    EXPECT_TRUE(tags.leading.empty());
    EXPECT_TRUE(tags.trailing.empty());
}

TEST(TagTransfer, RunsAreComposedInAnyOrderWithTheOriginalsSpellings)
{
    const auto tags = readOriginalTags(u"text0{\\i1}text1{\\i0\\b1}text2{\\b0}text3");
    // "A B C D": run 2 (bold) on B, run 1 (italic) on D.
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"A B C D", {{2, 3, 2}, {6, 7, 1}}))),
              "A {\\b1}B{\\b0} C {\\i1}D{\\i0}");
    // Adjacent runs share one block, as in the original.
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"XY", {{0, 1, 1}, {1, 2, 2}}))), "{\\i1}X{\\i0\\b1}Y{\\b0}");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"XY", {{0, 1, 2}, {1, 2, 1}}))), "{\\b1}X{\\i1\\b0}Y{\\i0}");
}

TEST(TagTransfer, ComposingSaysWhereEachRunLanded)
{
    const auto tags = readOriginalTags(u"{\\an8}text0{\\i1}text1{\\i0\\b1}text2{\\b0}text3");
    std::vector<RunPlacement> placed;
    const auto saved = composeTranslation(tags, draft(u"XY Z", {{0, 1, 1}, {1, 2, 2}, {3, 4, 9}}), &placed);
    EXPECT_EQ(u8(saved), "{\\an8}{\\i1}X{\\i0\\b1}Y{\\b0} Z"); // run 9 does not exist
    EXPECT_EQ(placed, (std::vector<RunPlacement>{{11, 12, 1}, {20, 21, 2}}));
}

TEST(TagTransfer, EachOccurrenceIsItsOwnRun)
{
    const auto tags = readOriginalTags(u"a {\\i1}b{\\i0} c {\\i1}d{\\i0}");
    ASSERT_EQ(tags.runs.size(), 2u);
    EXPECT_EQ(u8(tags.runs[0].text), "b");
    EXPECT_EQ(u8(tags.runs[1].text), "d");
}

TEST(TagTransfer, ValuesAreComparedNotSpellings)
{
    const auto tags = readOriginalTags(u"a{\\c&HFF&}b{\\1c&H0000FF&}c{\\c}d");
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "bc");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"xyz", {{1, 2, 1}}))), "x{\\c&HFF&}y{\\c}z");
}

TEST(TagTransfer, TheStyleSaysWhatTheBaseIs)
{
    // Without a Style, \b0 is the default: one run.
    EXPECT_EQ(readOriginalTags(u"a{\\b1}b{\\b0}c").runs.size(), 1u);
    // In a bold Style, "b" is the run and "c" is the base again.
    StyleValues bold;
    bold.bold = true;
    const auto tags = readOriginalTags(u"a{\\b0}b{\\b1}c", &bold);
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "b");
}

TEST(TagTransfer, ARunClosesWithTheLeadingSpellingOrAnEmptyReset)
{
    // No closing tag in the original: the leading \fs40 restores the base.
    const auto sized = readOriginalTags(u"{\\fs40}a {\\fs60}b");
    ASSERT_EQ(sized.runs.size(), 1u);
    EXPECT_EQ(u8(composeTranslation(sized, draft(u"x y z", {{2, 3, 1}}))), "{\\fs40}x {\\fs60}y{\\fs40} z");
    // Nothing set the base: an empty argument returns to the Style's value.
    const auto coloured = readOriginalTags(u"a {\\1c&H0000FF&}b");
    ASSERT_EQ(coloured.runs.size(), 1u);
    EXPECT_EQ(u8(composeTranslation(coloured, draft(u"x y z", {{2, 3, 1}}))), "x {\\1c&H0000FF&}y{\\1c} z");
}

TEST(TagTransfer, AtTheEndOnlyTheOriginalsOwnClosingIsWritten)
{
    // The Original closes nothing: a run ending the translation needs nothing.
    const auto open = readOriginalTags(u"Gate {\\i1}keeper");
    EXPECT_EQ(u8(composeTranslation(open, draft(u"Brama straznik", {{6, 14, 1}}))), "Brama {\\i1}straznik");
    EXPECT_EQ(u8(composeTranslation(open, draft(u"Brama straznik", {{0, 5, 1}}))), "{\\i1}Brama{\\i} straznik");
    expectRoundTrip(open, draft(u"Brama straznik", {{6, 14, 1}}));
    // The Original's own closing stays, as written.
    const auto closed = readOriginalTags(u"Gate {\\i1}keeper{\\i0}");
    EXPECT_EQ(u8(composeTranslation(closed, draft(u"Brama straznik", {{6, 14, 1}}))), "Brama {\\i1}straznik{\\i0}");
}

TEST(TagTransfer, WhitespaceIsNotARunAndRunTextIsTrimmed)
{
    const auto tags = readOriginalTags(u"a{\\i1} {\\i0}b {\\u1}c d {\\u0}e");
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "c d");
    EXPECT_EQ(tags.runs[0].displayStart, 4u);
    EXPECT_EQ(tags.runs[0].displayEnd, 7u);
}

TEST(TagTransfer, KaraokeKeepsLeadingTagsOnly)
{
    const auto tags = readOriginalTags(u"{\\an8}{\\k20}ka{\\k30}{\\i1}ra{\\i0}");
    EXPECT_TRUE(tags.karaoke);
    EXPECT_EQ(u8(tags.leading), "{\\an8}");
    EXPECT_TRUE(tags.runs.empty());
}

TEST(TagTransfer, DrawingAndMalformedTextCarryNothing)
{
    const auto drawing = readOriginalTags(u"{\\an7\\p1}m 0 0 l 10 0{\\p0}");
    EXPECT_TRUE(drawing.drawing);
    EXPECT_TRUE(drawing.leading.empty());
    EXPECT_EQ(u8(composeTranslation(drawing, draft(u"x"))), "x");
    const auto malformed = readOriginalTags(u"{\\an8}Hello {\\i1 there");
    EXPECT_TRUE(malformed.malformed);
    EXPECT_TRUE(malformed.leading.empty());
}

TEST(TagTransfer, ASavedTranslationReadsBackToItsDraft)
{
    const auto tags = readOriginalTags(u"{\\an8}text0{\\i1}text1{\\i0\\b1}text2{\\b0}text3{\\fad(1,2)}");
    expectRoundTrip(tags, draft(u"A B C D", {{2, 3, 2}, {6, 7, 1}}));
    expectRoundTrip(tags, draft(u"XY", {{0, 1, 1}, {1, 2, 2}}));
    expectRoundTrip(tags, draft(u"XY", {{0, 1, 2}, {1, 2, 1}}));
    expectRoundTrip(tags, draft(u"Just text\\Nand a break"));
    expectRoundTrip(tags, draft(u"Own {\\u1}tags{\\u0} stay", {{0, 3, 1}}));
    const auto read = readTranslation(tags, u"");
    EXPECT_TRUE(read.body.empty());
    EXPECT_TRUE(read.carried);
}

TEST(TagTransfer, TheOriginalAsADraftComposesBackToItself)
{
    for (const std::u16string original :
         {u"{\\an8}text0{\\i1}text1{\\i0\\b1}text2{\\b0}text3", u"{\\i1}Hello {\\b1}world{\\b0}{\\i0}",
          u"First row\\N{\\i1}second row{\\i0}", u"Plain", u"Gate {\\i1}keeper", u"{\\fs40}a {\\fs60}b"}) {
        const auto tags = readOriginalTags(original);
        EXPECT_EQ(u8(composeTranslation(tags, draftOfOriginal(original, tags))), u8(original));
    }
    const std::u16string original = u"{\\an8}{TS note}a {\\i1}b{\\i0}";
    const auto tags = readOriginalTags(original);
    const auto d = draftOfOriginal(original, tags);
    EXPECT_EQ(u8(d.body), "a b");
    EXPECT_EQ(d.placements, (std::vector<RunPlacement>{{2, 3, 1}}));
}

TEST(TagTransfer, ATranslationWithItsOwnFormattingKeepsIt)
{
    const auto tags = readOriginalTags(u"{\\an8}a {\\i1}b{\\i0}");
    const auto read = readTranslation(tags, u"{\\an8}x {\\u1}y{\\u0}");
    EXPECT_TRUE(read.carried);
    EXPECT_EQ(u8(read.body), "x {\\u1}y{\\u0}");
    EXPECT_TRUE(read.placements.empty());
    EXPECT_EQ(u8(composeTranslation(tags, read)), "{\\an8}x {\\u1}y{\\u0}");
}

TEST(TagTransfer, DifferentCarriedTagsAreLeftAlone)
{
    const auto tags = readOriginalTags(u"{\\an8}Hello");
    const auto read = readTranslation(tags, u"{\\an2}Hola");
    EXPECT_FALSE(read.carried);
    EXPECT_EQ(u8(read.body), "{\\an2}Hola");
    EXPECT_EQ(u8(composeTranslation(tags, read)), "{\\an2}Hola");
}

TEST(TagTransfer, EditsMovePlacements)
{
    std::vector<RunPlacement> p{{2, 5, 1}};
    shiftPlacements(p, 2, 2, 3); // typed at the run's start: inside it
    EXPECT_EQ(p, (std::vector<RunPlacement>{{2, 8, 1}}));
    shiftPlacements(p, 8, 8, 1); // typed at its end: outside it
    EXPECT_EQ(p, (std::vector<RunPlacement>{{2, 8, 1}}));
    shiftPlacements(p, 0, 1, 0); // deleted before it
    EXPECT_EQ(p, (std::vector<RunPlacement>{{1, 7, 1}}));
    shiftPlacements(p, 1, 7, 2); // the run's text replaced: the new text keeps it
    EXPECT_EQ(p, (std::vector<RunPlacement>{{1, 3, 1}}));
    shiftPlacements(p, 0, 4, 0); // deleted with its text
    EXPECT_TRUE(p.empty());
}

TEST(TagTransfer, PlacingARunMovesOrRemovesIt)
{
    std::vector<RunPlacement> p;
    EXPECT_FALSE(placeRun(p, 3, 3, 1)); // nothing selected
    EXPECT_TRUE(placeRun(p, 0, 2, 1));
    EXPECT_TRUE(placeRun(p, 4, 6, 1)); // a run is placed once: it moves
    EXPECT_EQ(p, (std::vector<RunPlacement>{{4, 6, 1}}));
    EXPECT_TRUE(placeRun(p, 0, 5, 2)); // overlapping runs are trimmed
    EXPECT_EQ(p, (std::vector<RunPlacement>{{0, 5, 2}, {5, 6, 1}}));
    EXPECT_TRUE(placeRun(p, 0, 5, 2)); // the same selection again removes it
    EXPECT_EQ(p, (std::vector<RunPlacement>{{5, 6, 1}}));
}

TEST(TagTransfer, TheNextRunAndTheStyleKeys)
{
    const auto tags = readOriginalTags(u"a{\\i1}b{\\i0} c {\\b1}d{\\b0} e {\\i1}f{\\i0}");
    ASSERT_EQ(tags.runs.size(), 3u);
    EXPECT_EQ(nextUnplacedRun(tags, {}), 1);
    const std::vector<RunPlacement> placed{{0, 1, 1}};
    EXPECT_EQ(nextUnplacedRun(tags, placed), 2);
    EXPECT_EQ(runWithProperty(tags, placed, u"i"), 3); // the first unplaced italic run
    EXPECT_EQ(runWithProperty(tags, {}, u"b"), 2);
    EXPECT_EQ(runWithProperty(tags, {}, u"u"), 0);
    const std::vector<RunPlacement> all{{0, 1, 1}, {1, 2, 2}, {2, 3, 3}};
    EXPECT_EQ(nextUnplacedRun(tags, all), 0);
}

TEST(TagTransfer, RunsWhoseTextIsKeptAreSuggested)
{
    const auto tags = readOriginalTags(u"I love {\\i1}Tokyo{\\i0} and {\\b1}you{\\b0}");
    const auto s = suggestPlacements(tags, draft(u"{\\u1}J'adore{\\u0} Tokyo"));
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s[0], (RunPlacement{18, 23, 1}));
    // Only whole words: "you" is not in "yours".
    EXPECT_EQ(suggestPlacements(tags, draft(u"yours, Tokyo")).size(), 1u);
    // Twice in the translation: not a safe guess.
    EXPECT_TRUE(suggestPlacements(tags, draft(u"Tokyo, Tokyo")).empty());
    // Already placed: nothing to suggest.
    EXPECT_TRUE(suggestPlacements(tags, draft(u"Tokyo", {{0, 5, 1}})).empty());
}

TEST(TagTransfer, ATranslationLeavingARunUnplacedIsMarked)
{
    const auto tags = readOriginalTags(u"{\\i1}Wall {\\b1}high");
    EXPECT_TRUE(hasUnplacedRuns(tags, u"{\\i1}Mur wysoki"));
    EXPECT_FALSE(hasUnplacedRuns(tags, u"{\\i1}Mur {\\b1}wysoki"));
    EXPECT_FALSE(hasUnplacedRuns(tags, u""));             // untranslated
    EXPECT_FALSE(hasUnplacedRuns(tags, u"{\\an2}Mur"));  // its own tags: the translator's
    EXPECT_FALSE(hasUnplacedRuns(readOriginalTags(u"{\\i1}Wall"), u"{\\i1}Mur")); // no runs
}

TEST(TagTransfer, OverlappingFormattingIsOneRunPerState)
{
    const auto tags = readOriginalTags(u"a{\\i1}b{\\b1}c{\\i0}d{\\b0}e");
    ASSERT_EQ(tags.runs.size(), 3u);
    EXPECT_EQ(u8(tags.runs[1].text), "c");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"xyz", {{0, 1, 2}}))), "{\\i1\\b1}x{\\i0\\b0}yz");
    expectRoundTrip(tags, draft(u"x y z", {{0, 1, 3}, {2, 3, 2}, {4, 5, 1}}));
}

TEST(TagTransfer, AWrapperAroundARunKeepsBoth)
{
    const auto tags = readOriginalTags(u"{\\i1}Hello {\\b1}world{\\b0}{\\i0}");
    EXPECT_EQ(u8(tags.leading), "{\\i1}");
    EXPECT_EQ(u8(tags.trailing), "{\\i0}"); // \b0 closes the run, not the wrapper
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"Hola mundo", {{5, 10, 1}}))), "{\\i1}Hola {\\b1}mundo{\\b0}{\\i0}");
    expectRoundTrip(tags, draft(u"Hola mundo", {{5, 10, 1}}));
}

TEST(TagTransfer, ARunOnTheSecondRowOfALine)
{
    const auto tags = readOriginalTags(u"First row\\N{\\i1}second row{\\i0}");
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "second row");
    EXPECT_TRUE(tags.trailing.empty());
    expectRoundTrip(tags, draft(u"Uno\\Ndos", {{5, 8, 1}}));
}

TEST(TagTransfer, ANamedResetTakesThatStylesValues)
{
    StyleValues plain, alt;
    plain.name = u8"Default";
    alt.name = u8"Alt";
    alt.bold = true;
    const std::vector<StyleValues> styles{plain, alt};
    // In Alt, \b1 changes nothing: b and c are one run.
    const auto tags = readOriginalTags(u"a{\\rAlt}b{\\b1}c{\\r}d", &plain, styles);
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "bc");
    expectRoundTrip(tags, draft(u"x y", {{0, 1, 1}}));
    // Without the Document's Styles, Alt is unknown: two runs, as before.
    EXPECT_EQ(readOriginalTags(u"a{\\rAlt}b{\\b1}c{\\r}d", &plain).runs.size(), 2u);
}

TEST(TagTransfer, ManyTransformsStayCheapAndTooManyTagsCarryNothing)
{
    std::u16string many = u"{\\an8}a";
    for (int i = 0; i < 400; ++i)
        many += u"{\\t(0,10,\\fs20)}b";
    const auto tags = readOriginalTags(many);
    ASSERT_FALSE(tags.malformed);
    for (const auto &run : tags.runs)
        for (const auto &[component, value] : run.differs)
            EXPECT_LT(value.size(), 64u) << u8(component);
    for (int i = 0; i < 700; ++i)
        many += u"{\\i1}d";
    EXPECT_TRUE(readOriginalTags(many).malformed); // past kMaxTagTransferTags
    EXPECT_TRUE(readOriginalTags(std::u16string(kMaxTagTransferUnits + 1, u'x')).malformed);
}

TEST(TagTransfer, SuggestionsKeepToWholeWordsInEveryScript)
{
    const auto tags = readOriginalTags(u"I {\\i1}a{\\i0} b");
    EXPECT_TRUE(suggestPlacements(tags, draft(u"éa b")).empty());   // inside a Latin word
    EXPECT_TRUE(suggestPlacements(tags, draft(u"жa b")).empty());   // next to a Cyrillic letter
    EXPECT_EQ(suggestPlacements(tags, draft(u"«a» b")).size(), 1u); // between quotation marks
    EXPECT_EQ(suggestPlacements(tags, draft(u"x、a")).size(), 1u);  // after CJK punctuation
}

TEST(TagTransfer, EqualRunsKeepTheNumberTheTranslatorChose)
{
    const auto tags = readOriginalTags(u"Say {\\i1}one{\\i0} and {\\i1}two{\\i0}");
    ASSERT_EQ(tags.runs.size(), 2u);
    // Run 2 placed: the saved text reads back as run 1, the first unused.
    const auto saved = composeTranslation(tags, draft(u"Di uno y dos", {{9, 12, 2}}));
    auto read = readTranslation(tags, saved);
    ASSERT_EQ(read.placements.size(), 1u);
    EXPECT_EQ(read.placements[0].run, 1);
    relabelRuns(tags, read, runOrder(std::vector<RunPlacement>{{9, 12, 2}}));
    EXPECT_EQ(read.placements[0].run, 2);
    // An order that no longer fits is ignored.
    auto other = readTranslation(tags, saved);
    relabelRuns(tags, other, std::vector<int>{2, 1});
    EXPECT_EQ(other.placements[0].run, 1);
}
