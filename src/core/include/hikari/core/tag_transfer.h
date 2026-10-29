#pragma once

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
    // False for a closing the Original did not write after the run: left out at the translation's end.
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
    // Spaces, \h or \N before a leading tag and after a trailing one, which the blocks carry.
    std::size_t leadingSpaces = 0, trailingSpaces = 0;
    std::vector<TagRun> runs;
    TagState defaults; // the Style's values, and the renderer's where it has none
    TagState base;     // the state at the first visible character
    // The values a named reset (\rName) gives, for the Styles the text names.
    std::map<std::u16string, TagState> named;
    bool karaoke = false;   // \k tags were left out
    bool drawing = false;   // nothing carried
    // Nothing carried: unbalanced braces, past kMaxTagTransferUnits / kMaxTagTransferTags, or resets
    // naming Styles that would copy too much to read.
    bool malformed = false;
    bool operator==(const OriginalTags &) const = default;
};

// A text past either is read as having no runs (an Original: malformed).
inline constexpr std::size_t kMaxTagTransferUnits = 16384; // UTF-16 code units
inline constexpr std::size_t kMaxTagTransferTags = 1024;
// A Document with more Styles carries nothing.
inline constexpr std::size_t kMaxTagTransferStyles = 4096;

// leading: tags before the first visible character plus every line-wide tag (\pos \move \org \fad
// \fade \clip \iclip \an \a \q); trailing: tags after the last visible character that undo leading
// ones; runs: stretches whose formatting differs from the base, numbered from 1. Brace comments,
// \N, karaoke tags and a run the Original never ends are not carried; nor is anything from a
// drawing or a malformed text. `styles`: the Document's, for named resets (the first of a name).
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

// The Original's text (\N and \h kept, brace comments left out), each run placed on its own words.
TranslationDraft draftOfOriginal(std::u16string_view original, const OriginalTags &tags);

// The saved translation; empty for an empty body. `placed`, when given,
// receives where each placement's text landed in it.
std::u16string composeTranslation(const OriginalTags &tags, const TranslationDraft &draft,
                                  std::vector<RunPlacement> *placed = nullptr);
// The draft that composes back to `translation` exactly; formatting that matches no run stays in
// the body. `order` (runOrder as last composed) decides a span two runs could be read as.
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
// Whether the composed translation draws the body's own formatting as written, with each placed
// run's on top. A body with no tags keeps it.
bool keepsOwnFormatting(const OriginalTags &tags, const TranslationDraft &draft);
// An empty translation has none, nor one whose own leading or trailing tags differ.
bool hasUnplacedRuns(const OriginalTags &tags, std::u16string_view translation);
// Indexed by `before`'s run number - 1: the `now` run it became, or 0. Same formatting and text
// first; else, with as many runs, the one in its place with its formatting or text unchanged.
std::vector<int> matchRuns(const OriginalTags &before, const OriginalTags &now);
// The run numbers of `placements` in text order, as composeTranslation wrote them.
std::vector<int> runOrder(std::span<const RunPlacement> placements);
// Equal-formatting runs read back as the first unused one; `order` (runOrder of the placements
// last composed) restores the translator's numbers where each still names a run of that formatting.
void relabelRuns(const OriginalTags &tags, TranslationDraft &draft, std::span<const int> order);
// Whether each of `runs` reads back over the same text from the composed translation, and the
// translator's own formatting around them draws as it did.
bool placementHolds(const OriginalTags &tags, const TranslationDraft &draft, std::span<const int> runs);
// Removes the placements that no longer hold after a body edit; their tags are not written.
void keepHeldPlacements(const OriginalTags &tags, TranslationDraft &draft);

}
