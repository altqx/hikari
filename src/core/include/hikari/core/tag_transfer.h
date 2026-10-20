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
// Brace comments, \N and karaoke tags are not carried. A drawing or malformed
// Original text carries nothing.
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
};

struct TagRun {
    int number = 0;
    std::u16string text;                          // its visible text, trimmed
    std::size_t displayStart = 0, displayEnd = 0; // in project(original).text, trimmed
    std::size_t rawStart = 0, rawEnd = 0;         // in the Original text, untrimmed
    TagState differs;                             // where it differs from the base
    std::map<std::u16string, TagSetter> opens;    // the tag that sets each value
    std::map<std::u16string, TagSetter> closes;   // the tag that restores the base
};

struct OriginalTags {
    std::u16string leading, trailing; // "{...}" or empty
    std::vector<TagRun> runs;
    TagState defaults; // the Style's values, and the renderer's where it has none
    TagState base;     // the state at the first visible character
    bool karaoke = false;   // \k tags were left out
    bool drawing = false;   // nothing carried
    bool malformed = false; // nothing carried
};

OriginalTags readOriginalTags(std::u16string_view original, const StyleValues *style = nullptr);

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
TranslationDraft readTranslation(const OriginalTags &tags, std::u16string_view translation);

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
// Whether a saved translation leaves a run of the Original unplaced (the
// Grid's "runs not placed" mark). An empty translation has none, and so has
// one whose own leading or trailing tags differ (the translator's, shown whole).
bool hasUnplacedRuns(const OriginalTags &tags, std::u16string_view translation);

} // namespace hikari::core
