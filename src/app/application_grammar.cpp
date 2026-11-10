#include "hikari/app/application.h"

#include "hikari/core/editor_syntax.h"
#include "hikari/core/text_projection.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <set>

namespace hikari::app {

namespace {

constexpr std::size_t kWalkInFlight = 4;
constexpr std::size_t kWalkPerTurn = 256;
constexpr std::size_t kMaxCheckedUnits = 4096;
constexpr std::size_t kWalkKept = std::size_t(1) << 20;

// MUST KILL: kGroupTexts must stay in core::grammar::groupIds() order.
constexpr std::array<std::pair<const char *, const char *>, 15> kGroupTexts{{
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Proper nouns"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Names of places, organisations and products keep their standard capitalisation.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Initialisms"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Abbreviations and initialisms use the expected letter case and punctuation.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Closed compounds"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Compound words written as one word instead of split apart.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Phrase corrections"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Common phrase-level mistakes where a standard expression is preferred.")},
    {QT_TRANSLATE_NOOP("HikariSub.Spelling", "Capitalisation and case"),
     QT_TRANSLATE_NOOP("HikariSub.Spelling", "Personal pronouns, titles, brands and other case-sensitive text.")},
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
        return QStringLiteral(HIKARI_BUILD_HARPER_LS);
#endif
    return sibling;
}

bool blank(const core::grammar::CheckedText &text)
{
    return std::ranges::all_of(text.text, [](char16_t c) { return c == u' '; });
}

}

void Application::setUpGrammar(const Options &options)
{
    m_harperProgram = harperPath(options.harperLs);
    m_grammarDir = options.grammarDir;
    if (m_grammarDir.isEmpty() && !m_settingsFile.isEmpty())
        m_grammarDir = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Harper");
    if (m_spellingFolders.empty() && !m_grammarDir.isEmpty()) {
        QFile words(QDir(m_grammarDir).filePath(QStringLiteral("dictionary.txt")));
        if (words.open(QIODevice::ReadOnly))
            for (const QString &line : QString::fromUtf8(words.readAll()).split(QLatin1Char('\n')))
                if (const QString word = line.trimmed(); !word.isEmpty() && !m_grammarOwnWords.contains(word))
                    m_grammarOwnWords << word;
    }
    m_grammarDelay = new QTimer(this);
    m_grammarDelay->setSingleShot(true);
    m_grammarDelay->setInterval(300);
    connect(m_grammarDelay, &QTimer::timeout, this, &Application::checkEditorGrammar);
    connect(this, &Application::spellingChanged, this, &Application::syncGrammarWords);
}

bool Application::grammarOn() const
{
    return windowSettings(m_workspace.activeWindow()).integer("grammar.language") > 0;
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
    const auto &settings = windowSettings(m_workspace.activeWindow());
    const auto language = std::clamp<std::int64_t>(settings.integer("grammar.language"), 1, 4);
    const auto ids = core::grammar::groupIds();
    std::array<bool, kGroupTexts.size()> groups{};
    for (std::size_t i = 0; i < ids.size() && i < groups.size(); ++i)
        groups[i] = settings.boolean("grammar.group." + std::string(ids[i]));
    const bool hunspellOn = !m_spellCheckers.empty() && settings.boolean("editor.spellchecker");
    QJsonObject linters;
    for (const auto &[rule, on] : core::grammar::linterSwitches(groups, hunspellOn))
        linters.insert(latin(rule), on);
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
    if (m_grammarApplyPending)
        return;
    m_grammarApplyPending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_grammarApplyPending = false;
        if (!grammarOn()) {
            m_harper.reset();
            m_grammarTicket = 0;
            m_grammarEditor.reset();
            for (auto *walk : grammarWalks())
                if (!walk->pending.empty()) {
                    walk->pending.clear();
                    walk->queue.clear();
                    walk->cancelled = true;
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
            m_harper->setSettings(settings);
            m_grammarTicket = 0;
            for (auto *walk : grammarWalks())
                grammarWalkSettingsChanged(*walk, before, settings);
        }
        m_grammarEditor.reset();
        checkEditorGrammar();
        emit grammarChanged();
    }, Qt::QueuedConnection);
}

void Application::grammarWalkSettingsChanged(GrammarWalk &walk, const QJsonObject &before, const QJsonObject &after)
{
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
            nextGrammarFinding(walk);
            showGrammarFinding(walk.window);
        }
        pumpGrammarWalk(walk.window);
        emit grammarWalkChanged();
        return;
    }
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
    pumpGrammarWalk(walk.window);
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
    auto raw = core::syntax::spellChecked(editor().syntaxState()) ? editorRaw(role) : std::nullopt;
    if (raw && raw->size() > kMaxCheckedUnits)
        raw.reset();
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
    const auto format = editorFormat();
    if (m_grammarEditor && m_grammarEditorRole == role && m_grammarEditor->raw == *raw && m_grammarEditor->format == format) {
        if (m_grammarTicket && m_harper)
            m_harper->cancel(m_grammarTicket);
        m_grammarTicket = 0;
        return;
    }
    if (m_grammarTicket && m_grammarRole == role && m_grammarPending.raw == *raw && m_grammarPending.format == format)
        return;
    auto *client = grammarClient();
    if (!client)
        return;
    if (m_grammarTicket)
        client->cancel(m_grammarTicket);
    m_grammarTicket = 0;
    m_grammarPending = GrammarCheck{*raw, core::grammar::checkedText(*raw, format), {}, format};
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
    for (auto *walk : grammarWalks())
        if (const auto it = walk->pending.find(ticket); it != walk->pending.end()) {
            walk->arrived[it->second] = findings;
            walk->pending.erase(it);
            pumpGrammarWalk(walk->window);
            return;
        }
}

void Application::grammarFailed(const QString &message)
{
    m_grammarTicket = 0;
    for (auto *walk : grammarWalks())
        if (!walk->pending.empty()) {
            walk->pending.clear();
            walk->queue.clear();
            walk->cancelled = true;
            walk->problem = QCoreApplication::translate("HikariSub.Spelling", "The grammar checker stopped.");
            emit grammarWalkChanged();
        }
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
    const QStringList words = anySpellChecker() ? addedDictionaryWords() : m_grammarOwnWords;
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
    // harper-ls rereads the dictionary file for each check: redo the checks in flight.
    if (m_grammarTicket)
        m_harper->cancel(m_grammarTicket);
    m_grammarTicket = 0;
    m_grammarEditor.reset();
    scheduleGrammar();
    for (auto *each : grammarWalks()) {
        auto &walk = *each;
        if (walk.pending.empty() && walk.arrived.empty())
            continue;
        std::set<std::size_t> again;
        for (const auto &[ticket, index] : walk.pending) {
            m_harper->cancel(ticket);
            again.insert(index);
        }
        for (const auto &[index, findings] : walk.arrived)
            again.insert(index);
        walk.pending.clear();
        walk.arrived.clear();
        for (auto it = again.rbegin(); it != again.rend(); ++it)
            walk.queue.push_front(*it);
        pumpGrammarWalk(walk.window);
    }
}

const Application::GrammarCheck *Application::editorGrammarCheck(int role) const
{
    if (!grammarOn() || !m_grammarEditor || role != m_grammarEditorRole || role != spellingRole() ||
        !core::syntax::spellChecked(editor().syntaxState()))
        return nullptr;
    const auto raw = editorRaw(role);
    return raw && *raw == m_grammarEditor->raw && m_grammarEditor->format == editorFormat() ? &*m_grammarEditor
                                                                                          : nullptr;
}

core::SubtitleFormat Application::editorFormat() const
{
    const auto *session = targetSession();
    return session ? session->document().format() : core::SubtitleFormat::Ass;
}

std::optional<std::pair<int, int>> Application::fieldSpan(int role, const core::grammar::CheckedText &text, int start,
                                                          int end) const
{
    const auto raw = core::grammar::rawRange(text, std::size_t(start), std::size_t(end));
    if (!raw)
        return std::nullopt;
    const auto span = editor().displaySpan(role, int(raw->first), int(raw->second));
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
    editor().reloadFromSession();
    refreshViews();
    const int shown = editor().displaySpan(role, int(*caret), int(*caret)).first;
    editor().selectInField(shown, shown);
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
    if (auto *checker = anySpellChecker()) {
        if (!checker->addUserWord(trimmed.toStdU16String()))
            return false;
        userWordsChanged(checker);
    } else if (!m_grammarOwnWords.contains(trimmed)) {
        m_grammarOwnWords << trimmed;
    }
    syncGrammarWords();
    spellingRefresh();
    return true;
}

QVariantMap Application::startGrammarWalk(int shellWindow)
{
    closeGrammarWalk(shellWindow);
    auto *session = windowSession(shellWindow);
    if (!grammarOn() || !session)
        return grammarWalkState(shellWindow);
    views(shellWindow).editor->commit();
    auto walk = std::make_unique<GrammarWalk>();
    walk->document = *m_workspace.currentTab(shellWindow);
    walk->window = shellWindow;
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
    views(shellWindow).grammarWalk = std::move(walk);
    pumpGrammarWalk(shellWindow);
    return grammarWalkState(shellWindow);
}

Application::GrammarWalk *Application::grammarWalkOf(int shellWindow)
{
    if (shellWindow == 0)
        return m_mainViews.grammarWalk.get();
    const auto found = m_shellWindows.find(shellWindow);
    return found != m_shellWindows.end() && found->second.views ? found->second.views->grammarWalk.get() : nullptr;
}

std::vector<Application::GrammarWalk *> Application::grammarWalks()
{
    std::vector<GrammarWalk *> out;
    for (auto *views : allViews())
        if (views->grammarWalk)
            out.push_back(views->grammarWalk.get());
    return out;
}

const core::LineRecord *Application::grammarWalkLine(GrammarWalk &walk, const application::EditSession &session,
                                                     std::size_t index)
{
    if (walk.rowsOf != &session.document() || walk.rowsRevision != session.revision()) {
        walk.rows.clear();
        for (const auto *line : session.document().lines())
            walk.rows.emplace(line->id.value, line);
        walk.rowsOf = &session.document();
        walk.rowsRevision = session.revision();
    }
    const auto it = walk.rows.find(walk.lines[index].value);
    return it == walk.rows.end() ? nullptr : it->second;
}

void Application::pumpGrammarWalk(int shellWindow)
{
    auto *pumped = grammarWalkOf(shellWindow);
    if (!pumped || pumped->cancelled)
        return;
    if (grammarWalkStale(shellWindow))
        return;
    auto &walk = *pumped;
    const auto *session = m_files->session(walk.document);
    std::set<std::size_t> open;
    for (const auto &found : walk.findings)
        if (!found.handled)
            open.insert(found.line);
    std::size_t kept = 0;
    for (auto it = walk.checks.begin(); it != walk.checks.end();) {
        if (it->first < walk.checked && !open.contains(it->first)) {
            it = walk.checks.erase(it);
        } else {
            kept += it->second.raw.size();
            ++it;
        }
    }
    const auto again = [this, &walk, shellWindow] {
        if (walk.pumpQueued)
            return;
        walk.pumpQueued = true;
        QTimer::singleShot(0, this, [this, shellWindow] {
            if (auto *queued = grammarWalkOf(shellWindow))
                queued->pumpQueued = false;
            pumpGrammarWalk(shellWindow);
        });
    };
    std::size_t read = 0;
    while (walk.pending.size() < kWalkInFlight && !walk.queue.empty() && kept < kWalkKept) {
        if (++read > kWalkPerTurn) {
            again();
            break;
        }
        const std::size_t index = walk.queue.front();
        walk.queue.pop_front();
        const auto *line = session ? grammarWalkLine(walk, *session, index) : nullptr;
        if (!line || !core::syntax::spellChecked(core::syntax::textState(line->comment, core::toUtf16(line->effect)))) {
            walk.arrived[index] = {};
            continue;
        }
        const auto format = session->document().format();
        GrammarCheck check{core::toUtf16(walk.translation ? line->translation : line->text), {}, {}, format};
        if (check.raw.size() > kMaxCheckedUnits) {
            walk.arrived[index] = {};
            continue;
        }
        check.text = core::grammar::checkedText(check.raw, format);
        if (blank(check.text)) {
            walk.arrived[index] = {};
            continue;
        }
        kept += check.raw.size();
        const QString text = QString::fromStdU16String(check.text.text);
        walk.checks[index] = std::move(check);
        auto *client = grammarClient();
        if (!client)
            return;
        walk.pending[client->check(text)] = index;
    }
    const std::size_t joined = walk.checked;
    bool added = false;
    for (auto it = walk.arrived.find(walk.checked); it != walk.arrived.end(); it = walk.arrived.find(walk.checked)) {
        auto found = it->second;
        std::ranges::stable_sort(found, {}, [](const backends::GrammarFinding &f) { return std::pair{f.start, f.end}; });
        for (const auto &f : found)
            walk.findings.push_back({walk.checked, f});
        added = added || !it->second.isEmpty();
        walk.arrived.erase(it);
        ++walk.checked;
    }
    if (added && !walk.current) {
        nextGrammarFinding(walk);
        showGrammarFinding(shellWindow);
    }
    if (kept >= kWalkKept && walk.checked > joined && !walk.queue.empty())
        again();
    emit grammarWalkChanged();
}

void Application::nextGrammarFinding(GrammarWalk &walk)
{
    const std::size_t from = walk.current ? *walk.current + 1 : 0;
    walk.current.reset();
    for (std::size_t i = from; i < walk.findings.size(); ++i)
        if (!walk.findings[i].handled) {
            walk.current = i;
            return;
        }
    for (std::size_t i = 0; i < std::min(from, walk.findings.size()); ++i)
        if (!walk.findings[i].handled) {
            walk.current = i;
            return;
        }
}

void Application::showGrammarFinding(int shellWindow)
{
    auto *shown = grammarWalkOf(shellWindow);
    if (!shown || !shown->current || m_workspace.currentTab(shellWindow) != shown->document)
        return;
    auto *session = windowSession(shellWindow);
    if (!session)
        return;
    auto &walk = *shown;
    auto &editor = *views(shellWindow).editor;
    while (walk.current && !grammarWalkLine(walk, *session, walk.findings[*walk.current].line)) {
        const std::size_t gone = walk.findings[*walk.current].line;
        for (auto &other : walk.findings)
            if (other.line == gone)
                other.handled = true;
        nextGrammarFinding(walk);
    }
    if (!walk.current)
        return;
    const auto &found = walk.findings[*walk.current];
    const auto &check = walk.checks.at(found.line);
    const auto line = walk.lines[found.line];
    editor.commit();
    if (session->draftLine() && *session->draftLine() != line)
        return;
    session->setSelection(application::Selection{line, {line}, line, {}});
    editor.reloadFromSession();
    editor.showLine(line.value);
    refreshViews();
    const int role = walk.translation ? 1 : 0;
    if (windowLineRaw(shellWindow, role) != check.raw)
        return;
    if (const auto span = fieldSpan(role, check.text, found.finding.start, found.finding.end))
        editor.selectInField(span->first, span->second, role);
}

bool Application::grammarWalkStale(int shellWindow)
{
    auto *walk = grammarWalkOf(shellWindow);
    if (!walk)
        return true;
    if (m_workspace.currentTab(shellWindow) == walk->document)
        return false;
    stopGrammarWalk(*walk);
    walk->current.reset();
    walk->problem = QCoreApplication::translate("HikariSub.Spelling", "Another file is open in the editor.");
    emit grammarWalkChanged();
    return true;
}

QVariantMap Application::grammarWalkProblem(const QString &problem)
{
    QVariantMap state = grammarWalkState(m_workspace.activeWindow());
    state.insert(QStringLiteral("problem"), problem);
    return state;
}

QVariantMap Application::grammarWalkState(int shellWindow)
{
    QVariantMap out{{QStringLiteral("on"), grammarOn()}};
    const auto *own = grammarWalkOf(shellWindow);
    if (!own)
        return out;
    const auto &walk = *own;
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
    constexpr std::size_t kContext = 40;
    const std::size_t start = std::size_t(f.start), end = std::size_t(f.end);
    const std::u16string_view shown(text);
    out.insert(QStringLiteral("index"), int(*walk.current) + 1);
    out.insert(QStringLiteral("line"), row);
    out.insert(QStringLiteral("rule"), f.rule);
    out.insert(QStringLiteral("groupName"), group ? groupName(*group) : QString());
    out.insert(QStringLiteral("message"), f.message);
    out.insert(QStringLiteral("before"),
               QString::fromStdU16String(core::grammar::contextBefore(shown.substr(0, start), kContext)));
    out.insert(QStringLiteral("text"), QString::fromStdU16String(text.substr(start, end - start)));
    out.insert(QStringLiteral("after"),
               QString::fromStdU16String(core::grammar::contextAfter(shown.substr(end), kContext)));
    out.insert(QStringLiteral("suggestions"), f.suggestions);
    out.insert(QStringLiteral("replacement"), f.suggestions.isEmpty() ? QString() : f.suggestions.front());
    out.insert(QStringLiteral("word"), f.word);
    return out;
}

QVariantMap Application::cancelGrammarWalk()
{
    const int shellWindow = m_workspace.activeWindow();
    if (auto *walk = grammarWalkOf(shellWindow))
        stopGrammarWalk(*walk);
    return grammarWalkState(shellWindow);
}

void Application::stopGrammarWalk(GrammarWalk &walk)
{
    if (m_harper)
        for (const auto &[ticket, index] : walk.pending)
            m_harper->cancel(ticket);
    walk.pending.clear();
    walk.queue.clear();
    walk.arrived.clear();
    walk.cancelled = walk.checked < walk.lines.size();
    emit grammarWalkChanged();
}

void Application::closeGrammarWalk(int shellWindow)
{
    auto *walk = grammarWalkOf(shellWindow);
    if (!walk)
        return;
    stopGrammarWalk(*walk);
    views(shellWindow).grammarWalk.reset();
}

QVariantMap Application::grammarWalkReplace(const QString &replacement)
{
    const int shellWindow = m_workspace.activeWindow();
    if (grammarWalkStale(shellWindow) || !grammarWalkOf(shellWindow)->current)
        return grammarWalkState(shellWindow);
    auto &walk = *grammarWalkOf(shellWindow);
    auto *session = targetSession();
    editor().commit();
    auto &found = walk.findings[*walk.current];
    auto &check = walk.checks.at(found.line);
    const int start = found.finding.start, end = found.finding.end;
    const auto caret = application::replaceGrammar(*session, walk.lines[found.line], walk.translation, check.raw,
                                                   std::size_t(start), std::size_t(end), replacement.toStdU16String());
    found.handled = true;
    if (!caret) {
        nextGrammarFinding(walk);
        showGrammarFinding(shellWindow);
        emit grammarWalkChanged();
        return grammarWalkProblem(caret.error() == application::CommandRefusal::StaleRevision
                                      ? QCoreApplication::translate("HikariSub.Spelling", "The line changed since it was checked.")
                                      : QCoreApplication::translate("HikariSub.Spelling", "The text there cannot be replaced."));
    }
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
    if (const auto *line = grammarWalkLine(walk, *session, found.line)) {
        check.raw = core::toUtf16(walk.translation ? line->translation : line->text);
        check.text = core::grammar::checkedText(check.raw, session->document().format());
    }
    editor().reloadFromSession();
    refreshViews();
    nextGrammarFinding(walk);
    showGrammarFinding(shellWindow);
    pumpGrammarWalk(shellWindow);
    emit grammarWalkChanged();
    return grammarWalkState(shellWindow);
}

QVariantMap Application::grammarWalkIgnore()
{
    const int shellWindow = m_workspace.activeWindow();
    if (grammarWalkStale(shellWindow) || !grammarWalkOf(shellWindow)->current)
        return grammarWalkState(shellWindow);
    auto &walk = *grammarWalkOf(shellWindow);
    walk.findings[*walk.current].handled = true;
    nextGrammarFinding(walk);
    showGrammarFinding(shellWindow);
    pumpGrammarWalk(shellWindow);
    emit grammarWalkChanged();
    return grammarWalkState(shellWindow);
}

QVariantMap Application::grammarWalkIgnoreRule()
{
    const int shellWindow = m_workspace.activeWindow();
    if (grammarWalkStale(shellWindow) || !grammarWalkOf(shellWindow)->current)
        return grammarWalkState(shellWindow);
    auto &walk = *grammarWalkOf(shellWindow);
    const auto group = groupOf(walk.findings[*walk.current].finding);
    walk.findings[*walk.current].handled = true;
    if (group)
        for (auto &other : walk.findings)
            if (groupOf(other.finding) == group)
                other.handled = true;
    nextGrammarFinding(walk);
    showGrammarFinding(shellWindow);
    pumpGrammarWalk(shellWindow);
    if (group)
        setGrammarGroup(int(*group), false);
    QVariantMap state = grammarWalkState(shellWindow);
    state.insert(QStringLiteral("ignoredGroup"), group ? int(*group) : -1);
    state.insert(QStringLiteral("ignoredGroupName"), group ? groupName(*group) : QString());
    emit grammarWalkChanged();
    return state;
}

QVariantMap Application::grammarWalkAddWord()
{
    const int shellWindow = m_workspace.activeWindow();
    if (grammarWalkStale(shellWindow) || !grammarWalkOf(shellWindow)->current)
        return grammarWalkState(shellWindow);
    auto &walk = *grammarWalkOf(shellWindow);
    const auto &found = walk.findings[*walk.current];
    if (!found.finding.word)
        return grammarWalkState(shellWindow);
    const auto covered = [&](const GrammarWalk::Finding &f) {
        const auto &text = walk.checks.at(f.line).text.text;
        return text.substr(std::size_t(f.finding.start), std::size_t(f.finding.end - f.finding.start));
    };
    const std::u16string word = covered(found);
    if (!addGrammarWord(QString::fromStdU16String(word)))
        return grammarWalkProblem(QCoreApplication::translate("HikariSub.Spelling", "“%1” was not added to the dictionary.")
                                      .arg(QString::fromStdU16String(word)));
    for (auto &other : walk.findings)
        if (other.finding.word && !other.handled && covered(other) == word)
            other.handled = true;
    nextGrammarFinding(walk);
    showGrammarFinding(shellWindow);
    pumpGrammarWalk(shellWindow);
    emit grammarWalkChanged();
    return grammarWalkState(shellWindow);
}

}
