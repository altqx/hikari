#pragma once

// KA3 (#278): the Kanji timer, Aegisub's DialogKanjiTimer
// (dialog_kara_timing_copy.cpp, libaegisub karaoke_matcher.cpp) over the
// whole song. Qt-free. The romaji Line's syllables are KA1's (AudioKaraoke::
// splitTags), its \k timing goes to the kanji Line a group at a time: a group
// is some syllables and the kanji characters they sing, its time their sum.
// Owner-approved departures (compatibility-decisions.md): KA3-whole-song,
// KA3-pair-by-time, KA3-suggest-whole-line, KA3-apply, KA3-keep-tags,
// KA3-leftover-kanji, KA3-go-back, KA8-multi-highlight.

#include "hikari/application/audio_karaoke.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

// One syllable of the romaji Line: its text without override blocks, its
// time in centiseconds (as its \k tag wrote it) and the tag's letters (k,
// kf, ko or K).
struct KanjiSyllable {
    std::u16string text;
    int cs = 0;
    std::u16string tag;
};

// Whether a Line's text has a \k, \kf, \ko or \K tag with a time.
bool hasKaraokeTags(std::u16string_view text);

// The romaji Line's syllables as KA1's model reads a tagged Line.
std::vector<KanjiSyllable> kanjiSyllables(std::u16string_view text, int startMs, int endMs);

// A group: how many syllables and characters it takes, from where the
// previous one stopped. A blank group has syllables and no characters.
struct KanjiGroup {
    std::size_t syllables = 0, characters = 0;
    bool operator==(const KanjiGroup &) const = default;
};

// The kanji Line: its text without its \k tags (an override block they
// leave empty goes too) and its characters, graphemes outside override
// blocks (\N \n \h one each), where they start and end in that text.
// KA8-multi-highlight: a # syllable (a tag after the first whose text up to
// the next tag is "#" or "＃", karaskel's one more highlight of the syllable
// before it) goes with its tag. `tagged`: when the Line has one and no
// character before its first tag, the groups its tags make, each tag's
// characters and its syllables (itself and the # ones after it).
struct KanjiText {
    struct Unit {
        std::size_t begin = 0, end = 0;
        std::u16string text; // without the blocks inside it
    };
    std::u16string text;
    std::vector<Unit> units;
    std::vector<KanjiGroup> tagged;
};
KanjiText kanjiText(std::u16string_view text);

// Aegisub's AutoMatchKaraoke: the group starting at syllable `from` and
// character `at`. The first syllable against the characters (equal letters,
// then kana by their readings); stuck at a kanji, the next kana within three
// characters looked for at the start of a later syllable; when the
// characters run out, the syllables left all go to this group (`ranOut`
// when that is more than one: Aegisub's "the user did something wrong").
KanjiGroup suggestGroup(const std::vector<KanjiSyllable> &syllables, std::size_t from, const KanjiText &kanji,
                        std::size_t at, const KaraokeCharClass &classes, bool *ranOut = nullptr);

// suggestGroup one group after another to the last syllable. `clean`: no
// group of it is blank or ran out of characters, and no character is left
// after it.
struct KanjiSuggestion {
    std::vector<KanjiGroup> groups;
    bool clean = true;
};
KanjiSuggestion suggestGroups(const std::vector<KanjiSyllable> &syllables, std::size_t from, const KanjiText &kanji,
                              std::size_t at, const KaraokeCharClass &classes);

// The kanji Line's text with `groups` (every syllable in one) as \k tags:
// each group's tag before its first character, the letters of its first
// syllable's tag and the sum of its syllables' times; a blank group's before
// the next group's. KA3-leftover-kanji: characters after the last group are
// in it. KA3-keep-tags: the Line's other tags stay; a tag goes into the
// Line's own block just before it. KA8-multi-highlight (`multi`): a group of
// more syllables and some characters takes its first syllable's time, and
// each further syllable is a # syllable after its characters with its own
// time and letters.
std::u16string kanjiOutput(const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                           const std::vector<KanjiGroup> &groups, bool multi = false);

// The Kanji timer window's state over a Document: the Line pairs of the two
// styles, each pair's grouping, the current pair and the timer's own undo.
class KanjiTimer {
public:
    struct Line {
        core::LineId id;
        std::u16string text;
        int startMs = 0, endMs = 0;
    };
    enum class Status { Suggested, Check, ToDo, Done, Applied, Changed, NoKanji, NoRomaji };
    struct Pair {
        std::optional<Line> romaji, kanji;
        Status status = Status::ToDo;
    };

    explicit KanjiTimer(KaraokeCharClass classes = KaraokeCharClass::ascii());

    // The Lines of the romaji and the kanji style in Document order (Lines
    // with text; a romaji Line with \k tags; no fx Line of the templater).
    // Read again after every Document change: a pair whose Lines changed
    // after it was done loses its grouping and shows Changed. A kanji Line
    // with # syllables (KanjiText::tagged) starts with its groups linked.
    void setLines(std::vector<Line> romaji, std::vector<Line> kanji);
    // Suggest groupings (on by default); not an undo step.
    void setSuggest(bool on);
    bool suggest() const { return m_suggest; }
    // KA8-multi-highlight: the kanji Lines written with # syllables (off:
    // each group folded into one tag, as Aegisub writes them).
    void setMulti(bool on) { m_multi = on; }
    bool multi() const { return m_multi; }

    const std::vector<Pair> &pairs() const { return m_pairs; }
    // The current pair (-1: none with both Lines).
    int current() const { return m_current; }
    bool choose(int pair);
    bool previous();

    // The current pair's grouping.
    struct View {
        std::vector<KanjiSyllable> syllables;
        KanjiText kanji;
        std::vector<KanjiGroup> linked;
        KanjiGroup selection;           // the current group, after the linked ones
        std::vector<KanjiGroup> rest;   // the suggestion after it
        bool canAccept = false;         // every syllable in a group, none of the suggested ones blank
        std::optional<std::u16string> output; // the kanji Line it would get
    };
    std::optional<View> view() const;

    // Aegisub's keys: Up and Down the selection's syllables, Right and Left
    // its characters, Enter links (and accepts the Line once every syllable
    // is linked), Backspace unlinks; Ctrl+Enter accepts the Line with the
    // rest of the suggestion. Each that changes something is an undo step.
    bool growSyllables();
    bool shrinkSyllables();
    bool growCharacters();
    bool shrinkCharacters();
    bool link();
    bool unlink();
    bool enter();
    bool acceptLine();
    // A click: the current group's syllables or characters end at this one;
    // on a linked group, that group and the ones after it are unlinked.
    bool clickSyllable(std::size_t syllable);
    bool clickCharacter(std::size_t character);
    // Skip romaji line / Skip kanji line: the current pair's Line is paired
    // with none, and the others pair again without it.
    bool skipRomaji();
    bool skipKanji();
    // Every pair whose suggestion is clean is done.
    int acceptAllSuggestions();

    bool canUndo() const { return m_undo > 0; }
    bool canRedo() const { return m_undo < m_steps.size(); }
    bool undo();
    bool redo();

    // KA3-apply: the kanji Lines of the done pairs and their new texts;
    // applied() marks them Applied once the Document has them.
    std::vector<std::pair<core::LineId, std::u16string>> pending() const;
    void applied();

private:
    struct Work {
        std::vector<KanjiGroup> linked;
        std::optional<KanjiGroup> selection; // none: the suggestion's first group
        bool done = false, applied = false, changed = false;
        std::u16string romajiText, kanjiText; // the texts it was made on
        bool operator==(const Work &) const = default;
    };
    using Key = std::pair<std::uint64_t, std::uint64_t>;
    struct State {
        std::set<core::LineId> skippedRomaji, skippedKanji;
        std::map<Key, Work> works;
        std::optional<Key> current;
    };
    // An undo step: only what it changed, before and after (a whole State
    // for each step would be the texts of every pair again and again).
    struct Step {
        std::map<Key, std::pair<std::optional<Work>, std::optional<Work>>> works;
        std::optional<core::LineId> skippedRomaji, skippedKanji;
        std::optional<Key> currentBefore, currentAfter;
        std::size_t bytes = 0;
    };

    static Key keyOf(const Pair &pair);
    void derive();
    // An edit: begin() opens its step, touch() is workFor with the Work as
    // it was kept for the step, commit() records it, rollback() undoes it.
    void begin();
    Work &touch(const Pair &pair);
    void commit();
    void rollback();
    void replay(const Step &step, bool forward);
    Work &workFor(const Pair &pair);
    KanjiGroup selectionOf(const Work &work, const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                           bool *ranOut = nullptr) const;
    std::vector<KanjiGroup> proposal(const Work &work, const std::vector<KanjiSyllable> &syllables,
                                     const KanjiText &kanji, bool *clean = nullptr) const;
    Status statusOf(const Pair &pair);
    bool editCurrent(const std::function<bool(Work &, const std::vector<KanjiSyllable> &, const KanjiText &)> &edit);
    bool acceptWork(const Pair &pair, Work &work, bool all);
    void moveOnFrom(int pair);

    KaraokeCharClass m_classes;
    bool m_suggest = true;
    bool m_multi = false;
    std::vector<Line> m_romaji, m_kanji;
    State m_state;
    std::vector<Pair> m_pairs;
    int m_current = -1;
    std::optional<Step> m_open;
    std::vector<Step> m_steps;
    std::size_t m_undo = 0; // the steps done
    std::size_t m_stepBytes = 0;
};

} // namespace hikari::application
