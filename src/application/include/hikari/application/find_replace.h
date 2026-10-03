#pragma once

// F1: find and replace (legacy GLOBAL_SEARCH, GLOBAL_FIND_REPLACE and
// GLOBAL_FIND_NEXT: FindReplace in findreplace.cpp, FindReplaceDialog's
// TabWindow and FindReplaceResultsDialog at 20d647c4).
//
// FindReplace keeps legacy's state between runs (the row and text position
// of the next search, the last match, the recent lists, the results) and
// walks rows as legacy does. Everything the legacy code shows or asks goes
// through the host, which answers as the user would.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/write_coordinator.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// One tab of the dialog (legacy TabWindow) as the user left it.
struct FindReplaceSettings {
    enum class Tab { Find, Replace, FindInFiles }; // legacy windowType
    enum class Field { Text, Style, Actor, Effect };
    // None: no radio button is checked (SetValues with no Lines bit); it
    // searches like From selected.
    enum class Lines { All, Selected, FromSelection, None };

    Tab tab = Tab::Find;
    std::u8string find;
    std::u8string replace;
    std::u8string styles;  // ChoosenStyleText: style names separated by commas
    std::u8string filters; // FindInSubsPattern
    std::u8string folder;  // FindInSubsPath
    Field field = Field::Text;
    Lines lines = Lines::All;
    bool matchCase = false;
    bool regex = false;
    bool startOfText = false;
    bool endOfText = false;
    bool includeComments = false;
    bool skipTags = false; // OnlyText
    bool skipText = false; // OnlyTags
    bool subfolders = false;
    bool hiddenFolders = false;
};

// Legacy FIND_REPLACE_OPTIONS as the TabWindow constructor reads it: of
// several field or Lines bits the last wins, none leaves Text and All lines
// (the first radio button of each group starts checked).
// The constructor sets "Search in subfolders" from the hidden-folders bit and
// leaves "Search in hidden folders" unchecked (kept legacy quirk).
FindReplaceSettings findReplaceFromOptions(int options);
// TabWindow::SetValues on a tab change: each radio button takes its bit, so
// without a Lines bit none is checked; the two folder boxes keep `current`'s
// values.
FindReplaceSettings findReplaceSetValues(int options, const FindReplaceSettings &current);
// TabWindow::SaveValues: the Find in subtitles tab keeps every bit from 512
// up of `previous` and adds its own; the other tabs start from nothing.
int findReplaceOptions(const FindReplaceSettings &settings, int previous);

// Case folding for searches without "Match case" (legacy wxString::Lower,
// one UTF-16 unit at a time so positions stay put).
using CaseFold = std::function<std::u16string(std::u16string_view)>;

// One row of the results dialog: a header (a Document's name or a file's
// path) or a match.
struct FindResult {
    bool header = false;
    std::u16string text;                  // the header, or the searched text (legacy SeekResults::name)
    int start = 0, length = 0;            // the match in `text`
    std::optional<DocumentId> document;   // a Document's match (legacy tab)
    std::u8string path;                   // a file's match
    int keyLine = 0;                      // the Line's row (in a file: legacy's own count)
    int idLine = 0;                       // the shown "Line N"
    bool translation = false;             // the match is in the translation (legacy isTextTL)
    bool checked = true;                  // legacy Item::modified
    bool visible = true;                  // its header's group is open
};

// What the legacy code shows with HikariMessageBox / HikariMessageDialog.
struct FindQuestion {
    enum class Kind {
        Message,      // information; any answer
        Wrap,         // "Reached end. Search from the beginning?" Yes/No
        Styles,       // some styles missing: Ok (remove nonexistent), Yes (remove styles), No (ignore), Cancel
        NoStyles,     // no style exists: Yes (remove styles), Cancel
        ConfirmFiles, // replace in files: Yes/No
    };
    Kind kind = Kind::Message;
    std::u8string text;
    std::u8string argument; // Message: the phrase; Styles: the missing styles
    std::u8string title;    // the legacy caption
};
enum class FindAnswer { Ok, Yes, No, Cancel };

// A Document the dialog can reach (legacy tab).
struct FindTab {
    DocumentId id;
    EditSession *session = nullptr;
    std::u8string name; // legacy SubsName
};

// The shell around FindReplace (legacy HikariSubFrame, the Grid and the EditBox).
class FindReplaceHost {
public:
    virtual ~FindReplaceHost() = default;
    virtual std::optional<FindTab> current() = 0; // the editing target
    virtual std::vector<FindTab> tabs() = 0;      // every open Document, in order
    // The Lines "Replace all" walks (legacy ignoreFiltered); empty: all.
    virtual LineVisible actionLines(EditSession &) { return {}; }
    virtual FindAnswer ask(const FindQuestion &question) = 0;
    virtual void log(const std::u8string &) {}
    // A match was found: `line` becomes active (and the only selected Line
    // unless `keepSelection`) of `document`, which becomes the editing
    // target; the editor selects [start, end) in the legacy editor `role`:
    // 0 TextEditOrig (the translation mode original), 1 TextEdit (the text,
    // or the translation in translation mode), 2 Actor, 3 Effect, -1 none.
    virtual void showLine(DocumentId document, core::LineId line, bool keepSelection, int role, int start, int end) = 0;
    // A command changed this Document (the editor and Grid show it again).
    virtual void changed(DocumentId) {}
    // Files on disk. The host enumerates (legacy wxDir::GetAllFiles: nullopt
    // for an invalid folder), reads (UTF-8, CRLF read as LF, BOM removed;
    // nullopt when unreadable or empty), backs up and writes (UTF-8 with BOM).
    virtual std::optional<std::vector<std::u8string>> listFiles(const std::u8string &folder,
                                                                const std::u8string &filter, bool subfolders,
                                                                bool hidden) = 0;
    virtual std::optional<std::u16string> readFile(const std::u8string &path) = 0;
    virtual bool fileExists(const std::u8string &path) = 0;
    virtual void backupFile(const std::u8string &path) = 0;
    virtual void writeFile(const std::u8string &path, const std::u16string &text) = 0;
    // ShowResult for a file: the open Document with this path, or the file opened.
    virtual std::optional<DocumentId> openFile(const std::u8string &path) = 0;
};

class FindReplace {
public:
    explicit FindReplace(FindReplaceHost &host, CaseFold fold = {});

    // The recent lists (legacy FIND_RECENT_FINDS, REPLACE_RECENT_REPLACEMENTS,
    // FIND_IN_SUBS_FILTERS_RECENT, FIND_IN_SUBS_PATHS_RECENT): cut to 20 when loaded.
    struct Recent {
        std::vector<std::u8string> finds, replacements, filters, paths;
    };
    void setRecent(Recent recent);
    const Recent &recent() const { return m_recent; }

    // The dialog's buttons; `window` is the tab as the user left it (its
    // styles may change through the styles question).
    void find(FindReplaceSettings *window);          // Find, Enter (legacy Find + fnext = false)
    void findNext();                                 // GLOBAL_FIND_NEXT (F3)
    void findAllInCurrent(FindReplaceSettings &window); // "Find all in current subtitles"
    void findInAllOpened(FindReplaceSettings &window);  // "Find in all open subtitles"
    void replace(FindReplaceSettings &window);       // "Replace next"
    void replaceAll(FindReplaceSettings &window);    // "Replace all"
    void replaceInAllOpened(FindReplaceSettings &window); // "Replace in all open subtitles"
    void findInFiles(FindReplaceSettings &window);   // "Find in subtitles"
    void replaceInFiles(FindReplaceSettings &window);    // "Replace in subtitles" (asks first)
    // A radio button of the Lines box (TabWindow::Reset) and FindReplaceDialog::Reset.
    void reset();
    // The dialog activates with a selection in the editor (OnActivate): the
    // next search starts over from row 0 unless it is the last match.
    void selectionAdopted();
    int lastStart() const { return m_findstart; }
    int lastEnd() const { return m_findend; }

    // The results dialog (FindReplaceResultsDialog).
    const std::vector<FindResult> &results() const { return m_results; }
    bool resultsShown() const { return m_resultsShown; }
    bool canReplaceChecked() const { return m_replaceCheckedEnabled; }
    void checkAll(bool check);                       // Check all / Uncheck all
    void toggleChecked(std::size_t row);             // a checkbox: a header checks its group
    void toggleGroup(std::size_t header);            // a header's text: shows or hides its group
    void showResult(std::size_t row);                // double click
    // "Replace" with the results dialog's replacement text.
    void replaceChecked(const std::u8string &replacement);

private:
    struct TextMatch {
        int position = 0;
        int length = 0;
    };
    bool updateValues(const FindReplaceSettings &window);
    bool checkStyles(FindReplaceSettings &window, const EditSession &session);
    bool keepFinding(std::u16string_view text, int textPos) const;
    std::u16string element(const core::LineRecord &line, bool translationMode) const;
    std::u16string lower(std::u16string_view s) const;
    // FindInSubsLine: every match of one Line's text, as results.
    void findInLine(const std::u16string &text, std::u16string_view lineText, std::optional<DocumentId> document,
                    bool *isFirst, int linePos, int linePosId, const std::u8string &header, bool translationMode);
    int replaceInLine(std::u16string &text) const; // ReplaceInSubsLine
    int replaceCheckedLine(std::u16string &line, int position, int length, int *diff) const;
    void findAllInTab(const FindTab &tab, bool allLines, bool selectedOnly);
    int replaceAllInTab(const FindTab &tab, bool allLines, bool selectedOnly);
    void findReplaceInFiles(FindReplaceSettings &window, bool find);
    void findReplaceInFile(const std::u8string &path, bool find, int &replacements);
    int replaceCheckedInFile(const std::vector<const FindResult *> &results);
    void addRecent(const FindReplaceSettings &window);
    void clearResults();

    FindReplaceHost &m_host;
    CaseFold m_fold;
    Recent m_recent;

    // Legacy FindReplace members.
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
    // UpdateValues.
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
    std::optional<std::wregex> m_regex; // findReplaceRegEx; kept until the next regular expression
    // FindReplaceResultsDialog.
    std::vector<FindResult> m_results;
    bool m_resultsShown = false;
    bool m_resultsInFiles = false; // findInFiles: set by a files search, never cleared
    bool m_replaceCheckedEnabled = true;
    bool m_resultsRegEx = false, m_resultsMatchCase = false, m_resultsNeedPrefix = false;
    std::u16string m_resultsFindString;
};

namespace find_replace_detail {
// wxRegEx::Replace (wxWidgets src/common/regex.cpp at the legacy pin): "\N"
// and "&" insert groups, "\" escapes the next character, a trailing "\"
// stays; after the first match "^" does not match (wxRE_NOTBOL). Returns
// the number of replacements.
int regexReplace(const std::wregex &re, std::u16string &text, std::u16string_view replacement, int maxMatches);
} // namespace find_replace_detail

} // namespace hikari::application
