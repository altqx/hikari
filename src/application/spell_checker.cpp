#include "hikari/application/spell_checker.h"
#include "hikari/application/legacy_dir.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hikari::application {

namespace {

namespace fs = std::filesystem;
using core::legacy::Misspell;

std::u16string pathText(const fs::path &p)
{
    const std::u8string s = p.u8string();
    return core::toUtf16(s);
}

// wxDir::GetAllFiles(folder, ..., "*.dic", wxDIR_FILES): the folder's own
// entries that are not folders and match the pattern, unhidden, in the order
// each legacy platform listed them (R5-per-platform, legacy_dir.h):
// FindFirstFileW with wx's PathMatchSpec check on Windows, readdir and
// wxMatchWild on Linux. No sorting.
std::vector<fs::path> listFiles(const fs::path &folder, std::u16string_view pattern)
{
    return legacy_dir::entries(folder, pattern, legacy_dir::Files).value_or(std::vector<fs::path>{});
}

// wxString::BeforeLast('.'): everything before the last dot (the full path).
std::u16string beforeLastDot(const fs::path &p)
{
    const std::u16string s = pathText(p);
    const auto dot = s.rfind(u'.');
    return dot == std::u16string::npos ? std::u16string() : s.substr(0, dot);
}

bool fileExists(const fs::path &p)
{
    std::error_code ec;
    return fs::exists(p, ec);
}

const core::LineRecord *findLine(const core::Document &document, core::LineId id)
{
    for (const auto *line : document.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

std::ptrdiff_t activeRow(const EditSession &session)
{
    const auto active = session.selection().active;
    if (!active)
        return 0;
    const auto lines = session.document().lines();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i]->id == *active)
            return static_cast<std::ptrdiff_t>(i);
    return 0;
}

} // namespace

std::vector<std::u16string> availableDictionaries(const fs::path &folder)
{
    const auto dics = listFiles(folder, u"*.dic");
    const auto affs = listFiles(folder, u"*.aff");
    std::vector<std::u16string> out;
    // R3-hang-crash-loss: legacy reads aff[i] past a shorter .aff list
    // (undefined); here the pairing stops at its end.
    for (std::size_t i = 0; i < dics.size() && i < affs.size(); ++i)
        if (beforeLastDot(dics[i]) == beforeLastDot(affs[i]))
            out.push_back(pathText(dics[i].stem()));
    return out;
}

std::vector<std::u16string> availableDictionaries(const std::vector<fs::path> &folders)
{
    std::vector<std::u16string> out;
    for (const auto &folder : folders)
        for (auto &symbol : availableDictionaries(folder))
            if (std::ranges::find(out, symbol) == out.end())
                out.push_back(std::move(symbol));
    return out;
}

std::optional<std::u16string> readUserDictionary(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
#ifdef _WIN32
    // wxFFile "r" is the CRT's text mode on Windows (R5-per-platform): CR LF
    // byte pairs read as LF, and Ctrl+Z ends the file, before decoding. The
    // Linux build reads the bytes as they are ("\r" kept).
    {
        std::string folded;
        folded.reserve(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == '\x1A')
                break;
            if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n')
                continue;
            folded += bytes[i];
        }
        bytes.swap(folded);
    }
#endif
    std::u16string text;
    auto byteAt = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    // wxConvAuto: a byte-order mark decides; else UTF-8, else its default
    // single-byte fallback, ISO-8859-1 (convauto.cpp ms_defaultMBEncoding).
    if (bytes.size() >= 2 && ((byteAt(0) == 0xFF && byteAt(1) == 0xFE) || (byteAt(0) == 0xFE && byteAt(1) == 0xFF))) {
        const bool little = byteAt(0) == 0xFF;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2)
            text += static_cast<char16_t>(little ? byteAt(i) | (byteAt(i + 1) << 8) : (byteAt(i) << 8) | byteAt(i + 1));
    } else {
        std::size_t from = bytes.size() >= 3 && byteAt(0) == 0xEF && byteAt(1) == 0xBB && byteAt(2) == 0xBF ? 3 : 0;
        const std::u8string utf8(reinterpret_cast<const char8_t *>(bytes.data()) + from, bytes.size() - from);
        const std::u16string decoded = core::toUtf16(utf8);
        if (core::toUtf8(decoded) == utf8) {
            text = decoded;
        } else {
            for (std::size_t i = from; i < bytes.size(); ++i)
                text += static_cast<char16_t>(byteAt(i));
        }
    }
    // OpenWrite::FileOpen: empty text (an empty or BOM-only file) is a failed read.
    if (text.empty())
        return std::nullopt;
    return text;
}

bool writeUserDictionary(const fs::path &file, std::u16string_view text)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    const std::u8string utf8 = core::toUtf8(text);
    out << "\xEF\xBB\xBF";
    out.write(reinterpret_cast<const char *>(utf8.data()), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(out);
}

SpellChecker::SpellChecker(std::vector<fs::path> folders, SpellingBackendLoader loader)
    : m_folders(std::move(folders)), m_loader(std::move(loader))
{
    if (m_folders.empty())
        m_folders.emplace_back();
}

SpellChecker::SpellChecker(fs::path folder, SpellingBackendLoader loader)
    : SpellChecker(std::vector<fs::path>{std::move(folder)}, std::move(loader))
{
}

SpellChecker::Status SpellChecker::initialize(std::u16string_view language)
{
    clear();
    std::u16string name(language.empty() ? std::u16string_view(u"en_US") : language);
    std::optional<std::pair<fs::path, fs::path>> pair;
    for (const auto &folder : m_folders) {
        const fs::path base = folder / fs::path(core::toUtf8(name));
        const fs::path dic = fs::path(base).concat(u8".dic");
        const fs::path aff = fs::path(base).concat(u8".aff");
        if (fileExists(dic) && fileExists(aff)) {
            pair.emplace(aff, dic);
            break;
        }
    }
    if (!pair)
        return Status::NoDictionary;
    const auto &[aff, dic] = *pair;
    m_backend = m_loader ? m_loader(aff, dic) : nullptr;
    if (!m_backend)
        return Status::LoadFailed;
    if (fileExists(userDictionary()))
        if (const auto content = readUserDictionary(userDictionary()))
            for (const auto &word : core::legacy::userDictionaryWords(*content))
                m_backend->add(word);
    return Status::Ready;
}

void SpellChecker::clear()
{
    m_results.clear();
    m_backend.reset();
}

bool SpellChecker::checkWord(std::u16string_view word)
{
    if (!m_backend)
        return true;
    std::u16string key(word);
    if (const auto found = m_results.find(key); found != m_results.end())
        return found->second;
    const bool correct = !word.empty() && m_backend->spell(word);
    if (m_results.size() > 100000)
        m_results.clear();
    m_results.emplace(std::move(key), correct);
    return correct;
}

core::legacy::WordCheck SpellChecker::wordCheck()
{
    if (!m_backend)
        return {};
    return [this](std::u16string_view word) { return checkWord(word); };
}

std::vector<std::u16string> SpellChecker::suggestions(std::u16string_view word)
{
    if (!m_backend)
        return {};
    return m_backend->suggest(word);
}

bool SpellChecker::addWord(std::u16string_view word)
{
    if (word.empty() || core::legacy::legacyIsNumber(word) || !m_backend)
        return false;
    m_backend->add(word);
    m_results.clear();
    const auto content = readUserDictionary(userDictionary());
    const std::u16string text = content ? core::legacy::appendUserWord(std::u16string_view(*content), word)
                                        : core::legacy::appendUserWord(std::nullopt, word);
    writeUserDictionary(userDictionary(), text);
    return true;
}

bool SpellChecker::removeWords(const std::vector<std::u16string> &words)
{
    if (words.empty() || !m_backend || !fileExists(userDictionary()))
        return false;
    const auto content = readUserDictionary(userDictionary());
    if (!content)
        return false;
    const auto removed = core::legacy::removeUserWords(*content, words);
    if (removed.removed.empty())
        return false;
    for (const auto &word : removed.removed)
        m_backend->remove(word);
    m_results.clear();
    writeUserDictionary(userDictionary(), removed.content);
    return true;
}

std::vector<std::u16string> SpellChecker::addedWords() const
{
    if (!fileExists(userDictionary()))
        return {};
    const auto content = readUserDictionary(userDictionary());
    return content ? core::legacy::userDictionaryEntries(*content) : std::vector<std::u16string>{};
}

bool spellsTranslation(const core::Document &document)
{
    return document.scriptInfo(u8"TLMode") == u8"Yes";
}

core::legacy::SpellMarks editorMarks(std::u16string_view text, core::SubtitleFormat format, SpellChecker &checker,
                                     const SpellingText &spelling)
{
    if (text.empty())
        return {};
    return core::legacy::checkTextAndBrackets(text, format, spelling.segment, checker.wordCheck());
}

std::optional<std::size_t> misspellAt(const core::legacy::SpellMarks &marks, int at)
{
    for (std::size_t i = 0; i < marks.misspells.size(); ++i)
        if (at >= marks.misspells[i].start && at <= marks.misspells[i].end)
            return i;
    return std::nullopt;
}

std::expected<int, CommandRefusal> replaceInEditor(EditSession &session, core::LineId line, bool translation,
                                                   const Misspell &misspell, std::u16string_view replacement)
{
    if (session.isReadOnly())
        return std::unexpected(CommandRefusal::ReadOnly);
    const bool drafted = session.draftLine() == line;
    if (drafted && session.draftProblem() && session.invalidCommitPolicy() == InvalidCommitPolicy::Block)
        return std::unexpected(CommandRefusal::InvalidDraft);
    const std::optional<core::LineRecord> record =
        drafted ? session.draftRecord() : [&]() -> std::optional<core::LineRecord> {
            const auto *found = findLine(session.document(), line);
            return found ? std::optional(*found) : std::nullopt;
        }();
    if (!record)
        return std::unexpected(CommandRefusal::UnknownLine);
    std::u16string text = core::toUtf16(translation ? record->translation : record->text);
    const int caret = core::legacy::replaceMisspell(misspell.word, replacement, misspell.start, misspell.end, text);
    DraftChange change;
    (translation ? change.translation : change.text) = core::toUtf8(text);
    if (!session.editDraft(line, change))
        return std::unexpected(CommandRefusal::Protected);
    if (!session.commitDraftAs("Correcting spelling errors in the text field") && session.draftLine())
        return std::unexpected(CommandRefusal::InvalidDraft);
    return caret;
}

SpellCheckWalk::SpellCheckWalk(SpellChecker &checker, SpellingText spelling)
    : m_checker(checker), m_spelling(std::move(spelling))
{
}

std::u16string SpellCheckWalk::lineText(const core::Document &document, std::size_t row) const
{
    const auto lines = document.lines();
    if (row >= lines.size())
        return {};
    return core::toUtf16(spellsTranslation(document) ? lines[row]->translation : lines[row]->text);
}

std::u16string SpellCheckWalk::findNextMisspell(EditSession &session, const Options &options)
{
    const std::ptrdiff_t current = activeRow(session);
    if (m_lastActiveLine != current) {
        m_lastLine = static_cast<std::size_t>(current);
        m_lastActiveLine = current;
        m_lastMisspell = 0;
    }
    const core::Document &document = session.document();
    const auto lines = document.lines();
    const auto check = m_checker.wordCheck();
    auto ignored = [&](std::u16string_view word) {
        // wxArrayString::Index(word, false): any case.
        return std::ranges::any_of(m_ignored, [&](const std::u16string &i) {
            return std::ranges::equal(i, word, [&](char16_t a, char16_t b) {
                return m_spelling.cases.toLower(a) == m_spelling.cases.toLower(b);
            });
        });
    };
    for (std::size_t i = m_lastLine; i < lines.size(); ++i) {
        m_errors.clear();
        // A skipped comment leaves the last Line where it was.
        if (lines[i]->comment && options.ignoreComments)
            continue;
        m_errors = core::legacy::checkText(lineText(document, i), document.format(), m_spelling.segment, check);
        if (i != m_lastLine)
            m_lastMisspell = 0;
        while (m_lastMisspell < m_errors.size()) {
            const std::u16string &word = m_errors[m_lastMisspell].word;
            if (options.ignoreUpperCase && core::legacy::isAllUpperCase(word, m_spelling.cases)) {
                ++m_lastMisspell;
                continue;
            }
            if (!ignored(word)) {
                m_lastLine = i;
                return word;
            }
            ++m_lastMisspell;
        }
        m_lastLine = i;
    }
    return {};
}

bool SpellCheckWalk::next(EditSession &session, const Options &options)
{
    m_refused = false;
    const std::u16string word = findNextMisspell(session, options);
    if (word.empty()) {
        m_current.reset();
        m_blockOnActive = true;
        return false;
    }
    Found found;
    found.word = word;
    found.suggestions = m_checker.suggestions(word);
    found.line = session.document().lines()[m_lastLine]->id;
    found.start = m_errors[m_lastMisspell].start;
    found.end = m_errors[m_lastMisspell].end;
    if (m_lastActiveLine != static_cast<std::ptrdiff_t>(m_lastLine)) {
        // SelectRow(lastLine) and the editor's SetLine. The session keeps an
        // invalid draft's Line under the Block policy: then nothing is shown,
        // and the window's next activation starts again (after its message).
        if (!session.navigateTo(found.line)) {
            m_current.reset();
            m_refused = true;
            m_blockOnActive = true;
            m_lastActiveLine = -1;
            return false;
        }
        session.setSelection(Selection{found.line, {found.line}, found.line, {}});
        m_lastActiveLine = static_cast<std::ptrdiff_t>(m_lastLine);
    }
    m_lastText = lineText(session.document(), m_lastLine);
    m_current = std::move(found);
    return true;
}

bool SpellCheckWalk::stale(EditSession &session, bool otherDocument) const
{
    const std::ptrdiff_t current = activeRow(session);
    return otherDocument || m_lastActiveLine != current ||
           m_lastText != lineText(session.document(), static_cast<std::size_t>(current));
}

void SpellCheckWalk::restart(EditSession &session, const Options &options)
{
    m_lastMisspell = 0;
    next(session, options);
}

bool SpellCheckWalk::activated(EditSession &session, const Options &options, bool otherDocument)
{
    if (m_blockOnActive) {
        m_blockOnActive = false;
        return false;
    }
    if (!stale(session, otherDocument))
        return false;
    restart(session, options);
    return true;
}

std::expected<SpellCheckWalk::Result, CommandRefusal>
SpellCheckWalk::replace(EditSession &session, std::u16string_view replacement, const Options &options)
{
    // R3-hang-crash-loss: legacy checks only that the list is not empty and
    // then reads errors[lastMisspell], past the list when every word of the
    // last Line was skipped (undefined); here that is refused like an empty one.
    if (replacement.empty() || m_errors.empty() || m_lastMisspell >= m_errors.size() || !m_current)
        return Result::Unchanged;
    const auto lines = session.document().lines();
    if (m_lastLine >= lines.size())
        return Result::Unchanged;
    const core::LineId id = lines[m_lastLine]->id;
    const bool translation = spellsTranslation(session.document());
    std::u16string text = lineText(session.document(), m_lastLine);
    const Misspell misspell = m_errors[m_lastMisspell];
    core::legacy::replaceMisspell(misspell.word, replacement, misspell.start, misspell.end, text);
    const std::u8string value = core::toUtf8(text);
    const auto ran = session.run(Command{"Correcting spelling errors", session.revision(), {id}, [&](core::Document &d) {
                                             return d.editLine(id, [&](core::LineRecord &l) {
                                                 (translation ? l.translation : l.text) = value;
                                             });
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    const int oldPos = misspell.start;
    next(session, options);
    // The replacement is still misspelled at the same place: skip it. (Legacy
    // compares only the offset, so the next Line's word at that offset is
    // skipped too. With nothing found legacy reads errors[lastMisspell] past
    // the list, undefined: bounds-checked, R3-hang-crash-loss.)
    if (m_current && m_lastMisspell < m_errors.size() && m_errors[m_lastMisspell].start == oldPos) {
        ++m_lastMisspell;
        next(session, options);
    }
    return Result::Replaced;
}

std::expected<SpellCheckWalk::Result, CommandRefusal>
SpellCheckWalk::replaceAll(EditSession &session, std::u16string_view misspell, std::u16string_view replacement,
                           const Options &options)
{
    if (replacement.empty() || misspell.empty())
        return Result::Unchanged;
    const auto &cases = m_spelling.cases;
    auto lower = [&](std::u16string_view s) {
        std::u16string out(s);
        for (char16_t &c : out)
            c = cases.toLower(c);
        return out;
    };
    const std::u16string find = lower(misspell);
    const core::Document &document = session.document();
    const bool translationMode = spellsTranslation(document);
    std::vector<std::pair<core::LineId, std::u8string>> changes;
    for (const auto *line : document.lines()) {
        if (line->comment && options.ignoreComments)
            continue;
        std::u16string text = core::toUtf16(translationMode && !line->translation.empty() ? line->translation : line->text);
        if (lower(text).find(find) == std::u16string::npos)
            continue;
        const auto found = core::legacy::findMisspells(text, find, document.format(), m_spelling.segment, cases);
        if (found.empty())
            continue;
        for (auto it = found.rbegin(); it != found.rend(); ++it)
            core::legacy::replaceMisspell(it->word, core::legacy::rightCase(replacement, it->word, cases), it->start,
                                          it->end, text);
        changes.emplace_back(line->id, core::toUtf8(text));
    }
    if (!changes.empty()) {
        std::set<core::LineId> touches;
        for (const auto &c : changes)
            touches.insert(c.first);
        // In translation mode the result goes to the translation, also when
        // it was read from an untranslated Line's text (legacy CheckTlRef).
        const auto ran = session.run(Command{"Correcting spelling errors", session.revision(), touches,
                                             [&](core::Document &d) {
                                                 for (const auto &[id, value] : changes)
                                                     if (!d.editLine(id, [&](core::LineRecord &l) {
                                                             (translationMode ? l.translation : l.text) = value;
                                                         }))
                                                         return false;
                                                 return true;
                                             }});
        if (!ran)
            return std::unexpected(ran.error());
    }
    next(session, options);
    return changes.empty() ? Result::NothingReplaced : Result::Replaced;
}

void SpellCheckWalk::ignore(EditSession &session, const Options &options)
{
    ++m_lastMisspell;
    next(session, options);
}

void SpellCheckWalk::ignoreAll(EditSession &session, std::u16string_view word, const Options &options)
{
    m_ignored.emplace_back(word);
    next(session, options);
}

bool SpellCheckWalk::addWord(EditSession &session, std::u16string_view word, const Options &options)
{
    const bool added = m_checker.addWord(word);
    next(session, options);
    return added;
}

} // namespace hikari::application
