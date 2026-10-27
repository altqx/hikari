// Tags from the original (TM1 #232; docs/research/translation-tag-transfer.md
// section 7): what the Original text carries to the translation, its runs,
// and the saved translation's round trip.

#include "hikari/core/tag_transfer.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <array>

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

TEST(TagTransfer, AnAnimatedClipGoesToTheFrontToo)
{
    // libass animates the line's clip wherever the \t stands.
    const auto tags = readOriginalTags(u"a {\\t(\\clip(0,0,100,100))}b{\\r}c");
    EXPECT_EQ(u8(tags.leading), "{\\t(\\clip(0,0,100,100))}");
    EXPECT_TRUE(tags.runs.empty());
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"x y"))), "{\\t(\\clip(0,0,100,100))}x y");
    // With a tag of the run's formatting it is the run's.
    EXPECT_EQ(readOriginalTags(u"a {\\t(\\iclip(0,0,1,1)\\fs20)}b{\\r} c").runs.size(), 1u);
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
    // Adjacent runs share one block, as in the original, the closing first.
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"XY", {{0, 1, 1}, {1, 2, 2}}))), "{\\i1}X{\\i0\\b1}Y{\\b0}");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"XY", {{0, 1, 2}, {1, 2, 1}}))), "{\\b1}X{\\b0\\i1}Y{\\i0}");
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

TEST(TagTransfer, ARunUnderTheTranslatorsOwnFormattingStaysPlaced)
{
    // Ctrl+B over a placed italic run: the bold is the body's, the run stays.
    const auto open = readOriginalTags(u"Wall {\\i1}high");
    expectRoundTrip(open, draft(u"Mur {\\b1}wysoki{\\b0}", {{9, 15, 1}}));
    EXPECT_EQ(u8(composeTranslation(open, draft(u"Mur {\\b1}wysoki{\\b0}", {{9, 15, 1}}))),
              "Mur {\\b1}{\\i1}wysoki{\\i}{\\b0}");
    expectRoundTrip(open, draft(u"{\\b1}Mur wysoki{\\b0}", {{9, 15, 1}}));
    // Tags of the translator's own that aren't the run's: still read, the
    // other runs placed and that text the body's.
    const auto tags = readOriginalTags(u"a {\\i1}b{\\i0} c {\\u1}d{\\u0}");
    const std::u16string own = composeTranslation(tags, draft(u"x y {\\i1\\b1}z{\\i0\\b0}", {{2, 3, 2}}));
    const auto read = readTranslation(tags, own);
    EXPECT_EQ(u8(read.body), "x y {\\i1\\b1}z{\\i0\\b0}") << u8(own);
    EXPECT_EQ(read.placements, (std::vector<RunPlacement>{{2, 3, 2}})) << u8(own);
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
    // Longer than the reader takes: no runs to place, as the editor shows none.
    EXPECT_FALSE(hasUnplacedRuns(tags, u"{\\i1}" + std::u16string(kMaxTagTransferUnits, u'w')));
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
    // Without the Document's Styles, Alt is unknown: the reset is to the
    // Line's Style (libass), and only \b1 makes a run.
    const auto unknown = readOriginalTags(u"a{\\rAlt}b{\\b1}c{\\r}d", &plain);
    ASSERT_EQ(unknown.runs.size(), 1u);
    EXPECT_EQ(u8(unknown.runs[0].text), "c");
}

// A leading named reset gives the Line another Style's values: it is what
// ends a run inside, and a trailing reset undoes it.
TEST(TagTransfer, ALeadingNamedResetEndsTheRunsInside)
{
    StyleValues plain, alt;
    plain.name = u8"Default";
    alt.name = u8"Alt";
    alt.italic = true;
    const std::vector<StyleValues> styles{plain, alt};
    const auto tags = readOriginalTags(u"{\\rAlt}a{\\i0}b", &plain, styles);
    EXPECT_EQ(u8(tags.leading), "{\\rAlt}");
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "b");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"x y z", {{2, 3, 1}}))), "{\\rAlt}x {\\i0}y{\\rAlt} z");
    const auto trailing = readOriginalTags(u"{\\rAlt}a{\\r}", &plain, styles);
    EXPECT_EQ(u8(trailing.trailing), "{\\r}");
}

TEST(TagTransfer, AFlagOutOfRangeIsTheActiveStyles)
{
    // libass (ass_parse.c) takes \i, \u and \s as 0 or 1 only, \b as 0, 1 or
    // a weight of 100 and up; any other value is the Style's.
    EXPECT_TRUE(readOriginalTags(u"a{\\i2}b{\\i0}c").runs.empty());
    EXPECT_TRUE(readOriginalTags(u"a{\\u-1}b{\\u0}c").runs.empty());
    EXPECT_TRUE(readOriginalTags(u"a{\\b50}b{\\b0}c").runs.empty());
    StyleValues plain, alt;
    plain.name = u8"Default";
    alt.name = u8"Alt";
    plain.italic = true;
    const std::vector<StyleValues> styles{plain, alt};
    // After \rAlt the active Style is Alt, not the Line's: \i2 is upright.
    const auto tags = readOriginalTags(u"{\\rAlt}a{\\i1}b{\\i2}c", &plain, styles);
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "b");
}

TEST(TagTransfer, SignedNumbersReadAsTheRendererReadsThem)
{
    // strtol and strtod take a '+'; -0 is 0.
    EXPECT_TRUE(readOriginalTags(u"a{\\frz-0}b").runs.empty());
    EXPECT_TRUE(readOriginalTags(u"a{\\frz+0}b").runs.empty());
    const auto drawing = readOriginalTags(u"{\\p-0}a {\\i1}b{\\i0}");
    EXPECT_FALSE(drawing.drawing);
    EXPECT_EQ(drawing.runs.size(), 1u);
    const auto plus = readOriginalTags(u"a {\\i+1}b{\\i0} c");
    ASSERT_EQ(plus.runs.size(), 1u);
    EXPECT_EQ(u8(plus.runs[0].text), "b");
}

TEST(TagTransfer, ARunOpensWithEveryTransformStillRunning)
{
    const auto tags = readOriginalTags(u"a {\\t(\\fs20)}b {\\t(\\bord2)}c{\\r} d");
    ASSERT_EQ(tags.runs.size(), 2u);
    EXPECT_EQ(u8(tags.runs[1].text), "c");
    const auto d = draft(u"x y z", {{2, 3, 2}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "x {\\t(\\fs20)\\t(\\bord2)}y{\\r} z");
    expectRoundTrip(tags, d);
}

TEST(TagTransfer, ARelativeSizeOpensAsTheSizeItGives)
{
    StyleValues style;
    style.name = u8"Default";
    style.fontsize = u8"20";
    const std::vector<StyleValues> styles{style};
    // \fs+5 scales 30 by 1.5 (libass): the run on c is 45 from the base.
    const auto tags = readOriginalTags(u"a {\\fs30}b {\\fs+5}c{\\fs20} d", &style, styles);
    ASSERT_EQ(tags.runs.size(), 2u);
    EXPECT_EQ(u8(tags.runs[1].text), "c");
    const auto d = draft(u"x y z", {{2, 3, 2}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "x {\\fs45}y{\\fs20} z");
    expectRoundTrip(tags, d);
}

TEST(TagTransfer, NoSuggestionAcrossTheTranslatorsOwnTags)
{
    const auto tags = readOriginalTags(u"I {\\i1}Tokyo{\\i0} x");
    EXPECT_TRUE(suggestPlacements(tags, draft(u"To{\\b1}ky{\\b0}o")).empty());
    EXPECT_EQ(suggestPlacements(tags, draft(u"{\\b1}Tokyo{\\b0}")).size(), 1u);
}

TEST(TagTransfer, NoSuggestionForTextTwoRunsShare)
{
    const auto tags = readOriginalTags(u"I {\\i1}Tokyo{\\i0} and {\\b1}Tokyo{\\b0} x");
    ASSERT_EQ(tags.runs.size(), 2u);
    EXPECT_TRUE(suggestPlacements(tags, draft(u"Tokyo y")).empty());
    // With one of them placed, the other is the only one left.
    EXPECT_EQ(suggestPlacements(tags, draft(u"Tokyo y", {{6, 7, 2}})).size(), 1u);
}

TEST(TagTransfer, AResetClosesBeforeTheNextRunOpens)
{
    // Run 2 placed just before run 1: the \r that ends it comes before the
    // \u1 that opens run 1, or it would undo it.
    const auto tags = readOriginalTags(u"x {\\u1}U{\\r} x {\\b1}B{\\r} x");
    ASSERT_EQ(tags.runs.size(), 2u);
    const auto d = draft(u"y BU y", {{2, 3, 2}, {3, 4, 1}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "y {\\b1}B{\\r\\u1}U{\\r} y");
    expectRoundTrip(tags, d);
    // A value both runs share is set again after the reset.
    const auto shared = readOriginalTags(u"x {\\i1\\u1}U{\\r} x {\\i1\\b1}B{\\r} x");
    ASSERT_EQ(shared.runs.size(), 2u);
    expectRoundTrip(shared, draft(u"y BU y", {{2, 3, 2}, {3, 4, 1}}));
}

TEST(TagTransfer, ADrawingScaleBelowOneIsText)
{
    // libass reads \p as a whole number, 0 and up: \p-1 and \p0.5 draw nothing.
    EXPECT_FALSE(readOriginalTags(u"{\\p-1}a {\\i1}b{\\i0} c").drawing);
    EXPECT_EQ(readOriginalTags(u"{\\p-1}a {\\i1}b{\\i0} c").runs.size(), 1u);
    EXPECT_FALSE(readOriginalTags(u"{\\p0.5}a").drawing);
    EXPECT_TRUE(readOriginalTags(u"{\\p1}m 0 0 l 1 1").drawing);
}

TEST(TagTransfer, SpacesAfterTheBackslashAreSkipped)
{
    // libass (ass_parse.c) skips them before the name.
    const auto tags = readOriginalTags(u"a {\\ i1}b{\\i0} c");
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "b");
    EXPECT_EQ(u8(readOriginalTags(u"a {\\\t pos(1,2)}b").leading), "{\\\t pos(1,2)}");
}

TEST(TagTransfer, TheEncodingIsAWholeNumber)
{
    StyleValues style;
    style.name = u8"Default";
    style.encoding = u8"1";
    const std::vector<StyleValues> styles{style};
    // libass reads \fe with argtoi32: \fe1.5 is the Style's 1.
    EXPECT_TRUE(readOriginalTags(u"a {\\fe1.5}b{\\fe1} c", &style, styles).runs.empty());
    EXPECT_EQ(readOriginalTags(u"a {\\fe2}b{\\fe1} c", &style, styles).runs.size(), 1u);
}

TEST(TagTransfer, NoSuggestionInATextWhoseBracesDontClose)
{
    const auto tags = readOriginalTags(u"I {\\i1}Tokyo{\\i0} x");
    EXPECT_TRUE(suggestPlacements(tags, draft(u"Tokyo {")).empty());
    EXPECT_TRUE(suggestPlacements(tags, draft(u"Tokyo }")).empty());
    EXPECT_EQ(suggestPlacements(tags, draft(u"Tokyo {}")).size(), 1u);
}

TEST(TagTransfer, SpacesAfterATrailingTagStayAfterIt)
{
    // The \h after the reset is at the base size: Paste all keeps it there.
    const std::u16string original = u"{\\fs40}a{\\r}\\h";
    const auto tags = readOriginalTags(original);
    EXPECT_EQ(u8(tags.trailing), "{\\r}\\h");
    const auto pasted = draftOfOriginal(original, tags);
    EXPECT_EQ(u8(pasted.body), "a");
    EXPECT_EQ(u8(composeTranslation(tags, pasted)), u8(original));
    expectRoundTrip(tags, draft(u"b"));
    // Between two trailing blocks too, in their order.
    const auto two = readOriginalTags(u"{\\i1\\b1}a{\\i0}\\N{\\b0} ");
    EXPECT_EQ(u8(two.trailing), "{\\i0}\\N{\\b0} ");
    EXPECT_EQ(u8(composeTranslation(two, draft(u"b"))), "{\\i1\\b1}b{\\i0}\\N{\\b0} ");
    // Spaces before the trailing block are the body's, as before.
    EXPECT_EQ(u8(readOriginalTags(u"{\\i1}a\\h{\\i0}").trailing), "{\\i0}");
}

TEST(TagTransfer, ATransformWithNoTagIsNoRun)
{
    // libass skips a \t with no override tag in it.
    EXPECT_TRUE(readOriginalTags(u"a {\\t()}b{\\r} c").runs.empty());
    EXPECT_TRUE(readOriginalTags(u"a {\\t(0,10,1)}b{\\r} c").runs.empty());
    // Nor with only tags it does not know.
    EXPECT_TRUE(readOriginalTags(u"a {\\t(\\foo1)}b{\\r} c").runs.empty());
    EXPECT_EQ(readOriginalTags(u"a {\\t(\\fs20)}b{\\r} c").runs.size(), 1u);
}

TEST(TagTransfer, AColourTakesOnlyAnUppercaseH)
{
    StyleValues style;
    style.name = u8"Default";
    style.primary = {255, 255, 255, 0};
    const std::vector<StyleValues> styles{style};
    // libass reads past '&' and 'H' only: "&hFFFFFF&" has no digits, black.
    const auto tags = readOriginalTags(u"a {\\c&hFFFFFF&}b{\\c&HFFFFFF&} c", &style, styles);
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].differs.at(u"1c")), "000000");
}

TEST(TagTransfer, TwoRunsAlikeSideBySideStayTwo)
{
    const auto tags = readOriginalTags(u"Say {\\i1}one{\\i0} and {\\i1}two{\\i0}");
    ASSERT_EQ(tags.runs.size(), 2u);
    const auto d = draft(u"xy z", {{0, 1, 1}, {1, 2, 2}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "{\\i1}x{\\i0\\i1}y{\\i0} z");
    expectRoundTrip(tags, d);
    // As in an Original: a tag through another state and back parts them.
    EXPECT_EQ(readOriginalTags(u"a {\\i1}b{\\i0\\i1}c{\\i0} d").runs.size(), 2u);
    EXPECT_EQ(readOriginalTags(u"a {\\i1}b{\\i1}c{\\i0} d").runs.size(), 1u);
}

TEST(TagTransfer, AScaleForTwoOpensTheOneARunChanges)
{
    StyleValues style;
    style.name = u8"Default";
    style.outlineWidth = u8"0";
    const std::vector<StyleValues> styles{style};
    // B is the base's in \xbord: its \bord5 opens as \ybord5 alone.
    const auto tags = readOriginalTags(u"a {\\bord5}A{\\xbord0}B{\\bord0} c", &style, styles);
    ASSERT_EQ(tags.runs.size(), 2u);
    EXPECT_EQ(u8(tags.runs[1].text), "B");
    const auto d = draft(u"x y z", {{2, 3, 2}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "x {\\ybord5}y{\\bord0} z");
    expectRoundTrip(tags, d);
    // Both runs, one after the other: the closing comes first.
    expectRoundTrip(tags, draft(u"x yw z", {{2, 3, 1}, {3, 4, 2}}));
    expectRoundTrip(tags, draft(u"x yw z", {{2, 3, 2}, {3, 4, 1}}));
}

TEST(TagTransfer, SpacesBeforeALeadingTagStayBeforeIt)
{
    // The \h before \fs40 is at the base size: Paste all keeps it there.
    const std::u16string original = u"\\h{\\fs40}a{\\r}";
    const auto tags = readOriginalTags(original);
    EXPECT_EQ(u8(tags.leading), "\\h{\\fs40}");
    const auto pasted = draftOfOriginal(original, tags);
    EXPECT_EQ(u8(pasted.body), "a");
    EXPECT_EQ(u8(composeTranslation(tags, pasted)), u8(original));
    expectRoundTrip(tags, draft(u"b"));
    // Spaces after the last leading tag are the body's, as before.
    const auto after = readOriginalTags(u"{\\fs40}\\h{}a");
    EXPECT_EQ(u8(after.leading), "{\\fs40}");
    EXPECT_EQ(u8(draftOfOriginal(u"{\\fs40}\\h{}a", after).body), "\\ha");
}

TEST(TagTransfer, AColourMayBeSigned)
{
    StyleValues style;
    style.name = u8"Default";
    const std::vector<StyleValues> styles{style};
    // libass reads it with strtoll: -1 is 0xFFFFFFFF, white over black.
    const auto tags = readOriginalTags(u"a {\\c-1}b{\\c} c", &style, styles);
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].differs.at(u"1c")), "FFFFFF");
    EXPECT_EQ(u8(readOriginalTags(u"a {\\1a+FF}b{\\1a} c", &style, styles).runs.at(0).differs.at(u"1a")), "FF");
}

TEST(TagTransfer, LongStyleValuesAndTheirComparisonsAreBounded)
{
    StyleValues style;
    style.name = u8"Default";
    style.fontname = std::u8string(kMaxTagTransferUnits + 1, u8'f');
    std::vector<StyleValues> styles{style};
    EXPECT_TRUE(readOriginalTags(u"a {\\i1}b{\\i0} c", &style, styles).malformed);
    // Under the bound, every tag compared against a long state is charged.
    style.fontname = std::u8string(8000, u8'f');
    styles = {style};
    std::u16string many = u"a {";
    for (int i = 0; i < 200; ++i)
        many += u"\\i0";
    many += u"}b {\\i1}c{\\i0} d";
    EXPECT_TRUE(readOriginalTags(many, &style, styles).malformed);
}

TEST(TagTransfer, RunSettersCopiedAreBounded)
{
    // Two Styles alternating over 700 runs, each run's every component
    // ended by one long spelled \r: each run would copy it per component.
    StyleValues plain, a, b;
    plain.name = u8"Default";
    a.name = u8"A";
    b.name = u8"B";
    for (auto *s : {&a, &b}) {
        s->bold = s->italic = s->underline = s->strikeOut = true;
        s->primary = s->secondary = s->outline = s->back = {1, 2, 3, 0};
        s->scaleX = s->scaleY = u8"120";
        s->spacing = s->angle = s->outlineWidth = s->shadow = u8"3";
    }
    a.fontname = u8"FA";
    a.fontsize = u8"30";
    b.fontname = u8"FB";
    b.fontsize = u8"31";
    const std::vector<StyleValues> styles{plain, a, b};
    std::u16string original = u"x";
    for (int i = 0; i < 700; ++i)
        original += i % 2 ? u"{\\rB}b" : u"{\\rA}a";
    original += u"{\\" + std::u16string(8000, u' ') + u"r}y";
    ASSERT_LE(original.size(), kMaxTagTransferUnits);
    const auto tags = readOriginalTags(original, &plain, styles);
    std::size_t copied = 0;
    for (const auto &run : tags.runs)
        for (const auto *setters : {&run.opens, &run.closes})
            for (const auto &[component, setter] : *setters)
                copied += setter.tag.size();
    EXPECT_TRUE(tags.malformed) << copied;
    // Two runs of them are read.
    original = u"x{\\rA}a{\\rB}b{\\" + std::u16string(8000, u' ') + u"r}y";
    EXPECT_EQ(readOriginalTags(original, &plain, styles).runs.size(), 2u);
}

TEST(TagTransfer, ResetStyleLookupsAreBounded)
{
    const auto numbered = [](std::u16string prefix, std::size_t i) {
        for (const char c : std::to_string(i))
            prefix += static_cast<char16_t>(c);
        return prefix;
    };
    std::vector<StyleValues> styles(4096);
    for (std::size_t i = 0; i < styles.size(); ++i)
        styles[i].name = toUtf8(numbered(u"S", i));
    // A bare \r and one name again and again: looked up once at most.
    std::u16string same = u"a {\\i1}b{\\i0} c";
    for (int i = 0; i < 1000; ++i)
        same += i % 2 ? u"{\\r}x" : u"{\\rGone}x";
    EXPECT_FALSE(readOriginalTags(same, &styles[0], styles).malformed);
    // A name each, none found, in one block before the text: each lookup
    // is charged.
    std::u16string names = u"{";
    for (std::size_t i = 0; i < 1000; ++i)
        names += u"\\r" + numbered(u"Gone", i);
    names += u"}a {\\i1}b{\\i0} c";
    EXPECT_TRUE(readOriginalTags(names, &styles[0], styles).malformed);
}

TEST(TagTransfer, GroupsSetAgainstRunsAreBounded)
{
    const auto numbered = [](std::u16string prefix, std::size_t i) {
        for (const char c : std::to_string(i))
            prefix += static_cast<char16_t>(c);
        return prefix;
    };
    // Runs alike in all but their font, and the translator's own fonts on
    // every group but the last, which is the last run's.
    const auto lines = [&](std::size_t count) {
        std::u16string original = u"w", translation;
        for (std::size_t i = 0; i < count; ++i) {
            original += u"{\\fn" + numbered(u"F", i) + u"}a{\\fn}w";
            translation += i + 1 < count ? u"{\\fn" + numbered(u"G", i) + u"}y{\\fn} " : u"{\\fn" + numbered(u"F", i) + u"}z{\\fn}";
        }
        return std::pair{original, translation};
    };
    const auto [few, fewTranslated] = lines(20);
    const auto fewTags = readOriginalTags(few);
    ASSERT_EQ(fewTags.runs.size(), 20u);
    EXPECT_EQ(readTranslation(fewTags, fewTranslated).placements.size(), 1u);
    // Every group against every run is past the budget: read as plain text.
    const auto [many, manyTranslated] = lines(500);
    const auto manyTags = readOriginalTags(many);
    ASSERT_EQ(manyTags.runs.size(), 500u);
    const auto read = readTranslation(manyTags, manyTranslated);
    EXPECT_TRUE(read.placements.empty());
    EXPECT_TRUE(read.body == manyTranslated);
}

TEST(TagTransfer, RunsSideBySideAcrossTheTranslatorsTagsReadBack)
{
    const auto tags = readOriginalTags(u"x {\\i1}a{\\i0} y {\\b1}b{\\b0} z");
    ASSERT_EQ(tags.runs.size(), 2u);
    // Run 1 on A, run 2 on B, the translator's own \u1 between them.
    const auto d = draft(u"A{\\u1}BC{\\u0}DE", {{0, 1, 1}, {6, 7, 2}});
    const auto saved = composeTranslation(tags, d);
    EXPECT_EQ(u8(saved), "{\\i1}A{\\i0\\b1}{\\u1}B{\\b0}C{\\u0}DE");
    // Read back, run 2 starts where run 1 ends (no text between).
    const auto read = readTranslation(tags, saved);
    EXPECT_EQ(u8(read.body), u8(d.body));
    EXPECT_EQ(read.placements, (std::vector<RunPlacement>{{0, 1, 1}, {1, 7, 2}}));
    EXPECT_EQ(u8(composeTranslation(tags, read)), u8(saved));
}

TEST(TagTransfer, AResetEndingARunKeepsTheBaseItUndid)
{
    // The carried \b1 is the base: the \r ending the underline run undoes
    // it too, so it is set again for what follows.
    const auto tags = readOriginalTags(u"{\\b1}x{\\u1}A{\\r}y{\\b1}z");
    ASSERT_EQ(tags.runs.size(), 2u); // A underlined, y not bold
    ASSERT_EQ(u8(tags.runs[0].text), "A");
    const auto d = draft(u"AB", {{0, 1, 1}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "{\\b1}{\\u1}A{\\r\\b1}B");
    expectRoundTrip(tags, d);
}

TEST(TagTransfer, AResetOpeningARunComesAfterTheClosings)
{
    // Two runs alike opened by \r over a \fs30 base: closing one (\fs30)
    // before the next one's \r, not after it.
    const auto tags = readOriginalTags(u"{\\fs30}x{\\r}A{\\fs30}x{\\r}B{\\fs30}x");
    ASSERT_EQ(tags.runs.size(), 2u);
    const auto d = draft(u"AB", {{0, 1, 1}, {1, 2, 2}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "{\\fs30}{\\r}A{\\fs30\\r}B{\\fs30}");
    expectRoundTrip(tags, d);
}

TEST(TagTransfer, EscapedBracesCarryNothing)
{
    // libass shows "\{" as a brace: no tag is read, Paste all copies it all.
    const std::u16string original = u"Say \\{name\\} {\\i1}x{\\i0} y";
    const auto tags = readOriginalTags(original);
    EXPECT_TRUE(tags.malformed);
    EXPECT_EQ(u8(draftOfOriginal(original, tags).body), u8(original));
}

TEST(TagTransfer, ANamedResetTakesTheLastStyleOfTheName)
{
    StyleValues plain, first, last;
    plain.name = u8"Default";
    first.name = last.name = u8"A";
    last.bold = true;
    const std::vector<StyleValues> styles{plain, first, last};
    // libass's lookup_style: the bold A, so \b0 parts b from c.
    const auto tags = readOriginalTags(u"a {\\rA}b{\\b0}c{\\r} d", &plain, styles);
    ASSERT_EQ(tags.runs.size(), 2u);
    EXPECT_EQ(u8(tags.runs[0].text), "b");
}

TEST(TagTransfer, AnOrderAsPlacedDecidesARunUnderOwnFormatting)
{
    const auto tags = readOriginalTags(u"a {\\i1}b{\\i0} c {\\b1}d{\\b0} e");
    ASSERT_EQ(tags.runs.size(), 2u);
    // Run 2 (bold) on the translator's own italic x: italic and bold, which
    // run 1 could be read under too.
    const auto d = draft(u"{\\i1}x{\\i0} y", {{5, 6, 2}});
    const auto saved = composeTranslation(tags, d);
    const auto read = readTranslation(tags, saved, std::vector<int>{2});
    ASSERT_EQ(read.placements.size(), 1u) << u8(saved);
    EXPECT_EQ(read.placements[0].run, 2);
    EXPECT_EQ(composeTranslation(tags, read), saved);
}

TEST(TagTransfer, TextModeAgainIsNoDrawing)
{
    // \p0 after \p1: the renderer draws nothing, \an8 still applies.
    const auto tags = readOriginalTags(u"{\\p1\\p0\\an8}Hello");
    EXPECT_FALSE(tags.drawing);
    EXPECT_EQ(u8(tags.leading), "{\\p1\\p0\\an8}");
}

TEST(TagTransfer, ALeadingResetEndsWhatItUndid)
{
    // The \t before the reset ends nothing after it: the run on b has no
    // closing and is not offered.
    EXPECT_TRUE(readOriginalTags(u"{\\t(\\fs20)\\r}a {\\t(\\bord2)}b").runs.empty());
}

TEST(TagTransfer, ARunOfSpacesAloneIsNoRun)
{
    // A non-breaking space is no space to hasVisible, but trimmed like one.
    EXPECT_TRUE(readOriginalTags(u"a{\\i1} {\\i0}b").runs.empty());
    EXPECT_TRUE(readOriginalTags(u"a {\\i1}   {\\i0} b").runs.empty());
}

TEST(TagTransfer, ValuesTheRendererClampsOrResetsAreNoRun)
{
    StyleValues style;
    style.name = u8"Default";
    style.fontname = u8"Arial";
    style.outlineWidth = u8"0";
    style.shadow = u8"0";
    const std::vector<StyleValues> styles{style};
    const auto runs = [&](std::u16string_view text) { return readOriginalTags(text, &style, styles).runs.size(); };
    // libass (ass_parse.c): \fn0 is the Style's font, \fsc both its scales.
    EXPECT_EQ(runs(u"a {\\fn0}b{\\fnArial} c"), 0u);
    // No border, \shad or blur below 0, \be rounded to a whole number.
    EXPECT_EQ(runs(u"a {\\bord-1}b{\\bord0} c"), 0u);
    EXPECT_EQ(runs(u"a {\\shad-3}b{\\shad0} c"), 0u);
    EXPECT_EQ(runs(u"a {\\blur-2}b{\\blur0} c"), 0u);
    EXPECT_EQ(runs(u"a {\\be0.4}b{\\be0} c"), 0u);
    EXPECT_EQ(runs(u"a {\\xshad-3}b{\\xshad0} c"), 1u); // \xshad may be below 0
    const auto fsc = readOriginalTags(u"a {\\fscx200}b {\\fsc}c", &style, styles);
    ASSERT_EQ(fsc.runs.size(), 1u);
    EXPECT_EQ(u8(fsc.runs[0].text), "b");
}

TEST(TagTransfer, ManyTransformsStayCheapAndTooManyTagsCarryNothing)
{
    std::u16string many = u"{\\an8}a";
    for (int i = 0; i < 400; ++i)
        many += u"{\\t(0,10,\\fs20)}b";
    many += u"{\\r}c"; // what ends them
    const auto tags = readOriginalTags(many);
    ASSERT_FALSE(tags.malformed);
    // A run opens with every transform still running; one whose 14-unit
    // transforms take more than 1024 units is not offered.
    EXPECT_EQ(tags.runs.size(), 1024u / 14u);
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

TEST(TagTransfer, ARunNothingEndsIsNotOffered)
{
    // No tag after it gives the transform back: placed on a word, it would
    // animate the rest of the translation too.
    EXPECT_TRUE(readOriginalTags(u"a {\\t(\\fs40)}b").runs.empty());
    const auto reset = readOriginalTags(u"a {\\t(\\fs40)}b{\\r} c");
    ASSERT_EQ(reset.runs.size(), 1u);
    EXPECT_EQ(u8(reset.runs[0].text), "b");
    expectRoundTrip(reset, draft(u"x y", {{0, 1, 1}}));
}

TEST(TagTransfer, LongStylesResetOftenCarryNothing)
{
    // A Style's font name has no length bound: each reset to it copies it.
    StyleValues a, b;
    a.name = u8"A";
    b.name = u8"B";
    a.fontname = std::u8string(8000, u8'a');
    b.fontname = std::u8string(8000, u8'b');
    const std::vector<StyleValues> styles{a, b};
    std::u16string text;
    for (int i = 0; i < 500; ++i)
        text += i % 2 ? u"{\\rA}x" : u"{\\rB}x";
    EXPECT_TRUE(readOriginalTags(text, &a, styles).malformed);
    EXPECT_FALSE(readOriginalTags(u"{\\rB}x{\\rA}y", &a, styles).malformed);
}

TEST(TagTransfer, AnEditedOriginalsRunsAreMatchedByFormattingAndText)
{
    const auto before = readOriginalTags(u"Say {\\i1}one{\\i0} now");
    ASSERT_EQ(before.runs.size(), 1u);
    // A run put in before it: the italic run is run 2 now.
    EXPECT_EQ(matchRuns(before, readOriginalTags(u"Say {\\b1}so{\\b0} {\\i1}one{\\i0} now")), std::vector<int>{2});
    // Its formatting changed, or its text: still the run in its place.
    EXPECT_EQ(matchRuns(before, readOriginalTags(u"Say {\\b1}one{\\b0} now")), std::vector<int>{1});
    EXPECT_EQ(matchRuns(before, readOriginalTags(u"Say {\\i1}uno{\\i0} now")), std::vector<int>{1});
    // Both changed, or gone: no run.
    EXPECT_EQ(matchRuns(before, readOriginalTags(u"Say {\\b1}uno{\\b0} now")), std::vector<int>{0});
    EXPECT_EQ(matchRuns(before, readOriginalTags(u"Say one now")), std::vector<int>{0});
    // The first of two alike now has the second's text: each is still the
    // run in its place, the second not taken by the first.
    const auto two = readOriginalTags(u"a{\\i1}one{\\i0}x{\\i1}two{\\i0}z");
    EXPECT_EQ(matchRuns(two, readOriginalTags(u"a{\\i1}two{\\i0}x{\\i1}two{\\i0}z")), (std::vector<int>{1, 2}));
}

TEST(TagTransfer, APlacedRunKeepsTheTranslatorsOwnFormattingAroundIt)
{
    // The run closes with a reset: after it, the translator's own size would go.
    const auto reset = readOriginalTags(u"a{\\fs20}b{\\r}c");
    ASSERT_EQ(reset.runs.size(), 1u);
    EXPECT_TRUE(keepsOwnFormatting(reset, draft(u"{\\fs20}XYZ")));
    EXPECT_EQ(u8(composeTranslation(reset, draft(u"{\\fs20}XYZ", {{8, 9, 1}}))), "{\\fs20}X{\\fs20}Y{\\r}Z");
    EXPECT_FALSE(keepsOwnFormatting(reset, draft(u"{\\fs20}XYZ", {{8, 9, 1}})));
    // On the last text, with nothing drawn after it, the reset changes nothing.
    EXPECT_TRUE(keepsOwnFormatting(reset, draft(u"X{\\u1}Y{\\u0}Z", {{12, 13, 1}})));
    // A closing that sets only the run's own flag keeps the translator's bold.
    const auto italic = readOriginalTags(u"a {\\i1}b{\\i0} c");
    EXPECT_TRUE(keepsOwnFormatting(italic, draft(u"{\\b1}XYZ", {{6, 7, 1}})));
    EXPECT_TRUE(keepsOwnFormatting(italic, draft(u"XYZ", {{1, 2, 1}})));
    // The translator's own \r between two runs side by side undoes the second.
    const auto two = readOriginalTags(u"a{\\fs20}c{\\i1}d{\\r}z");
    ASSERT_EQ(two.runs.size(), 2u);
    EXPECT_FALSE(keepsOwnFormatting(two, draft(u"C{\\r}D", {{0, 1, 1}, {5, 6, 2}})));
}

TEST(TagTransfer, AResetClosesTheWholeLineWrapper)
{
    const auto tags = readOriginalTags(u"{\\i1}Hello{\\r}");
    EXPECT_EQ(u8(tags.leading), "{\\i1}");
    EXPECT_EQ(u8(tags.trailing), "{\\r}");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"Hola"))), "{\\i1}Hola{\\r}");
}

TEST(TagTransfer, TheStylesEncodingAndRelativeSizesAreTheBase)
{
    StyleValues style;
    style.name = u8"Default";
    style.fontsize = u8"20";
    style.encoding = u8"128";
    // \fe128 gives the Style's encoding back: one run, on b.
    const auto encoding = readOriginalTags(u"a{\\fe0}b{\\fe128}c", &style);
    ASSERT_EQ(encoding.runs.size(), 1u);
    EXPECT_EQ(u8(encoding.runs[0].text), "b");
    // libass: \fs+5 is 20 * 1.5, \fs-5 then 30 * 0.5, as \fs15; \fs20 the Style's.
    const auto sizes = readOriginalTags(u"a {\\fs+5}b {\\fs-5}c {\\fs15}d {\\fs20}e", &style);
    ASSERT_EQ(sizes.runs.size(), 2u);
    EXPECT_EQ(u8(sizes.runs[0].text), "b");
    EXPECT_EQ(u8(sizes.runs[1].text), "c d");
    // A size not above 0 is the Style's.
    EXPECT_TRUE(readOriginalTags(u"a {\\fs0}b", &style).runs.empty());
}

TEST(TagTransfer, NoSuggestionsPastTheLengthBound)
{
    const auto tags = readOriginalTags(u"I {\\i1}a{\\i0} b");
    std::u16string body(kMaxTagTransferUnits, u'x');
    body += u" a";
    EXPECT_TRUE(suggestPlacements(tags, draft(body)).empty());
    EXPECT_EQ(suggestPlacements(tags, draft(u"x a")).size(), 1u);
}

// #341: overrides read as libass reads them.

TEST(TagTransfer, ANumberWithNoDigitsIsZero)
{
    // ass_strtod and strtoll read the longest number they can: none is 0,
    // "inf" is none, and what follows the digits is ignored.
    EXPECT_TRUE(readOriginalTags(u"a{\\fspabc}b{\\fsp0}c").runs.empty());
    EXPECT_TRUE(readOriginalTags(u"a{\\fspinf}b{\\fsp0}c").runs.empty());
    EXPECT_TRUE(readOriginalTags(u"a{\\iabc}b{\\i0}c").runs.empty());
    const auto tags = readOriginalTags(u"a {\\fe x}b{\\fe0} c{\\fe} d");
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "b c");
}

TEST(TagTransfer, AnArgumentIsSplitAsLibassSplitsIt)
{
    // In parentheses, the first argument; after the name, spaces only at
    // its end are skipped, so "\fs +5" is a size of 5, not 5 more.
    const auto parenthesised = readOriginalTags(u"a {\\bord(3)}b{\\bord3} c{\\r} d");
    ASSERT_EQ(parenthesised.runs.size(), 1u);
    EXPECT_EQ(u8(parenthesised.runs[0].text), "b c");
    StyleValues style;
    style.name = u8"Default";
    style.fontsize = u8"20";
    const std::vector<StyleValues> styles{style};
    const auto spaced = readOriginalTags(u"a {\\fs30}b {\\fs +5}c{\\fs20} d", &style, styles);
    ASSERT_EQ(spaced.runs.size(), 2u);
    EXPECT_EQ(u8(spaced.runs[1].differs.at(u"fs")), "5");
}

TEST(TagTransfer, ADrawingScaleIsItsWholeNumberPrefix)
{
    // argtoi32 reads "1e-3" as 1: a drawing, nothing carried.
    EXPECT_TRUE(readOriginalTags(u"{\\p1e-3\\an8}m 0 0").drawing);
    EXPECT_TRUE(readOriginalTags(u"{\\p 2}m 0 0").drawing);
}

TEST(TagTransfer, AResetToAStyleTheDocumentLacksIsTheLines)
{
    StyleValues plain, alt;
    plain.name = u8"Default";
    alt.name = u8"Alt";
    alt.italic = true;
    const std::vector<StyleValues> styles{plain, alt};
    EXPECT_TRUE(readOriginalTags(u"a{\\rGone}b{\\r}c", &plain, styles).runs.empty());
    // " Alt", the name with the space after "\r", is no Style either.
    EXPECT_TRUE(readOriginalTags(u"a{\\r Alt}b{\\r}c", &plain, styles).runs.empty());
    EXPECT_EQ(readOriginalTags(u"a{\\rAlt}b{\\r}c", &plain, styles).runs.size(), 1u);
}

TEST(TagTransfer, ASpacedTransformOpensItsRun)
{
    const auto tags = readOriginalTags(u"a{\\ t(\\fs20)\\ t(\\bord2)}B{\\r}c");
    ASSERT_EQ(tags.runs.size(), 1u);
    const auto d = draft(u"xYz", {{1, 2, 1}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "x{\\ t(\\fs20)\\ t(\\bord2)}Y{\\r}z");
    expectRoundTrip(tags, d);
    // A spaced reset is a reset: what both runs share is set again after it.
    const auto shared = readOriginalTags(u"x {\\i1\\u1}U{\\ r} x {\\i1\\b1}B{\\ r} x");
    ASSERT_EQ(shared.runs.size(), 2u);
    const auto both = draft(u"y BU y", {{2, 3, 2}, {3, 4, 1}});
    EXPECT_EQ(u8(composeTranslation(shared, both)), "y {\\i1\\b1}B{\\ r\\i1\\u1}U{\\ r} y");
    expectRoundTrip(shared, both);
}

TEST(TagTransfer, AClipAnimatedAmongOtherTagsGoesToTheFront)
{
    // libass animates the line's clip; only \fs20 is the run's.
    const auto tags = readOriginalTags(u"a{\\t(0,500,\\clip(0,0,100,100)\\fs20)}b{\\r}c");
    EXPECT_EQ(u8(tags.leading), "{\\t(0,500,\\clip(0,0,100,100))}");
    ASSERT_EQ(tags.runs.size(), 1u);
    EXPECT_EQ(u8(tags.runs[0].text), "b");
    EXPECT_EQ(u8(composeTranslation(tags, draft(u"xyz"))), "{\\t(0,500,\\clip(0,0,100,100))}xyz");
    const auto d = draft(u"xyz", {{1, 2, 1}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "{\\t(0,500,\\clip(0,0,100,100))}x{\\t(0,500,\\fs20)}y{\\r}z");
    expectRoundTrip(tags, d);
}

TEST(TagTransfer, AdjacentRunsWriteOnlyTheTransformsTheyAdd)
{
    const auto tags = readOriginalTags(u"a{\\t(\\fs30)}A{\\t(\\bord2)}B{\\r}c");
    ASSERT_EQ(tags.runs.size(), 2u);
    const auto d = draft(u"XY", {{0, 1, 1}, {1, 2, 2}});
    EXPECT_EQ(u8(composeTranslation(tags, d)), "{\\t(\\fs30)}X{\\t(\\bord2)}Y{\\r}");
    expectRoundTrip(tags, d);
    // Transforms that don't go on from the first run's: its reset first.
    const auto reset = readOriginalTags(u"a{\\t(\\fs30)}A{\\r\\t(\\bord2)}B{\\r}c");
    ASSERT_EQ(reset.runs.size(), 2u);
    EXPECT_EQ(u8(composeTranslation(reset, d)), "{\\t(\\fs30)}X{\\r\\t(\\bord2)}Y{\\r}");
    expectRoundTrip(reset, d);
    // Another value's reset ends the first run's transforms too: the
    // second's are all written again after it.
    const auto other = readOriginalTags(u"a{\\t(\\fs30)}B{\\t(\\bord2)}C{\\r}d{\\i1\\t(\\fs30)}A{\\r}z");
    ASSERT_EQ(other.runs.size(), 3u);
    const auto swapped = draft(u"XY", {{0, 1, 3}, {1, 2, 2}});
    EXPECT_EQ(u8(composeTranslation(other, swapped)), "{\\i1\\t(\\fs30)}X{\\r\\t(\\fs30)\\t(\\bord2)}Y{\\r}");
    expectRoundTrip(other, swapped);
}

TEST(TagTransfer, ARunAnEditKeepsFromReadingBackIsNoLongerPlaced)
{
    // The translator's \r, put in between two runs side by side, comes after
    // run 2's opening: B is drawn as the base, so run 2 is placed no longer.
    const auto tags = readOriginalTags(u"a{\\fs30}b{\\b1}c{\\fs20}d{\\r}z");
    ASSERT_EQ(tags.runs.size(), 3u);
    auto d = draft(u"A{\\r}B", {{0, 1, 1}, {4, 5, 2}});
    EXPECT_FALSE(placementHolds(tags, d, std::array{2}));
    keepHeldPlacements(tags, d);
    EXPECT_EQ(d.placements, (std::vector<RunPlacement>{{0, 1, 1}}));
    expectRoundTrip(tags, d);
}
