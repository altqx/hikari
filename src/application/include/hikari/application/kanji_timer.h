#pragma once

#include "hikari/application/audio_karaoke.h"
#include "hikari/application/furigana.h"
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

// One romaji syllable: its text without override blocks, its time in
// centiseconds and its \k tag's letters (k, kf, ko or K).
struct KanjiSyllable {
    std::u16string text;
    int cs = 0;
    std::u16string tag;
};

// A \k, \kf, \ko or \K tag with a time.
bool hasKaraokeTags(std::u16string_view text);

std::vector<KanjiSyllable> kanjiSyllables(std::u16string_view text, int startMs, int endMs);

// Counts taken from where the previous group stopped; a blank group has
// syllables and no characters.
struct KanjiGroup {
    std::size_t syllables = 0, characters = 0;
    bool operator==(const KanjiGroup &) const = default;
};

// The kanji Line's text without its \k tags (a block they leave empty goes too)
// and its units: graphemes outside override blocks (\N \n \h one each).
// A syllable whose text is "#" or "＃" (karaskel's extra highlight) goes with its
// tag. `tagged`: the groups its tags make, when no character precedes the first
// tag. `readings`: its furigana (漢|かん, #|ん, full-width forms), in hiragana.
struct KanjiText {
    struct Unit {
        std::size_t begin = 0, end = 0;
        std::u16string text; // without the blocks inside it
    };
    struct Reading {
        std::size_t begin = 0, end = 0;
        std::u16string text;
        bool operator==(const Reading &) const = default;
    };
    std::u16string text;
    std::vector<Unit> units;
    std::vector<KanjiGroup> tagged;
    std::vector<Reading> readings;
};
KanjiText kanjiText(std::u16string_view text);

// The group starting at syllable `from` and character `at`. `ranOut`: the
// characters ran out with more than one syllable left, all in this group.
KanjiGroup suggestGroup(const std::vector<KanjiSyllable> &syllables, std::size_t from, const KanjiText &kanji,
                        std::size_t at, const KaraokeCharClass &classes, bool *ranOut = nullptr);

// Groups to the last syllable. `clean`: no group is blank or ran out, and no
// character is left after them.
struct KanjiSuggestion {
    std::vector<KanjiGroup> groups;
    bool clean = true;
};
KanjiSuggestion suggestGroups(const std::vector<KanjiSyllable> &syllables, std::size_t from, const KanjiText &kanji,
                              std::size_t at, const KaraokeCharClass &classes);

// Typed readings by a run's units (empty: no furigana there).
using KanjiTyped = std::map<std::pair<std::size_t, std::size_t>, std::u16string>;

// A run of kanji within a group and its reading: the typed one, else the
// suggestion (the group's sung kana split over its runs, else the dictionary's).
struct KanjiReading {
    std::size_t group = 0;
    std::size_t begin = 0, end = 0; // its units
    std::u16string suggested;       // hiragana, empty: none
    std::u16string dictionary;      // empty: none
    bool sung = false;              // the suggestion is the sung kana
    bool differs = false;           // sung other than the dictionary reads it
    std::optional<std::u16string> typed;
    // Units a Line's own reading spells when it splits over none of the runs
    // (none: 0, 0).
    std::size_t spellsFrom = 0, spellsTo = 0;
    const std::u16string &reading() const { return typed ? *typed : suggested; }
    bool operator==(const KanjiReading &) const = default;
};

// `dictionary`: the kanji Line's runs as suggestReadings reads them; a word read
// over more runs is compared with their readings together. `suggest` off: only
// the typed readings.
std::vector<KanjiReading> kanjiReadings(const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                                        const std::vector<KanjiGroup> &groups,
                                        const std::vector<FuriganaRun> &dictionary, const KanjiTyped &typed,
                                        bool suggest = true);

// The kanji Line's text with `groups` as \k tags: each before its group's first
// character, with its first syllable's letters and the sum of its times; a blank
// group's goes before the next group's. Characters after the last group join it,
// and the Line's other tags stay. `multi`: further syllables become # syllables.
// `readings`: each group's reading follows it as base|reading.
std::u16string kanjiOutput(const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                           const std::vector<KanjiGroup> &groups, bool multi = false,
                           const std::vector<KanjiReading> *readings = nullptr);

// Keeps its own undo, apart from the Document's.
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

    // The romaji and kanji styles' Lines in Document order (with text; romaji with
    // \k tags; no templater fx Lines). Call again after every Document change: a
    // done pair whose Lines changed shows Changed.
    void setLines(std::vector<Line> romaji, std::vector<Line> kanji);
    // On by default; not an undo step.
    void setSuggest(bool on);
    bool suggest() const { return m_suggest; }
    // Off: each group folded into one tag.
    void setMulti(bool on) { m_multi = on; }
    bool multi() const { return m_multi; }
    // Off: only the readings typed or already in the Line. The dictionary suggests
    // readings where the romaji is no kana.
    void setFurigana(bool on) { m_furigana = on; }
    bool furigana() const { return m_furigana; }
    void setDictionary(ReadingAnalyzer analyze);
    bool hasDictionary() const { return static_cast<bool>(m_analyze); }

    const std::vector<Pair> &pairs() const { return m_pairs; }
    // The current pair (-1: none with both Lines).
    int current() const { return m_current; }
    bool choose(int pair);
    bool previous();

    struct View {
        std::vector<KanjiSyllable> syllables;
        KanjiText kanji;
        std::vector<KanjiGroup> linked;
        KanjiGroup selection;           // the current group, after the linked ones
        std::vector<KanjiGroup> rest;   // the suggestion after it
        bool canAccept = false;         // every syllable in a group, none of the suggested ones blank
        std::optional<std::u16string> output; // the kanji Line it would get
        // With Furigana on: the runs of the linked groups, the selection and the rest.
        std::vector<KanjiReading> readings;
    };
    std::optional<View> view() const;

    // Each that changes something is an undo step. enter() links, accepting the Line
    // once every syllable is linked; acceptLine() takes the rest of the suggestion.
    bool growSyllables();
    bool shrinkSyllables();
    bool growCharacters();
    bool shrinkCharacters();
    bool link();
    bool unlink();
    bool enter();
    bool acceptLine();
    // The current group's reading (split at spaces over its kanji runs), or its
    // suggestion again. Undo steps.
    bool setReading(std::u16string_view reading);
    bool useSuggestion();
    // A click: the current group's syllables or characters end at this one;
    // on a linked group, that group and the ones after it are unlinked.
    bool clickSyllable(std::size_t syllable);
    bool clickCharacter(std::size_t character);
    // The current pair's Line is paired with none; the others pair again without it.
    bool skipRomaji();
    bool skipKanji();
    // Every pair whose suggestion is clean is done.
    int acceptAllSuggestions();

    bool canUndo() const { return m_undo > 0; }
    bool canRedo() const { return m_undo < m_steps.size(); }
    bool undo();
    bool redo();

    // The done pairs' kanji Lines and their new texts; applied() marks them Applied
    // once the Document has them and clears the undo.
    std::vector<std::pair<core::LineId, std::u16string>> pending() const;
    void applied();

private:
    struct Work {
        std::vector<KanjiGroup> linked;
        std::optional<KanjiGroup> selection; // none: the suggestion's first group
        bool done = false, applied = false, changed = false;
        KanjiTyped typed;
        std::u16string romajiText, kanjiText; // the texts it was made on
        bool operator==(const Work &) const = default;
    };
    using Key = std::pair<std::uint64_t, std::uint64_t>;
    struct State {
        std::set<core::LineId> skippedRomaji, skippedKanji;
        std::map<Key, Work> works;
        std::optional<Key> current;
    };
    struct Step {
        std::map<Key, std::pair<std::optional<Work>, std::optional<Work>>> works;
        std::optional<core::LineId> skippedRomaji, skippedKanji;
        std::optional<Key> currentBefore, currentAfter;
        std::size_t bytes = 0;
    };

    static Key keyOf(const Pair &pair);
    void derive();
    void begin();
    // MUST KILL: touch() is workFor that also records the Work for the open step
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
    const std::vector<FuriganaRun> &dictionaryOf(const KanjiText &kanji) const;
    std::vector<KanjiReading> readingsOf(const Work &work, const std::vector<KanjiSyllable> &syllables,
                                         const KanjiText &kanji, const std::vector<KanjiGroup> &groups) const;
    std::u16string outputOf(const Work &work, const std::vector<KanjiSyllable> &syllables, const KanjiText &kanji,
                            const std::vector<KanjiGroup> &groups) const;
    bool editCurrent(const std::function<bool(Work &, const std::vector<KanjiSyllable> &, const KanjiText &)> &edit);
    bool acceptWork(const Pair &pair, Work &work, bool all);
    void moveOnFrom(int pair);

    KaraokeCharClass m_classes;
    bool m_suggest = true;
    bool m_multi = false;
    bool m_furigana = false;
    ReadingAnalyzer m_analyze;
    mutable std::map<std::u16string, std::vector<FuriganaRun>> m_dictionary; // by kanji text
    std::vector<Line> m_romaji, m_kanji;
    State m_state;
    std::vector<Pair> m_pairs;
    int m_current = -1;
    std::optional<Step> m_open;
    std::vector<Step> m_steps;
    // MUST KILL: m_undo counts the steps done
    std::size_t m_undo = 0;
    std::size_t m_stepBytes = 0;
};

} // namespace hikari::application
