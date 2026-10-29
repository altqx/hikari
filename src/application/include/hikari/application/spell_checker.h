#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/core/spelling.h"
#include "hikari/core/spelling_scripts.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hikari::application {

// A word the dictionary's encoding cannot hold is misspelled.
class SpellingBackend {
public:
    virtual ~SpellingBackend() = default;
    virtual bool spell(std::u16string_view word) = 0;
    virtual std::vector<std::u16string> suggest(std::u16string_view word) = 0;
    virtual void add(std::u16string_view word) = 0;    // runtime only
    virtual bool remove(std::u16string_view word) = 0; // runtime only
};
// nullptr when it cannot load.
using SpellingBackendLoader =
    std::function<std::unique_ptr<SpellingBackend>(const std::filesystem::path &aff, const std::filesystem::path &dic)>;

struct SpellingText {
    core::legacy::WordSegmenter segment;
    core::legacy::CaseMapping cases;
};

// The symbols (file names without extension) of the .dic files with a same-named .aff.
std::vector<std::u16string> availableDictionaries(const std::filesystem::path &folder);
// Folders in search order, each paired on its own; a symbol is listed once.
std::vector<std::u16string> availableDictionaries(const std::vector<std::filesystem::path> &folders);

class SpellChecker {
public:
    enum class Status {
        Ready,
        NoDictionary,
        LoadFailed,
    };
    // `folders` in search order; the first holds UserDic.udic.
    SpellChecker(std::vector<std::filesystem::path> folders, SpellingBackendLoader loader);
    SpellChecker(std::filesystem::path folder, SpellingBackendLoader loader);

    // en_US when `language` is empty. Other installed dictionaries load on a worker thread the first time a
    // word of their script is checked; the loaded handler runs on that thread after each load.
    Status initialize(std::u16string_view language);
    void setLoadedHandler(std::function<void()> loaded) { m_loaded = std::move(loaded); }
    void clear();
    bool ready() const { return !m_dictionaries.empty() && m_dictionaries.front().backend; }

    // True without a dictionary, when a dictionary of the word's scripts accepts it, or when one of its
    // scripts has none. While one is loading, an unaccepted word is true and not cached, unless `wait`.
    bool checkWord(std::u16string_view word, bool wait = false);
    // Empty without a dictionary.
    core::legacy::WordCheck wordCheck(bool wait = false);
    // The primary's first, then the script's other dictionaries, those accepting more of `neighbours` first;
    // each suggestion once. Waits for the script's dictionaries to load.
    std::vector<std::u16string> suggestions(std::u16string_view word,
                                            const std::vector<std::u16string> &neighbours = {});
    // Refused for empty and number words or without a dictionary; appends to UserDic.udic.
    bool addWord(std::u16string_view word);
    // Needs no dictionary; appends to UserDic.udic and to a loaded dictionary. Refused for empty and number words.
    bool addUserWord(std::u16string_view word);
    // Removes from UserDic.udic and the dictionary; false when none matched.
    bool removeWords(const std::vector<std::u16string> &words);
    std::vector<std::u16string> addedWords() const;

    const std::filesystem::path &folder() const { return m_folders.front(); }
    const std::vector<std::filesystem::path> &folders() const { return m_folders; }
    std::filesystem::path userDictionary() const { return m_folders.front() / "UserDic.udic"; }

private:
    struct Dictionary {
        std::u16string symbol;
        std::filesystem::path aff;
        std::filesystem::path dic;
        core::spelling::Scripts scripts;
        std::unique_ptr<SpellingBackend> backend;
        bool loaded = false; // backend is null when the load failed
        std::future<std::unique_ptr<SpellingBackend>> pending;
        std::uint64_t generation = 0; // m_userGeneration when the load started
        std::jthread worker;
    };
    enum class Load { Ready, Loading, Missing };
    std::optional<std::pair<std::filesystem::path, std::filesystem::path>> findPair(std::u16string_view symbol) const;
    Load poll(Dictionary &dictionary, bool wait);
    void startLoad(Dictionary &dictionary);
    // The primary first; `loading` is set when one is still loading.
    std::vector<Dictionary *> dictionariesFor(core::spelling::Scripts scripts, bool wait, bool *loading = nullptr);

    std::vector<std::filesystem::path> m_folders;
    SpellingBackendLoader m_loader;
    std::vector<Dictionary> m_dictionaries; // the primary first
    std::function<void()> m_loaded;
    // A load that started before the latest user dictionary change starts again.
    std::uint64_t m_userGeneration = 0;
    // Cleared whenever a dictionary changes.
    std::unordered_map<std::u16string, bool> m_results;
};

// Written as UTF-8 with a BOM; reading drops the BOM (and CRLF on Windows). nullopt when unreadable or empty.
std::optional<std::u16string> readUserDictionary(const std::filesystem::path &file);
bool writeUserDictionary(const std::filesystem::path &file, std::u16string_view text);

// Letter words outside override blocks and drawings.
std::vector<std::u16string> lineWords(std::u16string_view text, core::SubtitleFormat format,
                                      const SpellingText &spelling);

// True in translation mode, where the translation is spell-checked instead of the text.
bool spellsTranslation(const core::Document &document);

// Bracket errors and misspellings of the spell-checked field; `wait` as in SpellChecker::checkWord.
core::legacy::SpellMarks editorMarks(std::u16string_view text, core::SubtitleFormat format, SpellChecker &checker,
                                     const SpellingText &spelling, bool wait = false);
// The first misspelling whose span holds `at`.
std::optional<std::size_t> misspellAt(const core::legacy::SpellMarks &marks, int at);

// Replaces in the Line's draft (or committed text) and commits both as one undo step; returns the caret after it.
std::expected<int, CommandRefusal> replaceInEditor(EditSession &session, core::LineId line, bool translation,
                                                   const core::legacy::Misspell &misspell,
                                                   std::u16string_view replacement);

// [start, end) is in core::grammar::checkedText's offsets of `checked`; tags inside stay in order; one undo
// step. StaleRevision when the field no longer holds `checked`, Invalid when the replacement would touch a
// drawing or a malformed span. Returns the caret in raw offsets.
std::expected<std::size_t, CommandRefusal> replaceGrammar(EditSession &session, core::LineId line, bool translation,
                                                          std::u16string_view checked, std::size_t start,
                                                          std::size_t end, std::u16string_view replacement);

// Walks every Line from the active one for the next misspelling not ignored; each correction is one undo step.
class SpellCheckWalk {
public:
    struct Options {
        bool ignoreComments = false;
        bool ignoreUpperCase = false;
    };
    // Unchanged: refused before acting.
    enum class Result { Unchanged, Replaced, NothingReplaced };
    struct Found {
        core::LineId line;
        std::u16string word;
        int start = 0; // raw offsets in the Line's spell-checked text, inclusive
        int end = 0;
        std::vector<std::u16string> suggestions;
    };

    SpellCheckWalk(SpellChecker &checker, SpellingText spelling);

    // A new Line becomes active and selected; hidden Lines are walked too. When the session refuses to
    // leave the active Line, nothing is current and refused() is set.
    bool next(EditSession &session, const Options &options);
    const std::optional<Found> &current() const { return m_current; }
    bool refused() const { return m_refused; }

    // Restarts when the active Line or its text changed; returns whether it did. The first activation after
    // nothing was found is ignored.
    bool activated(EditSession &session, const Options &options, bool otherDocument = false);
    // Whether activated() would restart; actions check it before acting on a word no longer shown.
    bool stale(EditSession &session, bool otherDocument = false) const;
    void restart(EditSession &session, const Options &options);

    // Unchanged when the replacement is empty or no word is current.
    std::expected<Result, CommandRefusal> replace(EditSession &session, std::u16string_view replacement,
                                                  const Options &options);
    // Every Line's matches of `misspell` in any case, each in the case of the word it replaces; Unchanged when
    // either word is empty.
    std::expected<Result, CommandRefusal> replaceAll(EditSession &session, std::u16string_view misspell,
                                                     std::u16string_view replacement, const Options &options);
    void ignore(EditSession &session, const Options &options);
    void ignoreAll(EditSession &session, std::u16string_view word, const Options &options);
    // Then moves to the next word.
    bool addWord(EditSession &session, std::u16string_view word, const Options &options);

private:
    std::u16string findNextMisspell(EditSession &session, const Options &options);
    std::u16string lineText(const core::Document &document, std::size_t row) const;

    SpellChecker &m_checker;
    SpellingText m_spelling;
    std::size_t m_lastLine = 0;
    std::size_t m_lastMisspell = 0;
    std::ptrdiff_t m_lastActiveLine = -1;
    bool m_blockOnActive = false;
    bool m_refused = false;
    std::vector<std::u16string> m_ignored;
    std::vector<core::legacy::Misspell> m_errors;
    std::u16string m_lastText;
    std::optional<Found> m_current;
};

}
