#pragma once

// F3: the spell checker (legacy SpellChecker, SpellCheckerDialog and the
// TextEditor's spelling at 20d647c4: GLOBAL_OPEN_SPELLCHECKER, the editor's
// marks, suggestions and "Add word" and the Grid's marks) over a spelling
// backend port. Legacy loads Hunspell with the "Dictionary" folder's
// <language>.aff/.dic pair; the backend that does so is supplied by the
// composition (none ships yet), and tests use an in-memory fake.

#include "hikari/application/edit_session.h"
#include "hikari/core/spelling.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hikari::application {

// One loaded dictionary (legacy Hunspell). A word the dictionary's encoding
// cannot hold is misspelled, as legacy's failed conversion is.
class SpellingBackend {
public:
    virtual ~SpellingBackend() = default;
    virtual bool spell(std::u16string_view word) = 0;
    virtual std::vector<std::u16string> suggest(std::u16string_view word) = 0;
    virtual void add(std::u16string_view word) = 0;    // runtime only, as Hunspell::add
    virtual bool remove(std::u16string_view word) = 0; // runtime only, as Hunspell::remove
};
// Loads <folder>/<language>.aff and .dic; nullptr when it cannot.
using SpellingBackendLoader =
    std::function<std::unique_ptr<SpellingBackend>(const std::filesystem::path &aff, const std::filesystem::path &dic)>;

// What the UI supplies for the text walks: legacy's boost::locale word
// segmentation and the C runtime's per-character case functions.
struct SpellingText {
    core::legacy::WordSegmenter segment;
    core::legacy::CaseMapping cases;
};

// SpellChecker::AvailableDics: the symbols (file names without extension)
// of the folder's .dic files, each paired with the .aff file at the same
// position in the folder's listing (legacy pairs by position, not by name).
std::vector<std::u16string> availableDictionaries(const std::filesystem::path &folder);

class SpellChecker {
public:
    enum class Status {
        Ready,
        NoDictionary, // "No dictionary files were found ... Spell checking will be disabled"
        LoadFailed,   // "Failed to initialize spell checker."
    };
    SpellChecker(std::filesystem::path folder, SpellingBackendLoader loader);

    // SpellChecker::Initialize: DICTIONARY_LANGUAGE (en_US when empty), then
    // the user dictionary's words.
    Status initialize(std::u16string_view language);
    void clear(); // Cleaning
    bool ready() const { return m_backend != nullptr; }

    // CheckWord: true without a dictionary (spell checking off).
    bool checkWord(std::u16string_view word);
    // The word check the text walks use; empty without a dictionary.
    core::legacy::WordCheck wordCheck();
    std::vector<std::u16string> suggestions(std::u16string_view word);
    // AddWord: refused for empty and number words or without a dictionary;
    // appends the word to UserDic.udic.
    bool addWord(std::u16string_view word);
    // RemoveWords: the lines equal to a listed word leave UserDic.udic and the
    // dictionary; false when none matched.
    bool removeWords(const std::vector<std::u16string> &words);
    // LoadAddedMisspels: the lines "Remove from dictionary" lists.
    std::vector<std::u16string> addedWords() const;

    const std::filesystem::path &folder() const { return m_folder; }
    std::filesystem::path userDictionary() const { return m_folder / "UserDic.udic"; }

private:
    std::filesystem::path m_folder;
    SpellingBackendLoader m_loader;
    std::unique_ptr<SpellingBackend> m_backend;
    // Results by word, dropped whenever the dictionary changes (legacy wordResults).
    std::unordered_map<std::u16string, bool> m_results;
};

// The user dictionary file: UTF-8 with a byte-order mark (OpenWrite::FileWrite);
// read as legacy's text-mode read does (BOM dropped, CRLF read as LF).
std::optional<std::u16string> readUserDictionary(const std::filesystem::path &file);
bool writeUserDictionary(const std::filesystem::path &file, std::u16string_view text);

// The Line text the spell checker works on (legacy TextEdit): the
// translation in translation mode, else the text.
bool spellsTranslation(const core::Document &document);

// The editor's marks (TextEditor::CheckText through TextData::Init2): bracket
// errors and misspelled words of the spell-checked field.
core::legacy::SpellMarks editorMarks(std::u16string_view text, core::SubtitleFormat format, SpellChecker &checker,
                                     const SpellingText &spelling);
// TextEditor::FindError: the first misspelling whose span holds `at`.
std::optional<std::size_t> misspellAt(const core::legacy::SpellMarks &marks, int at);

// A suggestion chosen in the editor (its menu or the double-click list):
// the misspelling is replaced in the Line's draft (or committed text), and
// the draft and the replacement are committed as one "Correcting spelling
// errors in the text field" step (legacy EditBox::Send(EDITBOX_SPELL_CHECKER)).
// Returns the caret after the replacement.
std::expected<int, CommandRefusal> replaceInEditor(EditSession &session, core::LineId line, bool translation,
                                                   const core::legacy::Misspell &misspell,
                                                   std::u16string_view replacement);

// The Spellchecker window (SpellCheckerDialog): walks every Line from the
// active one for the next misspelled word that is not ignored, and corrects
// it there or everywhere, each as one "Correcting spelling errors" step.
class SpellCheckWalk {
public:
    struct Options {
        bool ignoreComments = false;
        bool ignoreUpperCase = false; // "Ignore words written entirely in uppercase"
    };
    struct Found {
        core::LineId line;
        std::u16string word;
        int start = 0; // raw offsets in the Line's spell-checked text, inclusive
        int end = 0;
        std::vector<std::u16string> suggestions;
    };

    SpellCheckWalk(SpellChecker &checker, SpellingText spelling);

    // SetNextMisspell. The found word's Line becomes active and selected when
    // it is not the Line of the previous word. Nothing found: "No spelling
    // errors were found".
    bool next(EditSession &session, const Options &options);
    const std::optional<Found> &current() const { return m_current; }

    // OnActive: when the window comes back and the active Line or its text
    // changed, the walk starts again from the active Line's first word.
    // Returns whether it did; the first activation after "No spelling errors
    // were found" is ignored.
    bool activated(EditSession &session, const Options &options, bool otherDocument = false);

    // Replace (button, Enter or a double-clicked suggestion). False when
    // the replacement is empty or no word is current.
    std::expected<bool, CommandRefusal> replace(EditSession &session, std::u16string_view replacement,
                                                const Options &options);
    // Replace all: every Line's matches of `misspell` in any case, each
    // replaced in the case of the word it replaces (GetRightCase).
    std::expected<bool, CommandRefusal> replaceAll(EditSession &session, std::u16string_view misspell,
                                                   std::u16string_view replacement, const Options &options);
    void ignore(EditSession &session, const Options &options);
    void ignoreAll(EditSession &session, std::u16string_view word, const Options &options);
    // Add to dictionary, then the next word.
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
    std::vector<std::u16string> m_ignored;
    std::vector<core::legacy::Misspell> m_errors;
    std::u16string m_lastText;
    std::optional<Found> m_current;
};

} // namespace hikari::application
