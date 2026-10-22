#pragma once

// Tags from the original (TM1 #232; docs/research/translation-tag-transfer.md
// section 7). In Translation mode the Original text keeps its override tags
// and the translator types only the translation.
//
// readOriginalTags splits an Original text into what goes to the translation
// with no help from the translator, and the runs the translator places:
//   - leading: every tag before the first visible character, plus each
//     line-wide tag (\pos \move \org \fad \fade \clip \iclip \an \a \q) from
//     anywhere in the line, as one block;
//   - trailing: tags after the last visible character that undo leading
//     tags (a whole-line wrapper's closing tags);
//   - runs: each stretch of visible text whose formatting differs from the
//     line's base, numbered from 1 in reading order. Run boundaries come from
//     formatting state, not from tag names.
// Brace comments, \N and karaoke tags are not carried, nor a run nothing in
// the Original ends (a \t never reset). A drawing or malformed Original text
// carries nothing.
//
// The saved translation is plain ASS: the leading block, the typed body with
// each placed run's tags around its text, and the trailing block. Placements
// are read back from it; nothing else is stored.

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core {

struct StyleValues;

// Override property -> normalised value ("i" -> "1", "1c" -> "0000FF").
using TagState = std::map<std::u16string, std::u16string>;

struct TagSetter {
    std::size_t order = 0; // the tag's position among the Original text's tags
    std::u16string tag;    // its spelling, "\i1"; empty when nothing can undo it
    // False for a closing the Original did not write after the run (the
    // leading spelling, or an empty reset): left out at the translation's end.
    bool written = true;
    bool operator==(const TagSetter &) const = default;
};

struct TagRun {
    int number = 0;
    std::u16string text;                          // its visible text, trimmed
    std::size_t displayStart = 0, displayEnd = 0; // in project(original).text, trimmed
    std::size_t rawStart = 0, rawEnd = 0;         // in the Original text, untrimmed
    TagState differs;                             // where it differs from the base
    std::map<std::u16string, TagSetter> opens;    // the tag that sets each value
    std::map<std::u16string, TagSetter> closes;   // the tag that restores the base
    bool operator==(const TagRun &) const = default;
};

struct OriginalTags {
    std::u16string leading, trailing; // "{...}" or empty
    // Units at the text's start before a leading tag, and at its end after a
    // trailing one (spaces, a \h, a \N): the leading and trailing blocks
    // carry them, where they were among their tags.
    std::size_t leadingSpaces = 0, trailingSpaces = 0;
    std::vector<TagRun> runs;
    TagState defaults; // the Style's values, and the renderer's where it has none
    TagState base;     // the state at the first visible character
    // The values a named reset (\rName) gives, for the Styles the text names.
    std::map<std::u16string, TagState> named;
    bool karaoke = false;   // \k tags were left out
    bool drawing = false;   // nothing carried
    // Nothing carried: unbalanced braces, or a text longer or with more tags
    // than kMaxTagTransferUnits / kMaxTagTransferTags, or whose formatting
    // (the Styles its resets name) would copy too much to read.
    bool malformed = false;
    bool operator==(const OriginalTags &) const = default;
};

// A text past either is read as having no runs (an Original: malformed).
inline constexpr std::size_t kMaxTagTransferUnits = 16384; // UTF-16 code units
inline constexpr std::size_t kMaxTagTransferTags = 1024;
// A Document with more Styles carries nothing: each Line's runs are read
// against them all (named resets), and the Grid reads every Line's.
inline constexpr std::size_t kMaxTagTransferStyles = 4096;

// `style` is the Line's Style; `styles` the Document's, for named resets
// (legacy GetStyle: the first of a name).
OriginalTags readOriginalTags(std::u16string_view original, const StyleValues *style = nullptr,
                              std::span<const StyleValues> styles = {});

struct RunPlacement {
    std::size_t start = 0, end = 0; // UTF-16 offsets in the draft's body
    int run = 0;
    bool operator==(const RunPlacement &) const = default;
};

struct TranslationDraft {
    std::u16string body;                  // what the translator typed: raw ASS, no carried tags
    std::vector<RunPlacement> placements; // sorted and disjoint
    // False when a saved translation's own leading or trailing tags differ
    // from the original's: the body is then the whole saved text.
    bool carried = true;
};

// The Original text as a draft: its text (\N and \h kept, brace comments
// left out) with each run placed on its own words. Paste all starts from it.
TranslationDraft draftOfOriginal(std::u16string_view original, const OriginalTags &tags);

// The saved translation; empty for an empty body. `placed`, when given,
// receives where each placement's text landed in it.
std::u16string composeTranslation(const OriginalTags &tags, const TranslationDraft &draft,
                                  std::vector<RunPlacement> *placed = nullptr);
// The draft that composes back to `translation` exactly. Formatting that
// matches no run stays in the body as the translator's own tags.
// `order`, the runs placed by position as last composed (runOrder), decides
// a span two runs could be read as under the translator's own formatting.
TranslationDraft readTranslation(const OriginalTags &tags, std::u16string_view translation,
                                 std::span<const int> order = {});

// Moves placements for an edit that replaced the body's [start, end) with
// `inserted` UTF-16 units. Text typed at a run's start joins it; text typed
// at its end does not. Replaced text that began a run keeps it.
void shiftPlacements(std::vector<RunPlacement> &placements, std::size_t start, std::size_t end,
                     std::size_t inserted);
// Places `run` on [start, end): a run is placed once, so an earlier placement
// moves. The same selection again removes it. Other runs it overlaps are
// trimmed. False for an empty selection.
bool placeRun(std::vector<RunPlacement> &placements, std::size_t start, std::size_t end, int run);
// The lowest-numbered run not placed, or 0.
int nextUnplacedRun(const OriginalTags &tags, std::span<const RunPlacement> placements);
// The first run that sets `property` ("i", "b", "u"), preferring unplaced ones, or 0.
int runWithProperty(const OriginalTags &tags, std::span<const RunPlacement> placements,
                    std::u16string_view property);
// Placements for unplaced runs whose text appears once in the body, as a
// whole word, outside every placement.
std::vector<RunPlacement> suggestPlacements(const OriginalTags &tags, const TranslationDraft &draft);
// Whether the draft's composed translation draws its text as the body's
// own formatting does, with each placed run's on top: a run closed with a
// reset (or opened under the translator's own \r) would otherwise change
// the translator's formatting around it. A body with no tags keeps it.
bool keepsOwnFormatting(const OriginalTags &tags, const TranslationDraft &draft);
// Whether a saved translation leaves a run of the Original unplaced (the
// Grid's "runs not placed" mark). An empty translation has none, and so has
// one whose own leading or trailing tags differ (the translator's, shown whole).
bool hasUnplacedRuns(const OriginalTags &tags, std::u16string_view translation);
// For an Original text edited from `before` to `now`: the run of `now` each
// run of `before` became (index number - 1), or 0 where none did. A run is
// the one with the same formatting and text; failing that, with the runs as
// many as before, the one in its place when its formatting or its text is
// unchanged. One in its own place with both unchanged comes first. Run numbers alone are positions: a run put in before the others
// renumbers them.
std::vector<int> matchRuns(const OriginalTags &before, const OriginalTags &now);
// The run numbers of `placements` in text order, as composeTranslation wrote them.
std::vector<int> runOrder(std::span<const RunPlacement> placements);
// Runs with the same formatting read back as the first unused of them, so a
// draft read from saved text can name the wrong one of two equal runs.
// `order` (runOrder of the placements last composed) puts back the numbers
// the translator chose, where each still names a run of the same formatting;
// otherwise the draft is left as read.
void relabelRuns(const OriginalTags &tags, TranslationDraft &draft, std::span<const int> order);

} // namespace hikari::core
