#include "hikari/application/find_replace.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/application/select_lines.h"
#include "hikari/core/clipboard_rows.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace hikari::application {

namespace {

using Settings = FindReplaceSettings;
using Field = FindReplaceSettings::Field;
using Tab = FindReplaceSettings::Tab;

// FindReplace.h option bits.
enum : int {
    kCaseSensitive = 1,
    kRegEx = 2,
    kStartOfText = 4,
    kEndOfText = 8,
    kInFieldText = 16,
    kInFieldStyle = 64,
    kInFieldActor = 128,
    kInFieldEffect = 256,
    kInLinesAll = 512,
    kInLinesSelected = 1024,
    kInLinesFromSelection = 2048,
    kSearchSubfolders = 4096,
    kSearchHiddenFolders = 8192,
    kSeekInComments = 1 << 14,
    kSeekOnlyInText = 1 << 15,
    kSeekOnlyInTags = 1 << 16,
};

constexpr auto npos = std::u16string::npos;

std::u16string u16(std::u8string_view s)
{
    return core::toUtf16(s);
}

std::u8string u8(std::u16string_view s)
{
    return core::toUtf8(s);
}

std::u16string number(long long value)
{
    const std::string s = std::to_string(value);
    return {s.begin(), s.end()};
}

std::u16string asciiFold(std::u16string_view s)
{
    std::u16string out(s);
    for (auto &c : out)
        if (c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c - u'A' + u'a');
    return out;
}

// wxString::Trim (wxSafeIsspace: ASCII white space only).
bool isSpace(char16_t c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\v' || c == u'\f' || c == u'\r';
}

std::u16string trimRight(std::u16string s)
{
    while (!s.empty() && isSpace(s.back()))
        s.pop_back();
    return s;
}

std::u16string trimBoth(std::u16string s)
{
    s = trimRight(std::move(s));
    std::size_t i = 0;
    while (i < s.size() && isSpace(s[i]))
        ++i;
    return s.substr(i);
}

// wxString::Mid: an offset past the end gives "".
std::u16string mid(std::u16string_view s, std::size_t from, std::size_t count = npos)
{
    return from >= s.size() ? std::u16string() : std::u16string(s.substr(from, count));
}

// wxStringTokenizer(text, "\n", wxTOKEN_STRTOK): no empty tokens.
std::vector<std::u16string> lineTokens(std::u16string_view text)
{
    std::vector<std::u16string> out;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t j = text.find(u'\n', i);
        if (j == npos)
            j = text.size();
        if (j > i)
            out.emplace_back(text.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// wxStringTokenizer(filters, ";") (wxTOKEN_RET_EMPTY: empty tokens inside,
// none after a trailing delimiter).
std::vector<std::u8string> filterTokens(std::u8string_view filters)
{
    std::vector<std::u8string> out;
    std::size_t from = 0;
    while (from < filters.size()) {
        std::size_t to = filters.find(u8';', from);
        if (to == std::u8string_view::npos)
            to = filters.size();
        out.emplace_back(filters.substr(from, to - from));
        from = to + 1;
    }
    return out;
}

// IsNumber (config.cpp): digits only; "" is a number.
bool isNumber(std::u16string_view s)
{
    return std::ranges::all_of(s, [](char16_t c) { return c >= u'0' && c <= u'9'; });
}

// AfterLast('.') lowered.
std::u16string extension(std::u8string_view path)
{
    std::u16string s = u16(path);
    const auto dot = s.rfind(u'.');
    if (dot != npos)
        s = s.substr(dot + 1);
    return asciiFold(s);
}

int columnOf(Field field)
{
    switch (field) {
    case Field::Style: return core::column::Style;
    case Field::Actor: return core::column::Actor;
    case Field::Effect: return core::column::Effect;
    case Field::Text: break;
    }
    return core::column::Text;
}

// Dialogue::GetTextElement on a file's line (no translation mode).
std::u16string rawElement(const core::RawDialogueFields &d, Field field)
{
    switch (field) {
    case Field::Style: return u16(d.style);
    case Field::Actor: return u16(d.actor);
    case Field::Effect: return u16(d.effect);
    case Field::Text: break;
    }
    return u16(d.text);
}

// Dialogue::SetTextElement(column, value, appendTextTL).
void setElement(core::LineRecord &line, Field field, const std::u16string &value, bool translationMode)
{
    switch (field) {
    case Field::Style: line.style = u8(value); return;
    case Field::Actor: line.actor = u8(value); return;
    case Field::Effect: line.effect = u8(value); return;
    case Field::Text: break;
    }
    const auto newline = value.find(u'\n');
    if (translationMode && newline != npos) {
        line.text = u8(value.substr(0, newline));
        line.translation = u8(value.substr(newline + 1));
    } else {
        line.text = u8(value);
    }
}

int rowOf(const EditSession &session, std::optional<core::LineId> id)
{
    const auto lines = session.document().lines();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (id && lines[i]->id == *id)
            return static_cast<int>(i);
    return 0;
}

// SubsFile::FirstSelection: the first selected shown row, or -1. Legacy
// gave -1 when only hidden Lines were selected, so "From selected" searched
// from row 0, before the selection; S57-from-selected starts at the first
// selected Line then (it is skipped, being hidden).
int firstSelection(const EditSession &session)
{
    const auto lines = session.document().lines();
    int hidden = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!session.selection().selected.contains(lines[i]->id))
            continue;
        if (lines[i]->visibility != core::LineVisibility::Hidden)
            return static_cast<int>(i);
        if (hidden == -1)
            hidden = static_cast<int>(i);
    }
    return hidden;
}

// SubsFile::GetElementByKey: the shown row of a row.
int shownRow(const EditSession &session, int row)
{
    const auto lines = session.document().lines();
    int shown = 0;
    for (int i = 0; i < row && i < static_cast<int>(lines.size()); ++i)
        if (lines[static_cast<std::size_t>(i)]->visibility != core::LineVisibility::Hidden)
            ++shown;
    return shown;
}

bool hasStyle(const core::Document &document, std::u8string_view name)
{
    return std::ranges::any_of(core::decodeStyles(document), [&](const auto &s) { return s.name == name; });
}

std::u8string replaced(std::u8string text, std::u8string_view from, std::u8string_view to)
{
    const auto at = text.find(from);
    if (at != std::u8string::npos)
        text.replace(at, from.size(), to);
    return text;
}

// A line without the CR of its CR LF end (the Linux build read the CR).
std::u16string withoutCr(std::u16string_view line)
{
    if (!line.empty() && line.back() == u'\r')
        line.remove_suffix(1);
    return std::u16string(line);
}

// One line of a file on disk as the legacy loops take it. Legacy tokenized
// with wxTOKEN_STRTOK, which drops empty lines; they are kept here as blank
// pieces, written back as they were (R3-hang-crash-loss) and not counted.
// The Linux build read the CR of each CR LF (R5-per-platform), so there a
// CRLF file's blank line is "\r": not empty, so a counted Line, as legacy.
// An SRT piece is a whole cue (legacy collects lines until the next number;
// "1\r" is no number, so on Linux a CRLF file is one piece, as legacy read it).
struct FilePiece {
    bool blank = false;
    std::u16string token; // legacy's token: trimmed (Trim()); an SRT cue's lines each with CRLF appended
    std::u16string raw;   // as read, without CRs of CR LF ends (an SRT cue's lines joined by CRLF, trimmed)
};

std::vector<FilePiece> filePieces(std::u16string_view text, bool srt)
{
    std::vector<FilePiece> out;
    if (srt) {
        const auto tokens = lineTokens(text);
        std::u16string token, raw;
        for (std::size_t next = 0; next < tokens.size();) {
            const std::u16string &t = tokens[next++];
            const bool noMoreTokens = next >= tokens.size();
            if (isNumber(t) || noMoreTokens) {
                if (noMoreTokens) {
                    token += t + u"\r\n";
                    raw += withoutCr(t) + u"\r\n";
                }
                if (token.empty())
                    continue;
                out.push_back({false, trimRight(std::move(token)), trimRight(std::move(raw))});
                token.clear();
                raw.clear();
            } else {
                token += t + u"\r\n";
                raw += withoutCr(t) + u"\r\n";
            }
        }
        return out;
    }
    for (std::size_t i = 0; i < text.size();) {
        std::size_t j = text.find(u'\n', i);
        if (j == npos)
            j = text.size();
        const std::u16string line(text.substr(i, j - i));
        if (line.empty())
            out.push_back({true, {}, {}});
        else
            out.push_back({false, trimRight(line), withoutCr(line)});
        i = j + 1;
    }
    return out;
}

// SetTextElement + GetRaw of a changed piece. Legacy parsed an SRT cue's
// Start from the start of the piece, so lines before its " --> " line went
// into the hours (atoi("1\r\r\n00") is 1: on Linux every CRLF file's first
// cue moved by an hour) and were lost. Here (R3-hang-crash-loss,
// F1-srt-leading-lines) the times come from the " --> " line; a leading cue
// number gives way to the count legacy writes in its place, other leading
// lines stay as they were.
std::u16string changedPiece(const FilePiece &piece, bool srt, int column, const std::u16string &value)
{
    if (srt) {
        std::vector<std::u16string> lines;
        for (std::size_t i = 0; i <= piece.raw.size();) {
            std::size_t j = piece.raw.find(u"\r\n", i);
            if (j == npos)
                j = piece.raw.size();
            lines.emplace_back(piece.raw.substr(i, j - i));
            i = j + 2;
        }
        std::size_t timeLine = 0;
        while (timeLine < lines.size() && lines[timeLine].find(u" --> ") == npos)
            ++timeLine;
        if (timeLine > 0 && timeLine < lines.size()) {
            std::u16string kept, rest;
            for (std::size_t i = 0; i < timeLine; ++i)
                if (i > 0 || !isNumber(lines[i]))
                    kept += lines[i] + u"\r\n";
            for (std::size_t i = timeLine; i < lines.size(); ++i)
                rest += lines[i] + (i + 1 < lines.size() ? u"\r\n" : u"");
            return kept + u16(core::rawDialogueWithField(u8(rest), column, u8(value)));
        }
    }
    return u16(core::rawDialogueWithField(u8(piece.token), column, u8(value)));
}

// The text before the first Dialogue/Comment line of an .ass file (written
// back with CRLF), or nullopt without dialogues.
std::optional<std::size_t> assEventsStart(std::u16string_view text)
{
    auto result = text.find(u"Dialogue:");
    const auto result1 = text.find(u"Comment:");
    if (result == npos && result1 == npos)
        return std::nullopt;
    if (result1 < result)
        result = result1;
    return result;
}

// The header with CRLF (legacy Replace("\n", "\r\n")). A line that ends
// with CR LF as read (the Linux build kept the CR) keeps one CR: legacy
// wrote "\r\r\n", changing every header line of a file it only replaced
// Line text in (R3-hang-crash-loss, F1-file-header-cr).
std::u16string crlf(std::u16string_view text)
{
    std::u16string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\n' && (i == 0 || text[i - 1] != u'\r'))
            out += u'\r';
        out += text[i];
    }
    return out;
}

} // namespace

namespace {

// The radio buttons set in order (HikariRadioButton::SetValue(true) clears
// the rest of its group): the last bit set wins.
void readOptions(Settings &s, int options)
{
    s.matchCase = options & kCaseSensitive;
    s.regex = options & kRegEx;
    s.startOfText = options & kStartOfText;
    s.endOfText = options & kEndOfText;
    if (options & kInFieldText)
        s.field = Field::Text;
    if (options & kInFieldStyle)
        s.field = Field::Style;
    if (options & kInFieldActor)
        s.field = Field::Actor;
    if (options & kInFieldEffect)
        s.field = Field::Effect;
    if (options & kInLinesAll)
        s.lines = Settings::Lines::All;
    if (options & kInLinesSelected)
        s.lines = Settings::Lines::Selected;
    if (options & kInLinesFromSelection)
        s.lines = Settings::Lines::FromSelection;
    s.includeComments = options & kSeekInComments;
    s.skipTags = options & kSeekOnlyInText;
    s.skipText = options & kSeekOnlyInTags;
}

} // namespace

FindReplaceSettings findReplaceFromOptions(int options)
{
    Settings s;
    s.field = Field::Text;
    s.lines = Settings::Lines::All;
    readOptions(s, options);
    // TabWindow's constructor reads the subfolders box from the hidden bit
    // and never sets the hidden box (kept legacy quirk).
    s.subfolders = options & kSearchHiddenFolders;
    s.hiddenFolders = false;
    return s;
}

FindReplaceSettings findReplaceSetValues(int options, const FindReplaceSettings &current)
{
    Settings s = current;
    // SetValue(false) clears a button without checking another; UpdateValues
    // reads no field as Text.
    s.field = Field::Text;
    s.lines = Settings::Lines::None;
    readOptions(s, options);
    return s;
}

int findReplaceOptions(const FindReplaceSettings &s, int previous)
{
    const bool files = s.tab == Tab::FindInFiles;
    // Only the Find in subtitles tab has no Lines box: it keeps the bits from 512 up.
    int options = files ? (previous >> 9) << 9 : 0;
    if (s.matchCase)
        options |= kCaseSensitive;
    if (s.regex)
        options |= kRegEx;
    if (s.startOfText)
        options |= kStartOfText;
    if (s.endOfText)
        options |= kEndOfText;
    switch (s.field) {
    case Field::Text: options |= kInFieldText; break;
    case Field::Style: options |= kInFieldStyle; break;
    case Field::Actor: options |= kInFieldActor; break;
    case Field::Effect: options |= kInFieldEffect; break;
    }
    if (!files) {
        switch (s.lines) {
        case Settings::Lines::All: options |= kInLinesAll; break;
        case Settings::Lines::Selected: options |= kInLinesSelected; break;
        case Settings::Lines::FromSelection: options |= kInLinesFromSelection; break;
        case Settings::Lines::None: break;
        }
    } else {
        if (s.subfolders)
            options |= kSearchSubfolders;
        if (s.hiddenFolders)
            options |= kSearchHiddenFolders;
    }
    if (s.includeComments)
        options |= kSeekInComments;
    if (s.skipTags)
        options |= kSeekOnlyInText;
    if (s.skipText)
        options |= kSeekOnlyInTags;
    return options;
}

FindReplace::FindReplace(FindReplaceHost &host, CaseFold fold) : m_host(host), m_fold(fold ? std::move(fold) : asciiFold)
{
}

void FindReplace::setRecent(Recent recent)
{
    // The constructor keeps 20 of each.
    for (auto *list : {&recent.finds, &recent.replacements, &recent.filters, &recent.paths})
        if (list->size() > 20)
            list->resize(20);
    m_recent = std::move(recent);
}

std::u16string FindReplace::lower(std::u16string_view s) const
{
    return m_fold(s);
}

std::u16string FindReplace::element(const core::LineRecord &line, bool translationMode) const
{
    switch (m_field) {
    case Field::Style: return u16(line.style);
    case Field::Actor: return u16(line.actor);
    case Field::Effect: return u16(line.effect);
    case Field::Text: break;
    }
    std::u16string out = u16(line.text);
    if (translationMode && !line.translation.empty())
        out += u'\n' + u16(line.translation);
    return out;
}

// --- Operations and questions -------------------------------------------

bool FindReplace::begin(const FindReplaceSettings *window)
{
    // Legacy's message boxes are modal: nothing else runs until they close.
    if (m_waiting)
        return false;
    if (window)
        m_window = *window;
    return true;
}

void FindReplace::finish()
{
    m_host.finished();
}

void FindReplace::ask(const FindQuestion &question, std::function<void(FindAnswer)> then)
{
    m_waiting = true;
    auto answered = std::make_shared<bool>(false);
    m_host.ask(question, [this, answered, then = std::move(then)](FindAnswer answer) {
        if (*answered)
            return;
        *answered = true;
        m_waiting = false;
        then(answer);
    });
}

void FindReplace::tell(const FindQuestion &question)
{
    m_host.ask(question, {});
}

std::optional<FindTab> FindReplace::tabById(DocumentId id)
{
    for (const auto &t : m_host.tabs())
        if (t.id == id && t.session)
            return t;
    return std::nullopt;
}

bool FindReplace::compile(std::u16string_view pattern)
{
    // wxRegEx(pattern, wxRE_ADVANCED [| wxRE_ICASE]) (R1-pcre2).
    int flags = core::LegacyRegex::Advanced;
    if (!m_matchCase)
        flags |= core::LegacyRegex::IgnoreCase;
    m_regex.emplace();
    if (m_regex->compile(pattern, flags))
        return true;
    // wxRegEx::Compile's wxLogError.
    m_host.logLine({FindLog::Kind::InvalidRegex,
                    replaced(replaced(u8"Invalid regular expression '%1': %2", u8"%1", u8(pattern)), u8"%2",
                             u8(m_regex->errorMessage())),
                    u8(pattern), u8(m_regex->errorMessage())});
    m_regex.reset();
    return false;
}

// wxRegEx::Matches then GetMatch(0); a match error (PCRE2's match or heap
// limit) is logged as wx_regexec does and counts as no match.
bool FindReplace::search(std::u16string_view text, int &start, int &length) const
{
    if (!m_regex)
        return false;
    if (!m_regex->matches(text)) {
        if (const auto error = m_regex->matchError(); !error.empty())
            m_host.logLine({FindLog::Kind::MatchError, u8"Failed to find match for regular expression: " + u8(error), u8(error), {}});
        return false;
    }
    const auto m = m_regex->match(0);
    if (!m)
        return false;
    start = static_cast<int>(m->first);
    length = static_cast<int>(m->second);
    return true;
}

// wxRegEx::Replace; an empty match moves on by one character
// (F1-empty-match, R3-hang-crash-loss: legacy repeated it forever).
int FindReplace::regexReplace(std::u16string &text, std::u16string_view replacement, std::size_t maxMatches) const
{
    if (!m_regex)
        return 0;
    const int count = m_regex->replace(text, replacement, maxMatches);
    if (const auto error = m_regex->matchError(); !error.empty())
        m_host.logLine({FindLog::Kind::MatchError, u8"Failed to find match for regular expression: " + u8(error), u8(error), {}});
    return std::max(count, 0);
}

bool FindReplace::updateValues(const FindReplaceSettings &w)
{
    m_field = w.field;
    m_matchCase = w.matchCase;
    m_regEx = w.regex;
    m_startLine = w.startOfText;
    m_endLine = w.endOfText;
    m_skipComments = !w.includeComments;
    m_onlyText = w.skipTags;
    m_onlyOption = w.skipTags || w.skipText;
    m_allLines = w.tab != Tab::FindInFiles && w.lines == Settings::Lines::All;
    m_selectedLines = w.tab != Tab::FindInFiles && w.lines == Settings::Lines::Selected;
    m_findString = u16(w.find);
    m_replaceString = w.tab == Tab::Find ? std::u16string() : u16(w.replace);
    if (m_startLine && m_regEx)
        m_findString = u"^" + m_findString;
    if (m_endLine && m_regEx) {
        if (m_findString.empty()) {
            m_findString = u"^(.*)$";
            m_replaceString = u"\\1" + m_replaceString;
        } else {
            m_findString += u"$";
        }
    }
    return m_regEx && !compile(m_findString);
}

// CheckStyles: the missing-styles questions, then `then(cancelled)`.
void FindReplace::checkStyles(DocumentId document, Then then)
{
    const auto tab = tabById(document);
    if (!tab)
        return then(true);
    FindReplaceSettings &window = m_window;
    m_stylesAsText = u16(window.styles);
    if (m_stylesAsText.empty())
        return then(false);
    std::u8string notFound, found;
    // wxStringTokenizer(styles, ",", wxTOKEN_STRTOK) and SubsFile::FindStyle.
    const std::u8string all = window.styles;
    for (std::size_t from = 0; from < all.size();) {
        std::size_t to = all.find(u8',', from);
        if (to == std::u8string::npos)
            to = all.size();
        if (to > from) {
            const std::u8string name(all.substr(from, to - from));
            if (hasStyle(tab->session->document(), name))
                found += name + u8",";
            else
                notFound += name + u8", ";
        }
        from = to + 1;
    }
    const auto done = [this, then] {
        if (!m_stylesAsText.empty() && !m_stylesAsText.starts_with(u","))
            m_stylesAsText = u"," + m_stylesAsText + u",";
        then(false);
    };
    if ((notFound.empty() || m_wasIgnored) && !found.empty())
        return done();
    if (!notFound.empty())
        notFound.pop_back(); // RemoveLast: "a, b,"
    FindQuestion question;
    if (found.empty()) {
        question = {FindQuestion::Kind::NoStyles,
                    u8"None of the selected styles exist in the subtitles being searched,\nso nothing will be "
                    u8"found.\nWhat would you like to do?",
                    {}, u8"Confirmation"};
    } else {
        question = {FindQuestion::Kind::Styles,
                    replaced(u8"Styles named \"%s\" do not exist in the subtitles being searched,\nwhich may "
                             u8"significantly reduce the number of search results.\nWhat would you like to do?",
                             u8"%s", notFound),
                    notFound, u8"Confirmation"};
    }
    ask(question, [this, found, done, then](FindAnswer answer) {
        if (answer == FindAnswer::Ok) {
            m_stylesAsText = u"," + u16(found);
            std::u8string kept = found;
            if (!kept.empty())
                kept.pop_back();
            m_window.styles = kept;
        } else if (answer == FindAnswer::Yes) {
            m_stylesAsText.clear();
            m_window.styles.clear();
        } else if (answer == FindAnswer::No) {
            m_wasIgnored = true;
        } else {
            return then(true);
        }
        done();
    });
}

bool FindReplace::keepFinding(std::u16string_view text, int textPos) const
{
    bool findStart = false;
    const int length = static_cast<int>(text.size());
    for (int i = textPos >= length ? length - 1 : textPos; i >= 0; --i) {
        if (text[static_cast<std::size_t>(i)] == u'}') {
            if (m_endLine && i == length - 1)
                findStart = true;
            break;
        }
        if (text[static_cast<std::size_t>(i)] == u'{') {
            findStart = true;
            break;
        }
    }
    if (!m_onlyText && findStart)
        return true;
    return m_onlyText && !findStart;
}

void FindReplace::addRecent(const FindReplaceSettings &window)
{
    m_recent.finds = addRecentSelection(std::move(m_recent.finds), window.find);
    if (window.tab != Tab::Find)
        m_recent.replacements = addRecentSelection(std::move(m_recent.replacements), window.replace);
    if (window.tab == Tab::FindInFiles) {
        m_recent.filters = addRecentSelection(std::move(m_recent.filters), window.filters);
        m_recent.paths = addRecentSelection(std::move(m_recent.paths), window.folder);
    }
    m_host.recentAdded(window.tab);
}

void FindReplace::reset()
{
    m_fromstart = true;
}

void FindReplace::selectionAdopted()
{
    m_textPosition = m_linePosition = 0;
}

// --- Find ------------------------------------------------------------------

void FindReplace::find(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    runFind(true, [this](bool) { finish(); });
}

void FindReplace::findNext()
{
    if (!begin(nullptr))
        return;
    if (m_findString.empty())
        return finish();
    runFind(false, [this](bool) { finish(); });
}

void FindReplace::runFind(bool withWindow, Found done)
{
    if (withWindow && m_window.tab == Tab::FindInFiles)
        return done(false);
    const auto tab = m_host.current();
    if (!tab || !tab->session)
        return done(false);
    if (withWindow && updateValues(m_window))
        return done(false);
    if (m_findString != m_oldfind) {
        m_fromstart = true;
        m_oldfind = m_findString;
    }
    EditSession &session = *tab->session;
    if (!m_fromstart && m_lastActive != rowOf(session, session.selection().active))
        m_lastActive = rowOf(session, session.selection().active);
    seekFromStart(tab->id, withWindow, std::move(done));
}

// Legacy's seekFromStart label.
void FindReplace::seekFromStart(DocumentId document, bool withWindow, Found done)
{
    const auto tab = tabById(document);
    if (!tab)
        return done(false);
    if (m_fromstart) {
        const int first = firstSelection(*tab->session);
        m_linePosition = !m_allLines && first != -1 ? first : 0;
        m_textPosition = 0;
    }
    if (!withWindow)
        return seekLines(document, withWindow, std::move(done));
    checkStyles(document, [this, document, withWindow, done](bool cancelled) {
        if (cancelled)
            return done(false); // legacy returns before the AddRecent tail
        seekLines(document, withWindow, done);
    });
}

void FindReplace::seekLines(DocumentId document, bool withWindow, Found done)
{
    // The editing target may have changed while a question was open.
    const auto tab = m_host.current();
    if (!tab || tab->id != document || !tab->session)
        return done(false);
    EditSession &session = *tab->session;
    const bool styles = !m_stylesAsText.empty();
    const bool tl = translationMode(session);
    const auto lines = session.document().lines();
    const int count = static_cast<int>(lines.size());
    while (m_linePosition < count) {
        const core::LineRecord &line = *lines[static_cast<std::size_t>(m_linePosition)];
        if (line.visibility == core::LineVisibility::Hidden || (m_skipComments && line.comment)) {
            ++m_linePosition;
            m_textPosition = 0;
            continue;
        }
        const bool eligible = (!styles && !m_selectedLines) ||
                              (styles && m_stylesAsText.find(u"," + u16(line.style) + u",") != npos) ||
                              (m_selectedLines && session.selection().selected.contains(line.id));
        if (!eligible) {
            m_textPosition = 0;
            ++m_linePosition;
            continue;
        }
        const std::u16string txt = element(line, tl);
        int foundPosition = -1;
        int foundLength = 0;
        if (!(m_startLine || m_endLine) && (m_findString.empty() || txt.empty())) {
            if (txt.empty() && m_findString.empty()) {
                foundPosition = 0;
                foundLength = 0;
            } else {
                m_textPosition = 0;
                ++m_linePosition;
                continue;
            }
        } else if (m_regEx) {
            int start = 0, length = 0;
            if (search(mid(txt, static_cast<std::size_t>(m_textPosition)), start, length)) {
                foundPosition = start + m_textPosition;
                foundLength = length;
            } else {
                m_textPosition = 0;
                ++m_linePosition;
                continue;
            }
        } else {
            const std::u16string ltext = m_matchCase ? txt : lower(txt);
            const std::u16string lfind = m_matchCase ? m_findString : lower(m_findString);
            if (m_startLine && (ltext.starts_with(lfind) || lfind.empty())) {
                foundPosition = 0;
                m_textPosition = 0;
            }
            // Legacy's else belongs to "End of text": with "Beginning of
            // text" the plain search below runs too (kept legacy quirk).
            if (m_endLine) {
                if (ltext.ends_with(lfind) || lfind.empty()) {
                    foundPosition = static_cast<int>(txt.size()) - static_cast<int>(lfind.size());
                    m_textPosition = 0;
                }
            } else {
                const auto at = ltext.find(lfind, static_cast<std::size_t>(m_textPosition));
                foundPosition = at == npos ? -1 : static_cast<int>(at);
            }
            foundLength = static_cast<int>(lfind.size());
        }
        if (foundPosition != -1 && (!m_onlyOption || keepFinding(txt, foundPosition))) {
            m_textPosition = foundPosition + foundLength;
            m_findstart = foundPosition;
            m_findend = m_textPosition;
            m_lastActive = m_reprow = m_linePosition;
            m_matchValid = true;
            m_matchDocument = document;
            m_matchLine = line.id;
            m_matchText = txt;
            // The editor that shows the match: TextEdit (1), the
            // translation mode original TextEditOrig (0), Actor (2), Effect (3).
            int role = -1, start = m_findstart, end = m_findend;
            if (m_field == Field::Text) {
                const auto newline = txt.find(u'\n');
                if (tl && newline != npos) {
                    const int n = static_cast<int>(newline);
                    if (foundPosition > n) {
                        role = 1;
                        start = foundPosition - n - 1;
                        end = m_findend - n - 1;
                    } else {
                        role = 0;
                    }
                } else {
                    role = 1;
                }
            } else if (m_field == Field::Actor) {
                role = 2;
            } else if (m_field == Field::Effect) {
                role = 3;
            }
            const core::LineId id = line.id;
            if (m_textPosition >= static_cast<int>(txt.size()) || m_startLine) {
                ++m_linePosition;
                m_textPosition = 0;
            }
            m_host.showLine(document, id, m_selectedLines, role, start, end);
            return endFind(withWindow, true, done);
        }
        m_textPosition = 0;
        ++m_linePosition;
    }
    m_linePosition = 0;
    m_fromstart = true;
    if (!m_wasResetToStart) {
        ask({FindQuestion::Kind::Wrap, u8"Reached end. Search from the beginning?", {}, u8"Confirmation"},
            [this, document, withWindow, done](FindAnswer answer) {
                if (answer == FindAnswer::Yes) {
                    m_wasResetToStart = true;
                    return seekFromStart(document, withWindow, done);
                }
                endFind(withWindow, false, done);
            });
        return;
    }
    const std::u8string phrase = u8(m_findString);
    tell({FindQuestion::Kind::Message, u8"Could not find the specified phrase \"" + phrase + u8"\".", phrase,
          u8"Confirmation", FindQuestion::Info::NotFound});
    m_wasResetToStart = false;
    endFind(withWindow, false, done);
}

void FindReplace::endFind(bool withWindow, bool found, const Found &done)
{
    if (m_fromstart) {
        if (withWindow)
            addRecent(m_window);
        m_fromstart = false;
    }
    done(found);
}

void FindReplace::clearResults()
{
    m_results.clear();
    m_replaceCheckedEnabled = true;
}

void FindReplace::findInLine(const std::u16string &text, std::u16string_view lineText, std::optional<DocumentId> document,
                             bool *isFirst, int linePos, int linePosId, const std::u8string &header, bool translationMode)
{
    int tabTextPosition = 0;
    for (;;) {
        int foundPosition = -1;
        int foundLength = 0;
        if (!(m_startLine || m_endLine) && (m_findString.empty() || text.empty())) {
            if (text.empty() && m_findString.empty()) {
                foundPosition = 0;
                foundLength = 0;
            } else {
                break;
            }
        } else if (m_regEx) {
            int start = 0, length = 0;
            if (!search(mid(text, static_cast<std::size_t>(tabTextPosition)), start, length))
                break;
            foundPosition = start + tabTextPosition;
            foundLength = length;
        } else {
            const std::u16string ltext = m_matchCase ? text : lower(text);
            const std::u16string lfind = m_matchCase ? m_findString : lower(m_findString);
            if (m_startLine && (ltext.starts_with(lfind) || lfind.empty())) {
                foundPosition = 0;
                tabTextPosition = 0;
            }
            if (m_endLine) {
                if (ltext.ends_with(lfind) || lfind.empty()) {
                    foundPosition = static_cast<int>(text.size()) - static_cast<int>(lfind.size());
                    tabTextPosition = 0;
                }
            } else {
                const auto at = ltext.find(lfind, static_cast<std::size_t>(tabTextPosition));
                foundPosition = at == npos ? -1 : static_cast<int>(at);
            }
            foundLength = static_cast<int>(lfind.size());
        }
        if (foundPosition == -1 || (m_onlyOption && !keepFinding(text, foundPosition)))
            break;
        if (*isFirst) {
            FindResult h;
            h.header = true;
            h.text = u16(header);
            m_results.push_back(std::move(h));
            *isFirst = false;
        }
        FindResult r;
        r.start = foundPosition;
        r.length = foundLength;
        r.document = document;
        if (!document)
            r.path = header;
        r.keyLine = linePos;
        r.idLine = linePosId + 1;
        if (m_field != Field::Text) {
            r.text = text + u"  ->  " + std::u16string(lineText);
        } else if (translationMode && text.find(u'\n') != npos) {
            const int n = static_cast<int>(text.find(u'\n'));
            r.translation = n < foundPosition;
            if (r.translation) {
                r.start = foundPosition - n - 1;
                r.text = text.substr(static_cast<std::size_t>(n) + 1);
            } else {
                r.text = text.substr(0, static_cast<std::size_t>(n));
            }
        } else {
            r.text = text;
        }
        m_results.push_back(std::move(r));
        if (tabTextPosition >= static_cast<int>(text.size()) || m_startLine)
            break;
        // Legacy never leaves a Line whose text ends with a plain "End of
        // text" search (it records it forever); here it is recorded once and
        // the search goes on (F1-end-of-text, R3-hang-crash-loss).
        if (m_endLine && !m_regEx)
            break;
        tabTextPosition = foundPosition + 1;
    }
}

void FindReplace::findAllInTab(const FindTab &tab, bool allLines, bool selectedOnly)
{
    const EditSession &session = *tab.session;
    const bool tl = translationMode(session);
    int positionId = 0;
    bool isFirst = true;
    const int first = firstSelection(session);
    int row = !allLines && first != -1 ? first : 0;
    if (row > 0)
        positionId = shownRow(session, row);
    const bool styles = !m_stylesAsText.empty();
    const auto lines = session.document().lines();
    for (; row < static_cast<int>(lines.size()); ++row) {
        const core::LineRecord &line = *lines[static_cast<std::size_t>(row)];
        if (line.visibility == core::LineVisibility::Hidden)
            continue;
        if (m_skipComments && line.comment) {
            ++positionId;
            continue;
        }
        if ((!styles && !selectedOnly) || (styles && m_stylesAsText.find(u"," + u16(line.style) + u",") != npos) ||
            (selectedOnly && session.selection().selected.contains(line.id))) {
            // GetTextNoCopy: the translation when there is one.
            const std::u16string shown = u16(line.translation.empty() ? line.text : line.translation);
            findInLine(element(line, tl), shown, tab.id, &isFirst, row, positionId, tab.name, tl);
        }
        ++positionId;
    }
}

void FindReplace::findAllInCurrent(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    clearResults();
    const auto tab = m_host.current();
    if (!tab || !tab->session)
        return finish();
    checkStyles(tab->id, [this, id = tab->id](bool cancelled) {
        const auto tab = tabById(id);
        if (cancelled || !tab || updateValues(m_window))
            return finish();
        findAllInTab(*tab, m_window.lines == Settings::Lines::All, m_window.lines == Settings::Lines::Selected);
        m_resultsRegEx = m_regEx;
        m_resultsMatchCase = m_matchCase;
        m_resultsNeedPrefix = m_endLine && m_regEx && m_findString.empty();
        m_resultsFindString = m_findString;
        m_resultsShown = true;
        addRecent(m_window);
        finish();
    });
}

void FindReplace::findInAllOpened(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    clearResults();
    const auto current = m_host.current();
    if (!current || !current->session)
        return finish();
    checkStyles(current->id, [this](bool cancelled) {
        if (cancelled || updateValues(m_window))
            return finish();
        for (const auto &tab : m_host.tabs())
            if (tab.session)
                findAllInTab(tab, m_window.lines == Settings::Lines::All, m_window.lines == Settings::Lines::Selected);
        m_resultsNeedPrefix = m_endLine && m_regEx && m_findString.empty();
        m_resultsRegEx = m_regEx;
        m_resultsMatchCase = m_matchCase;
        m_resultsFindString = m_findString;
        m_resultsShown = true;
        addRecent(m_window);
        finish();
    });
}

// --- Replace ---------------------------------------------------------------

int FindReplace::replaceInLine(std::u16string &text) const
{
    if (!(m_startLine || m_endLine) && (m_findString.empty() || text.empty())) {
        if (text.empty() && m_findString.empty()) {
            text = m_replaceString;
            return 1;
        }
        return 0;
    }
    if (m_startLine || m_endLine) {
        // A plain comparison, with a regular expression too (kept legacy
        // defect: "^x" and "x$" are looked for literally).
        const std::u16string ltext = m_matchCase ? text : lower(text);
        const std::u16string lfind = m_matchCase ? m_findString : lower(m_findString);
        const bool startsTagBlock = ltext.starts_with(u"{");
        const bool endsTagBlock = ltext.ends_with(u"}");
        if (m_startLine && (!m_onlyOption || (!startsTagBlock && m_onlyText) || (startsTagBlock && !m_onlyText))) {
            if (ltext.starts_with(lfind) || lfind.empty()) {
                text.replace(0, lfind.size(), m_replaceString);
                return 1;
            }
            return 0;
        }
        if (m_endLine && (!m_onlyOption || (!endsTagBlock && m_onlyText) || (endsTagBlock && !m_onlyText))) {
            if (ltext.ends_with(lfind) || lfind.empty()) {
                const std::size_t length = text.size();
                text.replace(length - lfind.size(), length, m_replaceString);
                return 1;
            }
            return 0;
        }
        return 0;
    }
    int linereps = 0;
    const std::u16string lfind = m_matchCase ? m_findString : lower(m_findString);
    const std::u16string ltext = m_matchCase ? text : lower(text);
    std::size_t newpos = 0;
    std::size_t flen = lfind.size();
    long long repsDiff = 0;
    for (;;) {
        std::size_t textPos = 0;
        if (m_regEx) {
            int start = 0, length = 0;
            if (!search(mid(ltext, newpos), start, length))
                break;
            textPos = static_cast<std::size_t>(start) + newpos;
            flen = static_cast<std::size_t>(length);
        } else {
            textPos = ltext.find(lfind, newpos);
        }
        // The next search starts after the pattern's length, also for a
        // regular expression (kept legacy quirk).
        newpos = textPos + lfind.size();
        // size_t arithmetic as legacy's: a position moved before the start
        // by shorter replacements wraps around and ends the loop.
        const std::size_t at = textPos + static_cast<std::size_t>(repsDiff);
        if (textPos == npos || at >= text.size())
            break;
        if (!m_onlyOption || keepFinding(text, static_cast<int>(at))) {
            if (m_regEx) {
                std::u16string match = mid(text, at, flen);
                regexReplace(match, m_replaceString, 0);
                text.replace(at, flen, match);
                repsDiff += static_cast<long long>(match.size()) - static_cast<long long>(flen);
            } else {
                text.replace(at, flen, m_replaceString);
                repsDiff += static_cast<long long>(m_replaceString.size()) - static_cast<long long>(flen);
            }
            ++linereps;
        }
    }
    return linereps;
}

int FindReplace::replaceAllInTab(const FindTab &tab, bool allLines, bool selectedOnly)
{
    EditSession &session = *tab.session;
    const bool notStyles = m_stylesAsText.empty();
    const LineVisible visible = m_host.actionLines(session);
    const bool tl = translationMode(session);
    auto compute = [&](int &total) {
        std::vector<std::pair<core::LineId, std::u16string>> changes;
        total = 0;
        const int first = firstSelection(session);
        const auto lines = session.document().lines();
        for (int i = !allLines && first != -1 ? first : 0; i < static_cast<int>(lines.size()); ++i) {
            const core::LineRecord &line = *lines[static_cast<std::size_t>(i)];
            if ((visible && !visible(line.id)) || (m_skipComments && line.comment))
                continue;
            if ((notStyles || m_stylesAsText.find(u"," + u16(line.style) + u",") != npos) &&
                !(selectedOnly && !session.selection().selected.contains(line.id))) {
                std::u16string txt = element(line, tl);
                const int reps = replaceInLine(txt);
                if (reps > 0) {
                    changes.emplace_back(line.id, std::move(txt));
                    total += reps;
                }
            }
        }
        return changes;
    };
    int total = 0;
    auto changes = compute(total);
    if (changes.empty())
        return 0;
    // Commands commit an overlapping draft first; count again on the result.
    if (const auto draft = session.draftLine();
        draft && std::ranges::any_of(changes, [&](const auto &c) { return c.first == *draft; })) {
        session.commitDraft();
        changes = compute(total);
        if (changes.empty())
            return 0;
    }
    std::set<core::LineId> touches;
    for (const auto &c : changes)
        touches.insert(c.first);
    const Field field = m_field;
    const auto ran = session.run(Command{"Replace all", session.revision(), touches, [&](core::Document &d) {
                                             for (const auto &[id, value] : changes)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) {
                                                         setElement(l, field, value, tl);
                                                     }))
                                                     return false;
                                             return true;
                                         }});
    if (!ran)
        return 0;
    m_host.changed(tab.id);
    return total;
}

// The last match is still where Find left it: not replaced yet, and its
// Line's text unchanged since. Legacy replaced [findstart, findend) of the
// last matched row whatever it held then: after Replace next had replaced the
// last match and its search found nothing more, or after an edit, the next
// Replace next overwrote text that did not match (F1-stale-replace,
// R3-hang-crash-loss). A match still there is replaced as legacy did, also
// when the search that ran first found nothing (kept quirk).
bool FindReplace::matchStillThere(const EditSession &session)
{
    if (!m_matchValid || m_findstart < 0 || m_findend < m_findstart)
        return false;
    const auto lines = session.document().lines();
    if (m_reprow < 0 || m_reprow >= static_cast<int>(lines.size()))
        return false;
    const core::LineRecord &line = *lines[static_cast<std::size_t>(m_reprow)];
    const auto tab = m_host.current();
    return tab && tab->id == m_matchDocument && line.id == m_matchLine &&
           element(line, translationMode(session)) == m_matchText;
}

void FindReplace::replace(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    if (window.tab != Tab::Replace) {
        m_host.logLine({FindLog::Kind::OutsideReplaceTab, u8"Replace called outside the replace tab", {}, {}});
        return finish();
    }
    const auto tab = m_host.current();
    if (!tab || !tab->session)
        return finish();
    // `searched`: Find already ran for this click (the active Line moved).
    const auto second = [this](bool searched) {
        const auto tab = m_host.current();
        if (!tab || !tab->session)
            return finish();
        const std::u16string find1 = u16(m_window.find);
        if (find1 != m_oldfind || m_findstart == -1 || m_findend == -1) {
            m_fromstart = true;
            m_oldfind = find1;
            return runFind(true, [this](bool found) { replaceFound(found); });
        }
        // A match that is gone is searched for again, once (F1-stale-replace).
        if (!matchStillThere(*tab->session) && !searched)
            return runFind(true, [this](bool found) { replaceFound(found); });
        replaceFound(true);
    };
    if (m_lastActive != rowOf(*tab->session, tab->session->selection().active))
        return runFind(true, [second](bool) { second(true); });
    second(false);
}

// Replace's second half, with the match Find left.
void FindReplace::replaceFound(bool)
{
    const auto tab = m_host.current();
    if (!tab || !tab->session || m_findstart == -1 || m_findend == -1 || !matchStillThere(*tab->session))
        return finish();
    const Field wrep = m_window.field;
    const std::u16string rep = u16(m_window.replace);
    EditSession &session = *tab->session;
    auto lines = session.document().lines();
    const core::LineId id = lines[static_cast<std::size_t>(m_reprow)]->id;
    if (session.draftLine() == id)
        session.commitDraft(); // commands commit an overlapping draft first
    lines = session.document().lines();
    const bool tl = translationMode(session);
    const Field searched = m_field;
    m_field = wrep;
    std::u16string replacedText = element(*lines[static_cast<std::size_t>(m_reprow)], tl);
    m_field = searched;
    const auto start = static_cast<std::size_t>(m_findstart);
    const auto length = static_cast<std::size_t>(m_findend - m_findstart);
    // Legacy's wxString::replace threw std::out_of_range past the end
    // (R3-hang-crash-loss: bounds-checked).
    if (start > replacedText.size())
        return finish();
    if (m_window.regex && m_regex) {
        std::u16string place = mid(replacedText, start, length);
        const int reps = regexReplace(place, rep, 1);
        replacedText.replace(start, length, reps ? place : rep);
    } else {
        replacedText.replace(start, length, rep);
    }
    const auto ran = session.run(Command{"Replace", session.revision(), {id}, [&](core::Document &d) {
                                             return d.editLine(id, [&](core::LineRecord &l) {
                                                 setElement(l, wrep, replacedText, tl);
                                             });
                                         }});
    if (ran)
        m_host.changed(tab->id);
    m_matchValid = false; // replaced: Find gives the next one
    m_textPosition = m_findstart + static_cast<int>(rep.size());
    runFind(true, [this](bool) { finish(); });
}

void FindReplace::replaceAll(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    if (window.tab != Tab::Replace)
        return finish();
    const auto tab = m_host.current();
    if (!tab || !tab->session || updateValues(m_window))
        return finish();
    checkStyles(tab->id, [this, id = tab->id](bool cancelled) {
        const auto tab = tabById(id);
        if (cancelled || !tab)
            return finish();
        const int all =
            replaceAllInTab(*tab, m_window.lines == Settings::Lines::All, m_window.lines == Settings::Lines::Selected);
        const auto text = u8(number(all));
        tell({FindQuestion::Kind::Message, u8"Replaced " + text + u8" times.", text, u8"Find and Replace",
              FindQuestion::Info::Replaced});
        addRecent(m_window);
        finish();
    });
}

void FindReplace::replaceInAllOpened(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    if (window.tab != Tab::Replace)
        return finish();
    const auto current = m_host.current();
    if (!current || !current->session)
        return finish();
    checkStyles(current->id, [this](bool cancelled) {
        if (cancelled)
            return finish();
        int all = 0;
        for (const auto &tab : m_host.tabs()) {
            if (!tab.session)
                continue;
            if (updateValues(m_window))
                return finish();
            all += replaceAllInTab(tab, m_window.lines == Settings::Lines::All,
                                   m_window.lines == Settings::Lines::Selected);
        }
        const auto text = u8(number(all));
        tell({FindQuestion::Kind::Message, u8"Replaced " + text + u8" times.", text, u8"Find and Replace",
              FindQuestion::Info::Replaced});
        addRecent(m_window);
        finish();
    });
}

// --- Files on disk ------------------------------------------------------------

void FindReplace::findInFiles(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    findReplaceInFiles(true);
    finish();
}

void FindReplace::replaceInFiles(const FindReplaceSettings &window)
{
    if (!begin(&window))
        return;
    ask({FindQuestion::Kind::ConfirmFiles,
         u8"Are you sure you want to make changes in all subtitle files?\nIf you make a mistake, backups are in the "
         u8"'ReplaceBackup' folder.",
         {}, u8"Info"},
        [this](FindAnswer answer) {
            if (answer == FindAnswer::Yes)
                findReplaceInFiles(false);
            finish();
        });
}

void FindReplace::findReplaceInFiles(bool find)
{
    const FindReplaceSettings &window = m_window;
    std::u8string filters = window.filters;
    if (filters.empty())
        filters = u8"*.ass;*.srt;*.sub;*.txt;*.mpl2";
    if (window.field != Field::Text)
        filters = u8"*.ass";
    // GetFolderFiles: each filter in turn.
    std::vector<std::u8string> paths;
    const auto tokens = filterTokens(filters);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto found = m_host.listFiles(window.folder, tokens[i], window.subfolders, window.hiddenFolders);
        if (!found) {
            tell({FindQuestion::Kind::Message, u8"Search path is invalid", {}, {}, FindQuestion::Info::InvalidPath});
            break;
        }
        paths.insert(paths.end(), found->begin(), found->end());
    }
    std::erase_if(paths, [](const std::u8string &p) {
        const auto ext = extension(p);
        return ext != u"ass" && ext != u"srt" && ext != u"sub" && ext != u"txt" && ext != u"mpl2";
    });
    if (paths.empty())
        return;
    if (updateValues(window))
        return;
    if (find) {
        m_resultsInFiles = true;
        clearResults();
    }
    // Legacy ran one thread per processor over consecutive slices of the
    // paths (the first n % processors one longer). Each thread's
    // SubsAllReplacements was never reset between its files, so after its
    // first replacement every later file of the slice that reached the write
    // was copied to ReplaceBackup and rewritten (unchanged files too, as
    // UTF-8 with a BOM, CRLF and trimmed lines), and the running total was
    // added again for each. Only files with a replacement are written here
    // (R3-hang-crash-loss, F1-file-untouched); the total shown is legacy's.
    // The results are the threads' lists in thread order, which is the path
    // order the serial walk gives.
    const int processors = std::max(1, m_host.processorCount());
    const std::size_t count = paths.size();
    const std::size_t perThread = count / static_cast<std::size_t>(processors);
    const std::size_t modulo = count % static_cast<std::size_t>(processors);
    const std::size_t threads = perThread == 0 ? count : static_cast<std::size_t>(processors);
    long long replacements = 0;
    std::size_t at = 0;
    for (std::size_t thread = 0; thread < threads; ++thread) {
        const std::size_t end = at + perThread + (thread < modulo ? 1 : 0);
        long long subsAllReplacements = 0;
        for (; at < end && at < count; ++at) {
            const FileOutcome outcome = findReplaceInFile(paths[at], find);
            subsAllReplacements += outcome.replacements;
            if (outcome.reachedWrite && subsAllReplacements)
                replacements += subsAllReplacements;
        }
    }
    if (!find && replacements) {
        const auto text = u8(number(replacements));
        tell({FindQuestion::Kind::Message, u8"Replaced " + text + u8" times.", text, u8"Find and Replace",
              FindQuestion::Info::Replaced});
        addRecent(window);
    } else if (find) {
        m_resultsShown = true;
    }
    addRecent(window);
}

FindReplace::FileOutcome FindReplace::findReplaceInFile(const std::u8string &path, bool find)
{
    const auto ext = extension(path);
    const auto read = m_host.readFile(path);
    // Legacy FileOpen gave "" and nothing matched: an .ass file has no
    // dialogues, any other reaches the write with nothing replaced.
    if (!read)
        return {ext != u"ass", 0};
    std::u16string subsText = *read;
    std::u16string replacedText;
    int tabLinePosition = 0;
    int positionId = 0;
    const bool isSRT = ext == u"srt";
    const bool isASS = ext == u"ass";
    std::u16string tlModeStyle;
    bool hasTlMode = false;
    if (isASS) {
        const auto tl = subsText.find(u"TLMode Style:");
        if (tl != npos) {
            hasTlMode = true;
            const auto newline = subsText.find(u"\n", tl + 13);
            if (newline != npos)
                tlModeStyle = trimBoth(subsText.substr(tl + 13, newline - (tl + 13)));
        }
        const auto events = assEventsStart(subsText);
        if (!events)
            return {false, 0}; // no dialogues: legacy went on to the next file
        if (!find)
            replacedText = crlf(subsText.substr(0, *events));
        subsText = subsText.substr(*events);
    }
    bool isFirst = true;
    int fileReplacements = 0;
    const int column = columnOf(m_field);
    for (const FilePiece &piece : filePieces(subsText, isSRT)) {
        if (piece.blank) {
            if (!find)
                replacedText += u"\r\n"; // kept (legacy dropped blank lines)
            continue;
        }
        const std::u16string &token = piece.token;
        const auto dial = core::rawDialogueFields(u8(token));
        std::u16string dialtxt = rawElement(dial, m_field);
        if (dial.comment && m_skipComments) {
            const bool notTlStyle = u16(dial.style) != tlModeStyle;
            if (!isASS || !hasTlMode || notTlStyle) {
                ++tabLinePosition;
                ++positionId;
            }
            if (notTlStyle) {
                // A skipped comment is written back as it was (legacy left it
                // out of the file: R3-hang-crash-loss).
                if (!find)
                    replacedText += (isSRT ? token : piece.raw) + u"\r\n";
                continue;
            }
        }
        if (find) {
            findInLine(dialtxt, u16(dial.text), std::nullopt, &isFirst, tabLinePosition, positionId, path, false);
        } else {
            const int reps = replaceInLine(dialtxt);
            if (isSRT)
                replacedText += number(tabLinePosition + 1) + u"\r\n";
            if (reps) {
                replacedText += changedPiece(piece, isSRT, column, dialtxt);
                fileReplacements += reps;
            } else {
                // An unchanged line is written as it was read (legacy wrote it trimmed).
                replacedText += (isSRT ? token : piece.raw) + u"\r\n";
                if (isSRT)
                    replacedText += u"\r\n";
            }
        }
        if (!isASS || !hasTlMode || u16(dial.style) != tlModeStyle) {
            ++tabLinePosition;
            ++positionId;
        }
    }
    if (fileReplacements) {
        if (!m_host.backupFile(path))
            m_host.logLine({FindLog::Kind::CannotBackUp, u8"Cannot back up " + path + u8".", path, {}});
        // Legacy counted the replacements whether or not the write worked
        // (FileWrite reports nothing); the host logs a failed write.
        m_host.writeFile(path, replacedText);
    }
    return {true, fileReplacements};
}

// --- The results dialog -----------------------------------------------------

void FindReplace::checkAll(bool check)
{
    for (auto &r : m_results)
        r.checked = check;
}

void FindReplace::toggleChecked(std::size_t row)
{
    if (row >= m_results.size())
        return;
    FindResult &item = m_results[row];
    item.checked = !item.checked;
    if (item.header) {
        for (std::size_t i = row + 1; i < m_results.size() && !m_results[i].header; ++i)
            m_results[i].checked = item.checked;
        return;
    }
    // The header is checked while anything in its group is.
    std::size_t header = row;
    while (header > 0 && !m_results[header].header)
        --header;
    bool somethingChecked = false;
    for (std::size_t i = header + 1; i < m_results.size() && !m_results[i].header; ++i)
        somethingChecked = somethingChecked || m_results[i].checked;
    if (m_results[header].header)
        m_results[header].checked = somethingChecked;
}

void FindReplace::toggleGroup(std::size_t header)
{
    if (header >= m_results.size() || !m_results[header].header)
        return;
    FindResult &h = m_results[header];
    for (std::size_t i = header + 1; i < m_results.size() && !m_results[i].header; ++i)
        m_results[i].visible = !h.visible;
    h.visible = !h.visible;
}

void FindReplace::showResult(std::size_t row)
{
    if (m_waiting || row >= m_results.size() || m_results[row].header)
        return;
    const FindResult r = m_results[row];
    if (r.document) {
        if (const auto tab = tabById(*r.document))
            showResultIn(*tab, r);
        return;
    }
    if (!m_host.fileExists(r.path))
        return;
    // OpenFile's save question is modal in legacy: nothing else runs until
    // it is answered.
    m_waiting = true;
    auto answered = std::make_shared<bool>(false);
    m_host.openFile(r.path, [this, answered, r](std::optional<DocumentId> id) {
        if (*answered)
            return;
        *answered = true;
        m_waiting = false;
        if (id)
            if (const auto tab = tabById(*id))
                showResultIn(*tab, r);
    });
}

void FindReplace::showResultIn(const FindTab &tab, const FindResult &r)
{
    if (!tab.session)
        return;
    const auto lines = tab.session->document().lines();
    if (r.keyLine < 0 || r.keyLine >= static_cast<int>(lines.size()))
        return;
    // EditBox::GetEditor(text): the original's editor when it shows this text.
    const core::LineRecord &line = *lines[static_cast<std::size_t>(r.keyLine)];
    const bool tl = translationMode(*tab.session);
    int role = 1;
    if (!r.text.empty())
        role = tl && r.text == u16(line.text) ? 0 : 1;
    else if (tl && line.translation.empty())
        role = 0;
    m_host.showLine(tab.id, line.id, false, role, r.start, r.start + r.length);
}

int FindReplace::replaceCheckedLine(std::u16string &line, int position, int length, int *diff) const
{
    // Legacy read outside the text here (R3-hang-crash-loss: bounds-checked).
    const long long at = static_cast<long long>(position) - *diff;
    if (at < 0 || at > static_cast<long long>(line.size()) || length < 0)
        return 0;
    const auto from = static_cast<std::size_t>(at);
    int reps = 1;
    if (m_regEx) {
        std::u16string foundString = mid(line, from, static_cast<std::size_t>(length));
        reps = regexReplace(foundString, m_replaceString, 0);
        if (reps > 0) {
            line.replace(from, static_cast<std::size_t>(length), foundString);
            *diff += length - static_cast<int>(foundString.size());
        }
    } else {
        line.replace(from, static_cast<std::size_t>(length), m_replaceString);
        *diff += length - static_cast<int>(m_replaceString.size());
    }
    return reps;
}

int FindReplace::replaceCheckedInFile(const std::vector<const FindResult *> &results)
{
    if (results.empty())
        return 0;
    std::size_t numOfResult = 0;
    const FindResult *seek = results[0];
    const std::u8string path = seek->path;
    const auto ext = extension(path);
    const auto read = m_host.readFile(path);
    if (!read)
        return 0;
    std::u16string subsText = *read;
    std::u16string replacedText;
    const bool isSRT = ext == u"srt";
    if (ext == u"ass") {
        const auto events = assEventsStart(subsText);
        if (!events)
            return 0;
        replacedText = crlf(subsText.substr(0, *events));
        subsText = subsText.substr(*events);
    }
    int numOfChanges = 0;
    int lineNum = 0;
    const int column = columnOf(m_field);
    // The results of this Line are used up (replaced or skipped).
    const auto skipLine = [&] {
        while (numOfResult < results.size() && results[numOfResult]->keyLine == lineNum)
            ++numOfResult;
        if (numOfResult < results.size())
            seek = results[numOfResult];
    };
    const auto unchanged = [&](const FilePiece &piece) {
        if (isSRT)
            replacedText += number(lineNum + 1) + u"\r\n";
        // As it was read (legacy wrote it trimmed).
        replacedText += (isSRT ? piece.token : piece.raw) + u"\r\n";
        if (isSRT)
            replacedText += u"\r\n";
    };
    for (const FilePiece &piece : filePieces(subsText, isSRT)) {
        if (piece.blank) {
            replacedText += u"\r\n"; // kept (legacy dropped blank lines)
            continue;
        }
        if (numOfResult >= results.size() || seek->keyLine != lineNum) {
            unchanged(piece);
            ++lineNum;
            continue;
        }
        std::u16string dialtxt = rawElement(core::rawDialogueFields(u8(piece.token)), m_field);
        int replacementDiff = 0;
        if (dialtxt != seek->text) {
            // Legacy left this Line out of the file and, never moving on,
            // every later one too (R3-hang-crash-loss): it stays as it is.
            m_host.logLine({FindLog::Kind::LineEdited,
                            u8"Line " + u8(number(seek->idLine)) + u8" cannot be replaced,\ncause it was edited.",
                            u8(number(seek->idLine)), {}});
            unchanged(piece);
            skipLine();
            ++lineNum;
            continue;
        }
        while (numOfResult < results.size() && seek->keyLine == lineNum) {
            numOfChanges += replaceCheckedLine(dialtxt, seek->start, seek->length, &replacementDiff);
            ++numOfResult;
            if (numOfResult < results.size())
                seek = results[numOfResult];
        }
        if (isSRT)
            replacedText += number(lineNum + 1) + u"\r\n";
        replacedText += changedPiece(piece, isSRT, column, dialtxt);
        ++lineNum;
    }
    if (!numOfChanges)
        return 0;
    if (!m_host.backupFile(path))
        m_host.logLine({FindLog::Kind::CannotBackUp, u8"Cannot back up " + path + u8".", path, {}});
    return m_host.writeFile(path, replacedText) ? numOfChanges : 0;
}

void FindReplace::replaceChecked(const std::u8string &replacement)
{
    if (!begin(nullptr))
        return;
    if (!m_resultsShown)
        return finish();
    m_replaceCheckedEnabled = false;
    // The options of the last search in the tabs go into FindReplace's own.
    m_replaceString = u16(replacement);
    m_regEx = m_resultsRegEx;
    m_matchCase = m_resultsMatchCase;
    m_findString = m_resultsFindString;
    if (m_regEx) {
        if (!compile(m_findString))
            return finish();
        if (m_resultsNeedPrefix)
            m_replaceString = u"\\1" + m_replaceString;
    }
    if (m_resultsInFiles) {
        std::vector<const FindResult *> group;
        std::u8string oldPath;
        for (const auto &r : m_results) {
            if (r.header || !r.checked)
                continue;
            if (oldPath != r.path) {
                replaceCheckedInFile(group);
                group.clear();
            }
            group.push_back(&r);
            oldPath = r.path;
        }
        replaceCheckedInFile(group);
        return finish();
    }
    // Each Document's checked matches, applied in order to its Lines' texts.
    struct Pending {
        DocumentId id;
        EditSession *session = nullptr;
        int changes = 0;
        // Working texts (text, translation) of the Lines the results reach.
        std::map<std::uint64_t, std::pair<std::u16string, std::u16string>> lines;
        // The Lines a replacement changed: only these are edited, so a
        // skipped or unreplaced Line keeps its saved bytes.
        std::set<std::uint64_t> changed;
    };
    std::vector<Pending> pending;
    const auto tabs = m_host.tabs();
    std::optional<DocumentId> oldTab;
    int oldKeyLine = -1;
    bool lastIsTextTl = false;
    bool skipLine = false;
    int replacementDiff = 0;
    for (const auto &r : m_results) {
        if (r.header || !r.checked || !r.document)
            continue;
        const auto tab = std::ranges::find_if(tabs, [&](const FindTab &t) { return t.id == *r.document; });
        if (tab == tabs.end() || !tab->session)
            continue;
        if (*r.document != oldTab) {
            oldKeyLine = -1;
            lastIsTextTl = false;
        }
        const auto lines = tab->session->document().lines();
        if (r.keyLine < 0 || r.keyLine >= static_cast<int>(lines.size()))
            continue;
        auto p = std::ranges::find_if(pending, [&](const Pending &x) { return x.id == *r.document; });
        if (p == pending.end()) {
            pending.push_back({*r.document, tab->session, 0, {}});
            p = pending.end() - 1;
        }
        const core::LineRecord &line = *lines[static_cast<std::size_t>(r.keyLine)];
        auto [entry, added] = p->lines.try_emplace(line.id.value, u16(line.text), u16(line.translation));
        std::u16string &lineText = r.translation ? entry->second.second : entry->second.first;
        if (oldKeyLine != r.keyLine || r.translation != lastIsTextTl) {
            replacementDiff = 0;
            if (lineText != r.text) {
                m_host.logLine({FindLog::Kind::LineEdited,
                                u8"Line " + u8(number(r.idLine)) + u8" cannot be replaced,\ncause it was edited.",
                                u8(number(r.idLine)), {}});
                skipLine = true;
                continue;
            }
            skipLine = false;
        }
        if (skipLine)
            continue;
        const int reps = replaceCheckedLine(lineText, r.start, r.length, &replacementDiff);
        p->changes += reps;
        if (reps > 0)
            p->changed.insert(line.id.value);
        oldTab = *r.document;
        oldKeyLine = r.keyLine;
        lastIsTextTl = r.translation;
    }
    // One "Replace all" step per changed Document (F1-checked-step,
    // R3-hang-crash-loss: legacy recorded each Document's step only when the
    // next one started, on that tab, so the last Document's change could get
    // no step and an empty step could land on another Document).
    for (auto &p : pending) {
        if (!p.changes)
            continue;
        EditSession &session = *p.session;
        if (const auto draft = session.draftLine(); draft && p.changed.contains(draft->value))
            session.commitDraft();
        std::set<core::LineId> touches;
        for (const auto id : p.changed)
            touches.insert(core::LineId{id});
        const auto ran = session.run(Command{"Replace all", session.revision(), touches, [&](core::Document &d) {
                                                 for (const auto id : p.changed) {
                                                     const auto &texts = p.lines.at(id);
                                                     if (!d.editLine(core::LineId{id}, [&](core::LineRecord &l) {
                                                             l.text = u8(texts.first);
                                                             l.translation = u8(texts.second);
                                                         }))
                                                         return false;
                                                 }
                                                 return true;
                                             }});
        if (ran)
            m_host.changed(p.id);
    }
    finish();
}

} // namespace hikari::application
