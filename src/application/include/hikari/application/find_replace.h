#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/write_coordinator.h"
#include "hikari/core/legacy_regex.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct FindReplaceSettings {
    enum class Tab { Find, Replace, FindInFiles };
    enum class Field { Text, Style, Actor, Effect };
    // None: no radio button checked; searches like FromSelection.
    enum class Lines { All, Selected, FromSelection, None };

    Tab tab = Tab::Find;
    std::u8string find;
    std::u8string replace;
    std::u8string styles;  // style names separated by commas
    std::u8string filters;
    std::u8string folder;
    Field field = Field::Text;
    Lines lines = Lines::All;
    bool matchCase = false;
    bool regex = false;
    bool startOfText = false;
    bool endOfText = false;
    bool includeComments = false;
    bool skipTags = false;
    bool skipText = false;
    bool subfolders = false;
    bool hiddenFolders = false;
};

// Of several field or Lines bits the last wins; none gives Text and All.
// MUST KILL: the hidden-folders bit sets subfolders and hidden folders stays off
FindReplaceSettings findReplaceFromOptions(int options);
// Each radio button takes its bit, so without a Lines bit none is checked;
// the folder boxes keep `current`'s values.
FindReplaceSettings findReplaceSetValues(int options, const FindReplaceSettings &current);
// The FindInFiles tab keeps the bits from 512 up of `previous`; the other
// tabs start from nothing.
int findReplaceOptions(const FindReplaceSettings &settings, int previous);

// Must map one UTF-16 unit to one so positions stay put.
using CaseFold = std::function<std::u16string(std::u16string_view)>;

// A header (a Document's name or a file's path) or a match.
struct FindResult {
    bool header = false;
    std::u16string text;                  // the header, or the searched text
    int start = 0, length = 0;            // the match in `text`
    std::optional<DocumentId> document;   // a Document's match
    std::u8string path;                   // a file's match
    int keyLine = 0;                      // the Line's row (in a file, as HikariSub legacy counts)
    int idLine = 0;                       // the shown "Line N"
    bool translation = false;             // the match is in the translation
    bool checked = true;
    bool visible = true;                  // its header's group is open
};

struct FindQuestion {
    enum class Kind {
        Message,      // information; nothing waits for it
        Wrap,         // "Reached end. Search from the beginning?" Yes/No
        Styles,       // some styles missing: Ok (remove nonexistent), Yes (remove styles), No (ignore), Cancel
        NoStyles,     // no style exists: Yes (remove styles), Cancel
        ConfirmFiles, // replace in files: Yes/No
    };
    // For the shell's translated text.
    enum class Info { None, NotFound, Replaced, InvalidPath };
    Kind kind = Kind::Message;
    std::u8string text;     // English
    std::u8string argument; // NotFound: the phrase; Replaced: the count; Styles: the missing styles
    std::u8string title;    // English
    Info info = Info::None;
    // Styles and NoStyles: the Document's tab name and the missing styles;
    // ConfirmFiles and InvalidPath: the search folder.
    std::u8string document;
    std::vector<std::u8string> missing;
    std::u8string folder;
};
enum class FindAnswer { Ok, Yes, No, Cancel };

// English text, and what the host needs to translate it.
struct FindLog {
    enum class Kind {
        InvalidRegex,      // "Invalid regular expression '%s': %s" (pattern, message)
        MatchError,        // "Failed to find match for regular expression: %s" (message)
        LineEdited,        // "Line %i cannot be replaced,\ncause it was edited." (the line number)
        CannotBackUp,      // "Cannot back up %s." (the path)
        OutsideReplaceTab,
    };
    Kind kind;
    std::u8string text;
    std::u8string first;
    std::u8string second;
};

struct FindTab {
    DocumentId id;
    EditSession *session = nullptr;
    std::u8string name;
};

class FindReplaceHost {
public:
    virtual ~FindReplaceHost() = default;
    virtual std::optional<FindTab> current() = 0; // the editing target
    virtual std::vector<FindTab> tabs() = 0;      // every open Document, in order
    // The Lines "Replace all" walks; empty: all.
    virtual LineVisible actionLines(EditSession &) { return {}; }
    // Must not block. A question passes `answer`, to be called once (at once
    // or later); FindReplace is busy() until then. A message passes an empty
    // `answer`.
    virtual void ask(const FindQuestion &question, std::function<void(FindAnswer)> answer) = 0;
    virtual void log(const std::u8string &) {}
    // A log line; by default its English text goes to log() above.
    virtual void logLine(const FindLog &line) { log(line.text); }
    // The recent lists changed for this tab.
    virtual void recentAdded(FindReplaceSettings::Tab) {}
    // `line` becomes active (and the only selected Line unless
    // `keepSelection`) of `document`, the new editing target; the editor
    // selects [start, end) in `role`: 0 the translation mode original, 1 the
    // text (the translation in translation mode), 2 Actor, 3 Effect, -1 none.
    virtual void showLine(DocumentId document, core::LineId line, bool keepSelection, int role, int start, int end) = 0;
    // A command changed this Document (the editor and Grid show it again).
    virtual void changed(DocumentId) {}
    // An operation (a button, F3, Replace checked) has finished, also after
    // its questions were answered: window() and the results are final.
    virtual void finished() {}
    // listFiles: nullopt for an invalid folder. readFile: the detected
    // charset, BOM removed, CR LF kept on Linux and read as LF on Windows (as
    // HikariSub legacy reads it); nullopt when unreadable or empty.
    // writeFile: UTF-8 with BOM.
    virtual std::optional<std::vector<std::u8string>> listFiles(const std::u8string &folder,
                                                                const std::u8string &filter, bool subfolders,
                                                                bool hidden) = 0;
    virtual std::optional<std::u16string> readFile(const std::u8string &path) = 0;
    virtual bool fileExists(const std::u8string &path) = 0;
    virtual bool backupFile(const std::u8string &path) = 0;
    virtual bool writeFile(const std::u8string &path, const std::u16string &text) = 0;
    // Slices for replacing in files.
    virtual int processorCount() { return 4; }
    // `done` gets the open Document with this path, else the file opened,
    // else the editing target when that open was cancelled or failed, or
    // nullopt. FindReplace is busy() until it answers.
    virtual void openFile(const std::u8string &path, std::function<void(std::optional<DocumentId>)> done) = 0;
};

class FindReplace {
public:
    explicit FindReplace(FindReplaceHost &host, CaseFold fold = {});

    // Cut to 20 when loaded.
    struct Recent {
        std::vector<std::u8string> finds, replacements, filters, paths;
    };
    void setRecent(Recent recent);
    const Recent &recent() const { return m_recent; }

    // Every call is ignored while busy().
    void find(const FindReplaceSettings &window);
    void findNext();                                       // F3
    void findAllInCurrent(const FindReplaceSettings &window);
    void findInAllOpened(const FindReplaceSettings &window);
    void replace(const FindReplaceSettings &window);
    void replaceAll(const FindReplaceSettings &window);
    void replaceInAllOpened(const FindReplaceSettings &window);
    void findInFiles(const FindReplaceSettings &window);
    void replaceInFiles(const FindReplaceSettings &window); // asks first
    bool busy() const { return m_waiting; }
    // The tab after the last operation; the styles question may change its styles.
    const FindReplaceSettings &window() const { return m_window; }
    // On a Lines radio button, the dialog's Reset or a new editing target.
    void reset();
    // The dialog activated with a selection in the editor: the next search
    // starts over from row 0 unless it is the last match.
    void selectionAdopted();
    int lastStart() const { return m_findstart; }
    int lastEnd() const { return m_findend; }

    const std::vector<FindResult> &results() const { return m_results; }
    bool resultsShown() const { return m_resultsShown; }
    bool canReplaceChecked() const { return m_replaceCheckedEnabled; }
    void checkAll(bool check);
    void toggleChecked(std::size_t row);             // a header checks its group
    void toggleGroup(std::size_t header);            // shows or hides its group
    void showResult(std::size_t row);                // a file may wait for the save question
    void replaceChecked(const std::u8string &replacement);

private:
    using Found = std::function<void(bool found)>;
    using Then = std::function<void(bool cancelled)>;
    bool begin(const FindReplaceSettings *window);
    void finish();
    void ask(const FindQuestion &question, std::function<void(FindAnswer)> then);
    void tell(const FindQuestion &question);
    std::optional<FindTab> tabById(DocumentId id);
    bool updateValues(const FindReplaceSettings &window);
    bool compile(std::u16string_view pattern);
    void checkStyles(DocumentId document, Then then);
    bool keepFinding(std::u16string_view text, int textPos) const;
    bool search(std::u16string_view text, int &start, int &length) const;
    int regexReplace(std::u16string &text, std::u16string_view replacement, std::size_t maxMatches) const;
    std::u16string element(const core::LineRecord &line, bool translationMode) const;
    std::u16string lower(std::u16string_view s) const;
    // `withWindow`: the dialog's tab, else the last values (F3).
    void runFind(bool withWindow, Found done);
    void seekFromStart(DocumentId document, bool withWindow, Found done);
    void seekLines(DocumentId document, bool withWindow, Found done);
    void endFind(bool withWindow, bool found, const Found &done);
    bool matchStillThere(const EditSession &session);
    void replaceFound(bool found);
    void findInLine(const std::u16string &text, std::u16string_view lineText, std::optional<DocumentId> document,
                    bool *isFirst, int linePos, int linePosId, const std::u8string &header, bool translationMode);
    int replaceInLine(std::u16string &text) const;
    int replaceCheckedLine(std::u16string &line, int position, int length, int *diff) const;
    void findAllInTab(const FindTab &tab, bool allLines, bool selectedOnly);
    int replaceAllInTab(const FindTab &tab, bool allLines, bool selectedOnly);
    void showResultIn(const FindTab &tab, const FindResult &result);
    void findReplaceInFiles(bool find);
    // reachedWrite: false for an .ass file without dialogues.
    struct FileOutcome {
        bool reachedWrite = false;
        int replacements = 0;
    };
    FileOutcome findReplaceInFile(const std::u8string &path, bool find);
    int replaceCheckedInFile(const std::vector<const FindResult *> &results);
    void addRecent(const FindReplaceSettings &window);
    void clearResults();

    FindReplaceHost &m_host;
    CaseFold m_fold;
    Recent m_recent;
    FindReplaceSettings m_window; // the tab of the running (or last) operation
    bool m_waiting = false;       // a question waits for its answer

    int m_linePosition = 0;
    int m_reprow = 0;
    int m_textPosition = 0;
    int m_findstart = -1;
    int m_findend = -1;
    int m_lastActive = 0;
    std::u16string m_oldfind;
    bool m_fromstart = true;
    bool m_wasResetToStart = false;
    bool m_wasIgnored = false;
    std::u16string m_stylesAsText;
    // Replace next replaces the last match only while it is still there.
    bool m_matchValid = false;
    std::optional<DocumentId> m_matchDocument;
    std::optional<core::LineId> m_matchLine;
    std::u16string m_matchText;
    FindReplaceSettings::Field m_field = FindReplaceSettings::Field::Text;
    bool m_matchCase = false;
    bool m_regEx = false;
    bool m_startLine = false;
    bool m_endLine = false;
    bool m_skipComments = true;
    bool m_onlyText = false;
    bool m_onlyOption = false;
    bool m_allLines = false;
    bool m_selectedLines = false;
    std::u16string m_findString;
    std::u16string m_replaceString;
    std::optional<core::LegacyRegex> m_regex;
    std::vector<FindResult> m_results;
    bool m_resultsShown = false;
    // MUST KILL: set by a files search, never cleared
    bool m_resultsInFiles = false;
    bool m_replaceCheckedEnabled = true;
    bool m_resultsRegEx = false, m_resultsMatchCase = false, m_resultsNeedPrefix = false;
    std::u16string m_resultsFindString;
};

}
