// GR1 (#364): grammar check with Harper. harper-ls is a helper process
// (backends::HarperClient), started at the first check while grammar check is
// on and ended when it is switched off. The Line editor's spell-checked field
// is checked 300 ms after its text stops changing; the Spellchecker panel's
// Grammar filter walks the whole file in the background. What harper-ls is
// given and where its findings go back is core::grammar's; a suggestion goes
// in by application::replaceGrammar.

#include "hikari/app/application.h"

#include "hikari/core/editor_syntax.h"
#include "hikari/core/text_projection.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <array>

namespace hikari::app {

namespace {

// Lines the panel's walk has with harper-ls at once.
constexpr std::size_t kWalkInFlight = 4;

// Harper's groups (harper-core/default_config.json), in core::grammar's order,
// with their descriptions shortened (the approved mockup).
constexpr std::array<std::pair<const char *, const char *>, 15> kGroupTexts{{
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Proper nouns"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Names of places, organizations and products keep their standard capitalization.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Initialisms"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Abbreviations and initialisms use the expected letter case and punctuation.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Closed compounds"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Compound words written as one word instead of split apart.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Phrase corrections"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Common phrase-level mistakes where a standard expression is preferred.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Capitalization and case"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Sentence starts, titles, brands and other case-sensitive text.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Punctuation and spacing"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Punctuation marks, apostrophes, quotation marks, commas and spacing.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Numbers and units"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Numbers, dates, times, decades and units of measurement.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Pronouns and possession"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Pronouns, contractions, possessives and their agreement.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Compounds and hyphenation"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Multi-word compounds that should be open, closed or hyphenated.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Spelling and orthography"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Misspellings and word forms spelled the wrong way.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Style and redundancy"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Wordy, repeated or weak phrasing that can be tightened.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Grammar and agreement"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Agreement, articles, prepositions, verb forms and sentence structure.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Word choice and usage"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "More standard or idiomatic wording for understandable phrases.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Regionalisms and dialect"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Wording that may not fit the chosen English.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Miscellaneous"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Rules that fit none of the groups above.")},
}};

QString latin(std::string_view s)
{
    return QString::fromLatin1(s.data(), qsizetype(s.size()));
}

QString groupName(std::size_t group)
{
    return group < kGroupTexts.size() ? QCoreApplication::translate("HikariSub.Spelling", kGroupTexts[group].first)
                                      : QString();
}

std::optional<std::size_t> groupOf(const backends::GrammarFinding &finding)
{
    return core::grammar::groupOfRule(finding.rule.toStdString());
}

QString harperPath(const QString &configured)
{
    if (!configured.isEmpty())
        return configured;
#ifdef _WIN32
    const QString name = QStringLiteral("harper-ls.exe");
#else
    const QString name = QStringLiteral("harper-ls");
#endif
    const QString sibling = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
#ifdef HIKARI_BUILD_HARPER_LS
    if (!QFileInfo::exists(sibling))
        return QStringLiteral(HIKARI_BUILD_HARPER_LS); // running from the build tree
#endif
    return sibling;
}

// A Line's text with nothing for harper-ls to read.
bool blank(const core::grammar::CheckedText &text)
{
    return std::ranges::all_of(text.text, [](char16_t c) { return c == u' '; });
}

const core::LineRecord *lineOf(const application::EditSession &session, core::LineId id)
{
    for (const auto *line : session.document().lines())
        if (line->id == id)
            return line;
    return nullptr;
}

} // namespace

void Application::setUpGrammar(const Options &options)
{
    m_harperProgram = harperPath(options.harperLs);
    m_grammarDir = options.grammarDir;
    if (m_grammarDir.isEmpty() && !m_settingsFile.isEmpty())
        m_grammarDir = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Harper");
    m_grammarDelay = new QTimer(this);
    m_grammarDelay->setSingleShot(true);
    m_grammarDelay->setInterval(300);
    connect(m_grammarDelay, &QTimer::timeout, this, &Application::checkEditorGrammar);
    connect(m_editor.get(), &ui::LineEditorController::changed, this, &Application::scheduleGrammar);
    // Words added to (or removed from) the user dictionary reach harper-ls's.
    connect(this, &Application::spellingChanged, this, &Application::syncGrammarWords);
}

bool Application::grammarOn() const
{
    return m_settings->integer("grammar.language") > 0;
}

QString Application::grammarVersion() const
{
    return QStringLiteral(HIKARI_HARPER_VERSION);
}

backends::HarperClient *Application::grammarClient()
{
    if (!grammarOn())
        return nullptr;
    if (!m_harper) {
        if (m_grammarDir.isEmpty()) {
            m_grammarTemp = std::make_unique<QTemporaryDir>();
            m_grammarDir = m_grammarTemp->path();
        }
        QDir().mkpath(m_grammarDir);
        m_harper = std::make_unique<backends::HarperClient>(m_harperProgram, m_grammarDir);
        connect(m_harper.get(), &backends::HarperClient::checked, this, &Application::grammarChecked);
        connect(m_harper.get(), &backends::HarperClient::failed, this, &Application::grammarFailed);
        m_grammarWords.clear();
        syncGrammarWords();
        m_harper->setSettings(grammarSettings());
    }
    return m_harper.get();
}

QJsonObject Application::grammarSettings() const
{
    static constexpr const char *kDialects[] = {"American", "British", "Canadian", "Australian"};
    const auto language = std::clamp<std::int64_t>(m_settings->integer("grammar.language"), 1, 4);
    const auto ids = core::grammar::groupIds();
    std::array<bool, kGroupTexts.size()> groups{};
    for (std::size_t i = 0; i < ids.size() && i < groups.size(); ++i)
        groups[i] = m_settings->boolean(("grammar.group." + std::string(ids[i])).c_str());
    const bool hunspellOn = m_spellChecker && m_settings->boolean("editor.spellchecker");
    QJsonObject linters;
    for (const auto &[rule, on] : core::grammar::linterSwitches(groups, hunspellOn))
        linters.insert(latin(rule), on);
    // Every file harper-ls keeps is in its own folder, never the user's
    // config folders.
    const QDir dir(m_grammarDir);
    const auto path = [&](const char *name) { return QDir::toNativeSeparators(dir.filePath(QLatin1String(name))); };
    return {{QStringLiteral("dialect"), QLatin1String(kDialects[language - 1])},
            {QStringLiteral("linters"), linters},
            {QStringLiteral("userDictPath"), path("dictionary.txt")},
            {QStringLiteral("workspaceDictPath"), path("workspace-dictionary.txt")},
            {QStringLiteral("fileDictPath"), path("file-dictionaries")},
            {QStringLiteral("ignoredLintsPath"), path("ignored-lints")},
            {QStringLiteral("statsPath"), path("stats.txt")}};
}

void Application::applyGrammarSettings()
{
    // Settings' Apply writes many keys: they are taken together, once.
    if (m_grammarApplyPending)
        return;
    m_grammarApplyPending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_grammarApplyPending = false;
        if (!grammarOn()) {
            m_harper.reset(); // the process ends
            m_grammarTicket = 0;
            m_grammarEditor.reset();
            if (m_grammarWalk && !m_grammarWalk->pending.empty()) {
                m_grammarWalk->pending.clear();
                m_grammarWalk->queue.clear();
                m_grammarWalk->cancelled = true;
            }
            emit grammarChanged();
            emit grammarWalkChanged();
            return;
        }
        if (m_harper) {
            const QJsonObject settings = grammarSettings();
            const QJsonObject before = m_harper->settings();
            if (settings == before)
                return;
            m_harper->setSettings(settings); // ends the process and the checks it had
            m_grammarTicket = 0;
            if (m_grammarWalk)
                grammarWalkSettingsChanged(before, settings);
        }
        m_grammarEditor.reset(); // found under the old rules
        checkEditorGrammar();
        emit grammarChanged();
    }, Qt::QueuedConnection);
}

// The walk's findings were found under `before`. Rules turned off (Ignore
// this rule, a group switched off) only drop theirs; any other change (the
// English, a rule turned on, Hunspell) walks the file again.
void Application::grammarWalkSettingsChanged(const QJsonObject &before, const QJsonObject &after)
{
    auto &walk = *m_grammarWalk;
    const QJsonObject wasOn = before.value(QStringLiteral("linters")).toObject();
    const QJsonObject isOn = after.value(QStringLiteral("linters")).toObject();
    QJsonObject rest = before, restAfter = after;
    rest.remove(QStringLiteral("linters"));
    restAfter.remove(QStringLiteral("linters"));
    const bool narrowed = rest == restAfter && std::ranges::all_of(isOn.keys(), [&](const QString &rule) {
        return !isOn.value(rule).toBool() || wasOn.value(rule).toBool(true);
    });
    if (narrowed) {
        for (auto &found : walk.findings)
            if (!isOn.value(found.finding.rule).toBool(true))
                found.handled = true;
        for (auto &[index, findings] : walk.arrived)
            findings.removeIf([&](const backends::GrammarFinding &f) { return !isOn.value(f.rule).toBool(true); });
        for (auto it = walk.pending.rbegin(); it != walk.pending.rend(); ++it)
            walk.queue.push_front(it->second);
        walk.pending.clear();
        if (walk.current && walk.findings[*walk.current].handled) {
            nextGrammarFinding();
            showGrammarFinding();
        }
        pumpGrammarWalk();
        emit grammarWalkChanged();
        return;
    }
    // Found under other settings: all of it again, from the walk's first Line
    // (a cancelled walk only loses what it found).
    walk.checks.clear();
    walk.queue.clear();
    walk.pending.clear();
    walk.arrived.clear();
    walk.findings.clear();
    walk.current.reset();
    walk.checked = 0;
    if (!walk.cancelled)
        for (std::size_t i = 0; i < walk.lines.size(); ++i)
            walk.queue.push_back(i);
    pumpGrammarWalk();
    emit grammarWalkChanged();
}

void Application::scheduleGrammar()
{
    if (grammarOn())
        m_grammarDelay->start();
}

void Application::checkEditorGrammar()
{
    const int role = spellingRole();
    // E4: as spelling, none for a comment, a template or code.
    const auto raw = core::syntax::spellChecked(m_editor->syntaxState()) ? editorRaw(role) : std::nullopt;
    if (!raw) {
        if (m_grammarTicket && m_harper)
            m_harper->cancel(m_grammarTicket);
        m_grammarTicket = 0;
        if (m_grammarEditor) {
            m_grammarEditor.reset();
            emit grammarChanged();
        }
        return;
    }
    if (m_grammarEditor && m_grammarEditorRole == role && m_grammarEditor->raw == *raw)
        return;
    if (m_grammarTicket && m_grammarRole == role && m_grammarPending.raw == *raw)
        return; // already with harper-ls
    auto *client = grammarClient();
    if (!client)
        return;
    if (m_grammarTicket)
        client->cancel(m_grammarTicket);
    m_grammarTicket = 0;
    m_grammarPending = GrammarCheck{*raw, core::grammar::checkedText(*raw), {}};
    m_grammarRole = role;
    if (blank(m_grammarPending.text)) {
        m_grammarEditor = std::move(m_grammarPending);
        m_grammarEditorRole = role;
        emit grammarChanged();
        return;
    }
    m_grammarTicket = client->check(QString::fromStdU16String(m_grammarPending.text.text));
}

void Application::grammarChecked(quint64 ticket, const QList<backends::GrammarFinding> &findings)
{
    m_grammarFailed = false;
    if (ticket == m_grammarTicket) {
        m_grammarTicket = 0;
        m_grammarPending.findings = findings;
        m_grammarEditor = std::move(m_grammarPending);
        m_grammarEditorRole = m_grammarRole;
        emit grammarChanged();
        return;
    }
    if (!m_grammarWalk)
        return;
    const auto it = m_grammarWalk->pending.find(ticket);
    if (it == m_grammarWalk->pending.end())
        return;
    m_grammarWalk->arrived[it->second] = findings;
    m_grammarWalk->pending.erase(it);
    pumpGrammarWalk();
}

void Application::grammarFailed(const QString &message)
{
    m_grammarTicket = 0;
    if (m_grammarWalk && !m_grammarWalk->pending.empty()) {
        m_grammarWalk->pending.clear();
        m_grammarWalk->queue.clear();
        m_grammarWalk->cancelled = true;
        m_grammarWalk->problem = QCoreApplication::translate("HikariSub.Spelling", "The grammar checker stopped.");
        emit grammarWalkChanged();
    }
    // One notice until a check succeeds again.
    if (m_grammarFailed)
        return;
    m_grammarFailed = true;
    m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Spelling", "Grammar check"),
                  QCoreApplication::translate("HikariSub.Spelling", "The grammar checker (harper-ls) stopped: %1")
                      .arg(message));
}

void Application::syncGrammarWords()
{
    if (!m_harper)
        return;
    const QStringList words = m_spellChecker ? addedDictionaryWords() : m_grammarOwnWords;
    QString content;
    for (const QString &word : words)
        if (!word.trimmed().isEmpty())
            content += word.trimmed() + QLatin1Char('\n');
    if (content == m_grammarWords && QFileInfo::exists(QDir(m_grammarDir).filePath(QStringLiteral("dictionary.txt"))))
        return;
    QSaveFile file(QDir(m_grammarDir).filePath(QStringLiteral("dictionary.txt")));
    if (!file.open(QIODevice::WriteOnly) || file.write(content.toUtf8()) < 0 || !file.commit())
        return;
    m_grammarWords = content;
    // harper-ls reads the file for each text: the field is checked again.
    m_grammarEditor.reset();
    scheduleGrammar();
}

const Application::GrammarCheck *Application::editorGrammarCheck(int role) const
{
    if (!grammarOn() || !m_grammarEditor || role != m_grammarEditorRole || role != spellingRole() ||
        !core::syntax::spellChecked(m_editor->syntaxState()))
        return nullptr;
    const auto raw = editorRaw(role);
    return raw && *raw == m_grammarEditor->raw ? &*m_grammarEditor : nullptr;
}

// A finding's range in the field as shown: the raw text's (whole Translated
// text in translation mode), less a carried leading block, and through the
// projection when tags are hidden.
std::optional<std::pair<int, int>> Application::fieldSpan(int role, const core::grammar::CheckedText &text, int start,
                                                          int end) const
{
    const auto raw = core::grammar::rawRange(text, std::size_t(start), std::size_t(end));
    if (!raw)
        return std::nullopt;
    const auto span = m_editor->displaySpan(role, int(raw->first), int(raw->second));
    if (span.first >= span.second)
        return std::nullopt;
    return span;
}

const backends::GrammarFinding *Application::editorGrammarFinding(int role, int position) const
{
    const auto *check = editorGrammarCheck(role);
    if (!check || position < 0)
        return nullptr;
    const auto range = [&](const backends::GrammarFinding &f) {
        return fieldSpan(role, check->text, f.start, f.end).value_or(std::pair{-1, -1});
    };
    // A position inside a finding, else at its end (the caret after a word).
    for (const auto &f : check->findings)
        if (const auto [s, e] = range(f); s <= position && position < e)
            return &f;
    for (const auto &f : check->findings)
        if (range(f).second == position)
            return &f;
    return nullptr;
}

QVariantList Application::editorGrammarMarks(int role)
{
    QVariantList out;
    const auto *check = editorGrammarCheck(role);
    if (!check)
        return out;
    for (const auto &f : check->findings)
        if (const auto span = fieldSpan(role, check->text, f.start, f.end))
            out << span->first << span->second;
    return out;
}

QVariantMap Application::editorGrammarAt(int role, int position)
{
    const auto *f = editorGrammarFinding(role, position);
    if (!f)
        return {};
    const auto group = groupOf(*f);
    const auto &text = m_grammarEditor->text.text;
    return {{QStringLiteral("rule"), f->rule},
            {QStringLiteral("group"), group ? int(*group) : -1},
            {QStringLiteral("groupName"), group ? groupName(*group) : QString()},
            {QStringLiteral("message"), f->message},
            {QStringLiteral("suggestions"), f->suggestions},
            {QStringLiteral("word"), f->word},
            {QStringLiteral("text"), QString::fromStdU16String(text.substr(std::size_t(f->start), std::size_t(f->end - f->start)))},
            {QStringLiteral("start"), f->start},
            {QStringLiteral("end"), f->end}};
}

bool Application::replaceEditorGrammar(int role, int position, const QString &replacement)
{
    auto *session = targetSession();
    const auto *f = editorGrammarFinding(role, position);
    if (!session || !f || !session->selection().active)
        return false;
    const auto caret = application::replaceGrammar(*session, *session->selection().active, role == 1,
                                                   m_grammarEditor->raw, std::size_t(f->start), std::size_t(f->end),
                                                   replacement.toStdU16String());
    if (!caret)
        return false;
    m_editor->reloadFromSession();
    refreshViews(); // the Grid and the tab's modified mark
    if (const auto raw = editorRaw(role)) {
        const int at = int(core::rawOffset(core::project(*raw), *caret, false));
        const int shown = m_editor->displaySpan(role, at, at).first;
        m_editor->selectInField(shown, shown);
    }
    // The findings after it moved with the text: the field is checked again.
    m_grammarEditor.reset();
    checkEditorGrammar();
    emit grammarChanged();
    return true;
}

int Application::ignoreGrammarRule(const QString &rule)
{
    const auto group = core::grammar::groupOfRule(rule.toStdString());
    if (!group)
        return -1;
    setGrammarGroup(int(*group), false);
    return int(*group);
}

void Application::setGrammarGroup(int group, bool on)
{
    const auto ids = core::grammar::groupIds();
    if (group < 0 || std::size_t(group) >= ids.size())
        return;
    m_settings->set(("grammar.group." + std::string(ids[std::size_t(group)])).c_str(), on);
}

QVariantList Application::grammarGroups() const
{
    const auto ids = core::grammar::groupIds();
    std::vector<int> counts(ids.size());
    for (const auto &rule : core::grammar::rules())
        ++counts[rule.group];
    QVariantList out;
    for (std::size_t i = 0; i < ids.size() && i < kGroupTexts.size(); ++i)
        out << QVariantMap{{QStringLiteral("id"), latin(ids[i])},
                           {QStringLiteral("name"), groupName(i)},
                           {QStringLiteral("description"),
                            QCoreApplication::translate("HikariSub.Spelling", kGroupTexts[i].second)},
                           {QStringLiteral("rules"), counts[i]}};
    return out;
}

bool Application::addGrammarWord(const QString &word)
{
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (m_spellChecker) {
        if (!m_spellChecker->addUserWord(trimmed.toStdU16String()))
            return false;
    } else if (!m_grammarOwnWords.contains(trimmed)) {
        m_grammarOwnWords << trimmed;
    }
    syncGrammarWords();
    spellingRefresh(); // Hunspell's marks, the Grid's too
    return true;
}

// The panel's Grammar walk.

QVariantMap Application::startGrammarWalk()
{
    closeGrammarWalk();
    auto *session = targetSession();
    if (!grammarOn() || !session)
        return grammarWalkState();
    m_editor->commit(); // the editor is left for the panel: its draft commits
    auto walk = std::make_unique<GrammarWalk>();
    walk->document = *m_workspace.editingTarget();
    walk->translation = application::spellsTranslation(session->document());
    const auto lines = session->document().lines();
    std::size_t first = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (session->selection().active == lines[i]->id)
            first = i;
    for (std::size_t i = 0; i < lines.size(); ++i)
        walk->lines.push_back(lines[(first + i) % lines.size()]->id);
    for (std::size_t i = 0; i < walk->lines.size(); ++i)
        walk->queue.push_back(i);
    m_grammarWalk = std::move(walk);
    pumpGrammarWalk();
    return grammarWalkState();
}

void Application::pumpGrammarWalk()
{
    if (!m_grammarWalk || m_grammarWalk->cancelled)
        return;
    // Another Document went into the editor while the walk waited for harper-ls.
    if (grammarWalkStale())
        return;
    auto &walk = *m_grammarWalk;
    const auto *session = m_files->session(walk.document);
    while (walk.pending.size() < kWalkInFlight && !walk.queue.empty()) {
        const std::size_t index = walk.queue.front();
        walk.queue.pop_front();
        const auto *line = session ? lineOf(*session, walk.lines[index]) : nullptr;
        if (!line || !core::syntax::spellChecked(core::syntax::textState(line->comment, core::toUtf16(line->effect)))) {
            walk.arrived[index] = {};
            continue;
        }
        GrammarCheck check{core::toUtf16(walk.translation ? line->translation : line->text), {}, {}};
        check.text = core::grammar::checkedText(check.raw);
        const bool empty = blank(check.text);
        const QString text = QString::fromStdU16String(check.text.text);
        walk.checks[index] = std::move(check);
        if (empty) {
            walk.arrived[index] = {};
            continue;
        }
        auto *client = grammarClient();
        if (!client)
            return;
        walk.pending[client->check(text)] = index;
    }
    // The findings join in walk order, as their Lines' results come.
    bool added = false;
    for (auto it = walk.arrived.find(walk.checked); it != walk.arrived.end(); it = walk.arrived.find(walk.checked)) {
        // A Line's findings left to right, as they are read.
        auto found = it->second;
        std::ranges::stable_sort(found, {}, [](const backends::GrammarFinding &f) { return std::pair{f.start, f.end}; });
        for (const auto &f : found)
            walk.findings.push_back({walk.checked, f});
        added = added || !it->second.isEmpty();
        walk.arrived.erase(it);
        ++walk.checked;
    }
    if (added && !walk.current) {
        nextGrammarFinding();
        showGrammarFinding();
    }
    emit grammarWalkChanged();
}

void Application::nextGrammarFinding()
{
    auto &walk = *m_grammarWalk;
    const std::size_t from = walk.current ? *walk.current + 1 : 0;
    walk.current.reset();
    for (std::size_t i = from; i < walk.findings.size(); ++i)
        if (!walk.findings[i].handled) {
            walk.current = i;
            return;
        }
    // Back to any left before (the walk moves on with what it finds).
    for (std::size_t i = 0; i < std::min(from, walk.findings.size()); ++i)
        if (!walk.findings[i].handled) {
            walk.current = i;
            return;
        }
}

void Application::showGrammarFinding()
{
    if (!m_grammarWalk || !m_grammarWalk->current || m_workspace.editingTarget() != m_grammarWalk->document)
        return;
    const auto &walk = *m_grammarWalk;
    const auto &found = walk.findings[*walk.current];
    const auto &check = walk.checks.at(found.line);
    m_editor->reloadFromSession();
    m_editor->showLine(walk.lines[found.line].value);
    const int role = walk.translation ? 1 : 0;
    if (editorRaw(role) != check.raw)
        return;
    if (const auto span = fieldSpan(role, check.text, found.finding.start, found.finding.end))
        m_editor->selectInField(span->first, span->second, role);
}

bool Application::grammarWalkStale()
{
    if (!m_grammarWalk)
        return true;
    if (m_workspace.editingTarget() == m_grammarWalk->document)
        return false;
    // Another Document is edited: the walk's Lines are not there.
    cancelGrammarWalk();
    m_grammarWalk->current.reset();
    m_grammarWalk->problem = QCoreApplication::translate("HikariSub.Spelling", "Another file is open in the editor.");
    emit grammarWalkChanged();
    return true;
}

QVariantMap Application::grammarWalkProblem(const QString &problem)
{
    QVariantMap state = grammarWalkState();
    state.insert(QStringLiteral("problem"), problem);
    return state;
}

QVariantMap Application::grammarWalkState()
{
    QVariantMap out{{QStringLiteral("on"), grammarOn()}};
    if (!m_grammarWalk)
        return out;
    const auto &walk = *m_grammarWalk;
    const bool walking = !walk.cancelled && walk.checked < walk.lines.size();
    out.insert(QStringLiteral("walking"), walking);
    out.insert(QStringLiteral("done"), !walking && !walk.cancelled);
    out.insert(QStringLiteral("cancelled"), walk.cancelled);
    out.insert(QStringLiteral("checked"), int(walk.checked));
    out.insert(QStringLiteral("total"), int(walk.lines.size()));
    out.insert(QStringLiteral("count"), int(walk.findings.size()));
    out.insert(QStringLiteral("problem"), walk.problem);
    out.insert(QStringLiteral("found"), walk.current.has_value());
    if (!walk.current)
        return out;
    const auto &found = walk.findings[*walk.current];
    const auto &f = found.finding;
    const auto &text = walk.checks.at(found.line).text.text;
    const auto group = groupOf(f);
    int row = 0;
    if (const auto *session = m_files->session(walk.document)) {
        const auto lines = session->document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id == walk.lines[found.line])
                row = int(i) + 1;
    }
    // The text around the finding, as harper-ls read it (no tags, breaks as
    // spaces), at most kContext characters a side.
    constexpr std::size_t kContext = 40;
    const std::size_t start = std::size_t(f.start), end = std::size_t(f.end);
    const std::size_t from = start > kContext ? start - kContext : 0;
    out.insert(QStringLiteral("index"), int(*walk.current) + 1);
    out.insert(QStringLiteral("line"), row);
    out.insert(QStringLiteral("rule"), f.rule);
    out.insert(QStringLiteral("groupName"), group ? groupName(*group) : QString());
    out.insert(QStringLiteral("message"), f.message);
    out.insert(QStringLiteral("before"), QString::fromStdU16String(text.substr(from, start - from)));
    out.insert(QStringLiteral("text"), QString::fromStdU16String(text.substr(start, end - start)));
    out.insert(QStringLiteral("after"), QString::fromStdU16String(text.substr(end, kContext)));
    out.insert(QStringLiteral("suggestions"), f.suggestions);
    out.insert(QStringLiteral("replacement"), f.suggestions.isEmpty() ? QString() : f.suggestions.front());
    out.insert(QStringLiteral("word"), f.word);
    return out;
}

QVariantMap Application::cancelGrammarWalk()
{
    if (!m_grammarWalk)
        return grammarWalkState();
    auto &walk = *m_grammarWalk;
    if (m_harper)
        for (const auto &[ticket, index] : walk.pending)
            m_harper->cancel(ticket);
    walk.pending.clear();
    walk.queue.clear();
    walk.arrived.clear();
    walk.cancelled = walk.checked < walk.lines.size();
    emit grammarWalkChanged();
    return grammarWalkState();
}

void Application::closeGrammarWalk()
{
    if (!m_grammarWalk)
        return;
    cancelGrammarWalk();
    m_grammarWalk.reset();
}

QVariantMap Application::grammarWalkReplace(const QString &replacement)
{
    if (grammarWalkStale() || !m_grammarWalk->current)
        return grammarWalkState();
    auto &walk = *m_grammarWalk;
    auto *session = targetSession();
    m_editor->commit();
    auto &found = walk.findings[*walk.current];
    auto &check = walk.checks.at(found.line);
    const int start = found.finding.start, end = found.finding.end;
    const auto caret = application::replaceGrammar(*session, walk.lines[found.line], walk.translation, check.raw,
                                                   std::size_t(start), std::size_t(end), replacement.toStdU16String());
    found.handled = true;
    if (!caret) {
        nextGrammarFinding();
        showGrammarFinding();
        emit grammarWalkChanged();
        return grammarWalkProblem(caret.error() == application::CommandRefusal::StaleRevision
                                      ? QCoreApplication::translate("HikariSub.Spelling", "The line changed since it was checked.")
                                      : QCoreApplication::translate("HikariSub.Spelling", "The text there cannot be replaced."));
    }
    // The Line's other findings move with its text; one it overlapped is gone.
    const int delta = int(replacement.size()) - (end - start);
    for (auto &other : walk.findings) {
        if (other.line != found.line || other.handled)
            continue;
        if (other.finding.start >= end) {
            other.finding.start += delta;
            other.finding.end += delta;
        } else if (other.finding.end > start) {
            other.handled = true;
        }
    }
    if (const auto *line = lineOf(*session, walk.lines[found.line])) {
        check.raw = core::toUtf16(walk.translation ? line->translation : line->text);
        check.text = core::grammar::checkedText(check.raw);
    }
    // The editor shows the correction even when no finding is left to show.
    m_editor->reloadFromSession();
    refreshViews();
    nextGrammarFinding();
    showGrammarFinding();
    emit grammarWalkChanged();
    return grammarWalkState();
}

QVariantMap Application::grammarWalkIgnore()
{
    if (grammarWalkStale() || !m_grammarWalk->current)
        return grammarWalkState();
    m_grammarWalk->findings[*m_grammarWalk->current].handled = true;
    nextGrammarFinding();
    showGrammarFinding();
    emit grammarWalkChanged();
    return grammarWalkState();
}

QVariantMap Application::grammarWalkIgnoreRule()
{
    if (grammarWalkStale() || !m_grammarWalk->current)
        return grammarWalkState();
    auto &walk = *m_grammarWalk;
    const auto group = groupOf(walk.findings[*walk.current].finding);
    walk.findings[*walk.current].handled = true;
    if (group)
        for (auto &other : walk.findings)
            if (groupOf(other.finding) == group)
                other.handled = true;
    nextGrammarFinding();
    showGrammarFinding();
    if (group)
        setGrammarGroup(int(*group), false);
    QVariantMap state = grammarWalkState();
    state.insert(QStringLiteral("ignoredGroup"), group ? int(*group) : -1);
    state.insert(QStringLiteral("ignoredGroupName"), group ? groupName(*group) : QString());
    emit grammarWalkChanged();
    return state;
}

QVariantMap Application::grammarWalkAddWord()
{
    if (grammarWalkStale() || !m_grammarWalk->current)
        return grammarWalkState();
    auto &walk = *m_grammarWalk;
    const auto &found = walk.findings[*walk.current];
    if (!found.finding.word)
        return grammarWalkState();
    const auto covered = [&](const GrammarWalk::Finding &f) {
        const auto &text = walk.checks.at(f.line).text.text;
        return text.substr(std::size_t(f.finding.start), std::size_t(f.finding.end - f.finding.start));
    };
    const std::u16string word = covered(found);
    if (!addGrammarWord(QString::fromStdU16String(word)))
        return grammarWalkProblem(QCoreApplication::translate("HikariSub.Spelling", "Error. Word \"%1\" was not added.")
                                      .arg(QString::fromStdU16String(word)));
    for (auto &other : walk.findings)
        if (other.finding.word && !other.handled && covered(other) == word)
            other.handled = true;
    nextGrammarFinding();
    showGrammarFinding();
    emit grammarWalkChanged();
    return grammarWalkState();
}

} // namespace hikari::app
