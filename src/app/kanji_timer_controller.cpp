#include "hikari/app/kanji_timer_controller.h"

#include "hikari/application/kana.h"
#include "hikari/application/style_manager.h"
#include "hikari/core/text_projection.h"
#include "settings_store.h"

#include <QChar>

#include <algorithm>
#include <map>
#include <set>

namespace hikari::app {

namespace {

constexpr std::size_t kShownGroups = 60;

constexpr const char *kSuggest = "audio.kanjiTimerSuggest";
constexpr const char *kMulti = "audio.kanjiTimerMulti";
constexpr const char *kFurigana = "audio.kanjiTimerFurigana";

std::pair<std::vector<application::KanjiTimer::Line>, std::vector<application::KanjiTimer::Line>>
styleLines(application::EditSession &session, const QString &romajiStyle, const QString &kanjiStyle)
{
    std::vector<application::KanjiTimer::Line> romaji, kanji;
    const std::u8string romajiName = core::toUtf8(romajiStyle.toStdU16String());
    const std::u8string kanjiName = core::toUtf8(kanjiStyle.toStdU16String());
    for (const auto *line : session.document().lines()) {
        if (line->text.empty() || line->effect == u8"fx")
            continue;
        const auto text = core::toUtf16(line->text);
        const int start = static_cast<int>(line->start.value.microseconds() / 1000);
        const int end = static_cast<int>(line->end.value.microseconds() / 1000);
        if (line->style == romajiName && application::hasKaraokeTags(text))
            romaji.push_back({line->id, text, start, end});
        else if (line->style == kanjiName)
            kanji.push_back({line->id, text, start, end});
    }
    return {std::move(romaji), std::move(kanji)};
}

QString qs(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), static_cast<qsizetype>(s.size()));
}

QString qs8(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

application::KaraokeCharClass charClasses()
{
    auto classes = application::KaraokeCharClass::ascii();
    const auto ascii = classes.space;
    classes.space = [ascii](char16_t c) { return c < 128 ? ascii(c) : QChar(c).isSpace(); };
    classes.lower = [](char16_t c) { return static_cast<char16_t>(QChar(c).toLower().unicode()); };
    return classes;
}

QString assTime(int ms)
{
    const int cs = std::max(ms, 0) / 10;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(cs / 360000)
        .arg(cs / 6000 % 60, 2, 10, QLatin1Char('0'))
        .arg(cs / 100 % 60, 2, 10, QLatin1Char('0'))
        .arg(cs % 100, 2, 10, QLatin1Char('0'));
}

QString shown(std::u16string_view text)
{
    QString out;
    const auto kanji = application::kanjiText(text);
    for (const auto &unit : kanji.units)
        out += unit.text == u"\\N" || unit.text == u"\\n" || unit.text == u"\\h" ? QStringLiteral(" ") : qs(unit.text);
    return out.simplified();
}

QString statusKey(application::KanjiTimer::Status status)
{
    using S = application::KanjiTimer::Status;
    switch (status) {
    case S::Suggested: return QStringLiteral("suggested");
    case S::Check: return QStringLiteral("check");
    case S::ToDo: return QStringLiteral("todo");
    case S::Done: return QStringLiteral("done");
    case S::Applied: return QStringLiteral("applied");
    case S::Changed: return QStringLiteral("changed");
    case S::NoKanji: return QStringLiteral("noKanji");
    case S::NoRomaji: return QStringLiteral("noRomaji");
    }
    return {};
}

bool hasKanji(std::u16string_view text)
{
    return std::ranges::any_of(text, [](char16_t c) { return !application::isKana(c) && c > 0x2E80; });
}

}

KanjiTimerController::KanjiTimerController(ui::SettingsStore &settings, Hooks hooks, QObject *parent)
    : QObject(parent), m_settings(settings), m_hooks(std::move(hooks))
{
}

KanjiTimerController::~KanjiTimerController() = default;

application::EditSession *KanjiTimerController::target() const
{
    return m_hooks.target ? m_hooks.target() : nullptr;
}

bool KanjiTimerController::suggest() const
{
    return m_settings.boolean(kSuggest);
}

void KanjiTimerController::setSuggest(bool on)
{
    if (on == suggest())
        return;
    m_settings.set(kSuggest, on);
    if (m_timer)
        m_timer->setSuggest(on);
    emit changed();
}

bool KanjiTimerController::multi() const
{
    return m_settings.boolean(kMulti);
}

void KanjiTimerController::setMulti(bool on)
{
    if (on == multi())
        return;
    m_settings.set(kMulti, on);
    if (m_timer)
        m_timer->setMulti(on);
    emit changed();
}

bool KanjiTimerController::furigana() const
{
    return m_settings.boolean(kFurigana);
}

void KanjiTimerController::setFurigana(bool on)
{
    if (on == furigana())
        return;
    m_settings.set(kFurigana, on);
    if (m_timer)
        configure(*m_timer);
    emit changed();
}

bool KanjiTimerController::noDictionary() const
{
    return furigana() && m_dictionary && !*m_dictionary;
}

void KanjiTimerController::configure(application::KanjiTimer &timer) const
{
    timer.setMulti(multi());
    timer.setFurigana(furigana());
    if (!furigana() || timer.hasDictionary())
        return;
    if (!m_dictionary)
        m_dictionary = m_hooks.dictionary ? m_hooks.dictionary() : application::ReadingAnalyzer{};
    if (*m_dictionary)
        timer.setDictionary(*m_dictionary);
}

void KanjiTimerController::open()
{
    m_open = true;
    readLines();
    emit changed();
}

void KanjiTimerController::close()
{
    m_open = false;
}

void KanjiTimerController::documentEdited()
{
    if (!m_open)
        return;
    readLines();
    emit changed();
}

void KanjiTimerController::setStyles(const QString &romaji, const QString &kanji)
{
    if (romaji == m_romajiStyle && kanji == m_kanjiStyle)
        return;
    m_romajiStyle = romaji;
    m_kanjiStyle = kanji;
    m_timer.reset();
    readLines();
    emit changed();
}

void KanjiTimerController::guessStyles()
{
    auto *session = target();
    m_romajiStyle.clear();
    m_kanjiStyle.clear();
    if (!session)
        return;
    std::map<QString, int> karaoke, lines;
    for (const auto *line : session->document().lines()) {
        const QString style = qs8(line->style);
        if (line->text.empty())
            continue;
        ++lines[style];
        if (application::hasKaraokeTags(core::toUtf16(line->text)))
            ++karaoke[style];
    }
    int most = 0;
    for (const QString &style : m_styles)
        if (karaoke[style] > most) {
            most = karaoke[style];
            m_romajiStyle = style;
        }
    if (m_romajiStyle.isEmpty() && !m_styles.isEmpty())
        m_romajiStyle = m_styles.first();
    for (const QString &style : m_styles)
        if (style != m_romajiStyle &&
            (style.contains(QLatin1String("kanji"), Qt::CaseInsensitive) || style.contains(QLatin1String("jp"), Qt::CaseInsensitive))) {
            m_kanjiStyle = style;
            return;
        }
    const auto at = m_styles.indexOf(m_romajiStyle);
    for (qsizetype i = 1; i <= m_styles.size(); ++i) {
        const QString &style = m_styles[(at + i) % m_styles.size()];
        if (style != m_romajiStyle && lines[style] > 0) {
            m_kanjiStyle = style;
            return;
        }
    }
    for (const QString &style : m_styles)
        if (style != m_romajiStyle) {
            m_kanjiStyle = style;
            return;
        }
}

void KanjiTimerController::readLines()
{
    auto *session = target();
    const std::uint64_t document = session && m_hooks.targetId ? m_hooks.targetId() : 0;
    if (document != m_document) {
        if (m_document != 0)
            m_kept[m_document] = Kept{std::move(m_timer), m_romajiStyle, m_kanjiStyle};
        m_document = document;
        m_timer.reset();
        m_romajiStyle.clear();
        m_kanjiStyle.clear();
        if (auto kept = m_kept.find(document); kept != m_kept.end()) {
            m_timer = std::move(kept->second.timer);
            m_romajiStyle = kept->second.romajiStyle;
            m_kanjiStyle = kept->second.kanjiStyle;
            m_kept.erase(kept);
            if (m_timer && m_timer->suggest() != suggest())
                m_timer->setSuggest(suggest());
        }
    }
    m_styles.clear();
    if (session)
        if (const auto styles = application::documentStyles(*session))
            for (const auto &s : *styles)
                m_styles << qs8(s.name);
    if (!m_styles.contains(m_romajiStyle) || !m_styles.contains(m_kanjiStyle)) {
        m_timer.reset();
        guessStyles();
    }
    m_problem.clear();
    if (!session || m_styles.isEmpty()) {
        m_problem = QStringLiteral("noStyles");
        m_timer.reset();
        return;
    }
    if (m_romajiStyle == m_kanjiStyle) {
        m_problem = QStringLiteral("same");
        m_timer.reset();
        return;
    }
    auto [romaji, kanji] = styleLines(*session, m_romajiStyle, m_kanjiStyle);
    if (romaji.empty()) {
        m_problem = QStringLiteral("noKaraoke");
        m_timer.reset();
        return;
    }
    if (!m_timer) {
        m_timer = std::make_unique<application::KanjiTimer>(charClasses());
        m_timer->setSuggest(suggest());
    }
    configure(*m_timer);
    m_timer->setLines(std::move(romaji), std::move(kanji));
}

QVariantList KanjiTimerController::pairs() const
{
    QVariantList out;
    if (!m_timer)
        return out;
    const auto view = m_timer->view();
    int number = 0;
    for (const auto &p : m_timer->pairs()) {
        QVariantMap row{{QStringLiteral("number"), ++number},
                        {QStringLiteral("kanji"), p.kanji ? shown(p.kanji->text) : QString()},
                        {QStringLiteral("romaji"), p.romaji ? shown(p.romaji->text) : QString()},
                        {QStringLiteral("status"), statusKey(p.status)}};
        if (number - 1 == m_timer->current() && view) {
            const bool selection = view->selection.syllables || view->selection.characters;
            row.insert(QStringLiteral("linked"), static_cast<int>(view->linked.size()));
            row.insert(QStringLiteral("groups"),
                       static_cast<int>(view->linked.size() + (selection ? 1 : 0) + view->rest.size()));
        }
        out << row;
    }
    return out;
}

int KanjiTimerController::current() const
{
    return m_timer ? m_timer->current() : -1;
}

QVariantMap KanjiTimerController::pair() const
{
    const auto v = m_timer ? m_timer->view() : std::nullopt;
    if (!v)
        return {};
    const auto &p = m_timer->pairs()[static_cast<std::size_t>(m_timer->current())];
    const std::size_t groupCount = v->linked.size() + (v->selection.syllables || v->selection.characters ? 1 : 0) + v->rest.size();
    const std::size_t shownFrom = v->linked.size() > kShownGroups / 2 ? v->linked.size() - kShownGroups / 2 : 0;
    const std::size_t shownTo = std::min(groupCount, shownFrom + kShownGroups);
    std::size_t index = 0;
    QVariantList groups;
    std::size_t syllable = 0, character = 0;
    auto syllablesOf = [&](std::size_t from, std::size_t count) {
        QVariantList out;
        for (std::size_t k = from; k < from + std::min(count, kShownGroups) && k < v->syllables.size(); ++k)
            out << QVariantMap{{QStringLiteral("text"), qs(v->syllables[k].text).trimmed()},
                               {QStringLiteral("cs"), v->syllables[k].cs},
                               {QStringLiteral("index"), static_cast<int>(k)}};
        return out;
    };
    auto charactersOf = [&](std::size_t from, std::size_t count) {
        QString out;
        for (std::size_t c = from; c < from + count && c < v->kanji.units.size(); ++c)
            out += qs(v->kanji.units[c].text);
        return out;
    };
    QVariantMap detail;
    std::vector<std::vector<const application::KanjiReading *>> runsOf(groupCount);
    for (const auto &r : v->readings)
        if (r.group < groupCount)
            runsOf[r.group].push_back(&r);
    auto joined = [](const std::vector<const application::KanjiReading *> &runs, auto text) {
        QStringList out;
        for (const auto *r : runs)
            out << qs(text(*r));
        return out.join(QLatin1Char(' ')).trimmed();
    };
    auto add = [&](const application::KanjiGroup &g, const QString &kind) {
        const auto &runs = runsOf[index];
        const bool shown = index >= shownFrom && index < shownTo;
        ++index;
        const QString kanji = shown || kind == QLatin1String("cur") ? charactersOf(character, g.characters) : QString();
        const bool typed = std::ranges::any_of(runs, [](const auto *r) { return r->typed.has_value(); });
        const QString reading = joined(runs, [](const auto &r) { return r.reading(); });
        if (shown) {
            QVariantMap group{{QStringLiteral("kind"), kind},
                              {QStringLiteral("syllables"), syllablesOf(syllable, g.syllables)},
                              {QStringLiteral("kanji"), kanji},
                              {QStringLiteral("first"), static_cast<int>(character)},
                              {QStringLiteral("characters"), static_cast<int>(g.characters)},
                              {QStringLiteral("blank"), g.characters == 0}};
            if (!runs.empty())
                group.insert(QStringLiteral("furigana"),
                             QVariantMap{{QStringLiteral("reading"), reading},
                                         {QStringLiteral("typed"), typed},
                                         {QStringLiteral("differs"),
                                          std::ranges::any_of(runs, [](const auto *r) { return r->differs; })}});
            groups << group;
        }
        if (kind == QLatin1String("cur")) {
            std::u16string romaji;
            int cs = 0;
            for (std::size_t k = syllable; k < syllable + g.syllables && k < v->syllables.size(); ++k) {
                romaji += v->syllables[k].text;
                cs += v->syllables[k].cs;
            }
            const auto sung = application::romajiToHiragana(romaji);
            detail = {{QStringLiteral("kanji"), kanji},
                      {QStringLiteral("romaji"), qs(romaji).simplified()},
                      {QStringLiteral("reading"), sung && hasKanji(kanji.toStdU16String()) ? qs(*sung) : QString()},
                      {QStringLiteral("cs"), cs},
                      {QStringLiteral("blank"), g.characters == 0}};
            if (!runs.empty()) {
                QString dictionary;
                for (const auto *r : runs)
                    if (r->differs && dictionary.isEmpty())
                        dictionary = qs(r->dictionary);
                const QString suggestion = joined(runs, [](const auto &r) { return r.suggested; });
                detail.insert(QStringLiteral("furigana"),
                              QVariantMap{{QStringLiteral("reading"), reading},
                                          {QStringLiteral("suggestion"), suggestion},
                                          {QStringLiteral("typed"), typed},
                                          {QStringLiteral("dictionary"), dictionary},
                                          {QStringLiteral("notSung"),
                                           !suggestion.isEmpty() && std::ranges::none_of(runs, [](const auto *r) { return r->sung; })}});
            }
        }
        syllable += g.syllables;
        character += g.characters;
    };
    for (const auto &g : v->linked)
        add(g, QStringLiteral("done"));
    const bool selection = v->selection.syllables || v->selection.characters;
    if (selection)
        add(v->selection, QStringLiteral("cur"));
    for (const auto &g : v->rest)
        add(g, QStringLiteral("sug"));
    QVariantList restKanji;
    for (std::size_t c = character; c < v->kanji.units.size() && c < character + kShownGroups; ++c)
        restKanji << QVariantMap{{QStringLiteral("text"), qs(v->kanji.units[c].text)},
                                 {QStringLiteral("index"), static_cast<int>(c)}};
    QString noKanjiLeft;
    if (selection && v->selection.characters == 0) {
        std::size_t used = 0, from = 0;
        for (const auto &g : v->linked) {
            used += g.characters;
            from += g.syllables;
        }
        if (used >= v->kanji.units.size()) {
            QStringList texts;
            for (const auto &syl : syllablesOf(from, v->selection.syllables))
                texts << syl.toMap().value(QStringLiteral("text")).toString();
            noKanjiLeft = texts.join(QLatin1Char(' '));
        }
    }
    QVariantList result;
    int total = 0;
    for (const auto &s : v->syllables)
        total += s.cs;
    if (v->output) {
        const QString text = qs(*v->output);
        qsizetype i = 0;
        while (i < text.size()) {
            const bool tag = text[i] == QLatin1Char('{');
            const qsizetype end = tag ? (text.indexOf(QLatin1Char('}'), i) < 0 ? text.size() : text.indexOf(QLatin1Char('}'), i) + 1)
                                      : (text.indexOf(QLatin1Char('{'), i) < 0 ? text.size() : text.indexOf(QLatin1Char('{'), i));
            if (result.size() >= qsizetype(4 * kShownGroups)) {
                result << QVariantMap{{QStringLiteral("tag"), false}, {QStringLiteral("text"), QStringLiteral("…")}};
                break;
            }
            result << QVariantMap{{QStringLiteral("tag"), tag}, {QStringLiteral("text"), text.mid(i, end - i)}};
            i = end;
        }
    }
    bool canPrevious = false;
    for (int q = m_timer->current() - 1; q >= 0 && !canPrevious; --q)
        canPrevious = m_timer->pairs()[static_cast<std::size_t>(q)].romaji && m_timer->pairs()[static_cast<std::size_t>(q)].kanji;
    return {{QStringLiteral("times"), assTime(p.romaji->startMs) + QStringLiteral(" – ") + assTime(p.romaji->endMs)},
            {QStringLiteral("duration"), (p.romaji->endMs - p.romaji->startMs) / 10},
            {QStringLiteral("groups"), groups},
            {QStringLiteral("earlier"), static_cast<int>(shownFrom)},
            {QStringLiteral("later"), static_cast<int>(groupCount - shownTo)},
            {QStringLiteral("restSyllables"), syllablesOf(syllable, v->syllables.size() - std::min(syllable, v->syllables.size()))},
            {QStringLiteral("restKanji"), restKanji},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("noKanjiLeft"), noKanjiLeft},
            {QStringLiteral("canAccept"), v->canAccept},
            {QStringLiteral("canUnlink"), !v->linked.empty()},
            {QStringLiteral("canPrevious"), canPrevious},
            {QStringLiteral("result"), result},
            {QStringLiteral("total"), total}};
}

int KanjiTimerController::pending() const
{
    return m_timer ? static_cast<int>(m_timer->pending().size()) : 0;
}

bool KanjiTimerController::canUndo() const
{
    return m_timer && m_timer->canUndo();
}

bool KanjiTimerController::canRedo() const
{
    return m_timer && m_timer->canRedo();
}

bool KanjiTimerController::done(bool ok)
{
    if (ok)
        emit changed();
    return ok;
}

bool KanjiTimerController::growSyllables() { return done(m_timer && m_timer->growSyllables()); }
bool KanjiTimerController::shrinkSyllables() { return done(m_timer && m_timer->shrinkSyllables()); }
bool KanjiTimerController::growCharacters() { return done(m_timer && m_timer->growCharacters()); }
bool KanjiTimerController::shrinkCharacters() { return done(m_timer && m_timer->shrinkCharacters()); }
bool KanjiTimerController::link() { return done(m_timer && m_timer->link()); }
bool KanjiTimerController::unlink() { return done(m_timer && m_timer->unlink()); }
bool KanjiTimerController::enter() { return done(m_timer && m_timer->enter()); }
bool KanjiTimerController::acceptLine() { return done(m_timer && m_timer->acceptLine()); }
bool KanjiTimerController::skipRomaji() { return done(m_timer && m_timer->skipRomaji()); }
bool KanjiTimerController::skipKanji() { return done(m_timer && m_timer->skipKanji()); }
bool KanjiTimerController::previous() { return done(m_timer && m_timer->previous()); }
bool KanjiTimerController::choose(int pair) { return done(m_timer && m_timer->choose(pair)); }
bool KanjiTimerController::undo() { return done(m_timer && m_timer->undo()); }
bool KanjiTimerController::redo() { return done(m_timer && m_timer->redo()); }

bool KanjiTimerController::clickSyllable(int syllable)
{
    return done(m_timer && syllable >= 0 && m_timer->clickSyllable(static_cast<std::size_t>(syllable)));
}

bool KanjiTimerController::setReading(const QString &reading)
{
    return done(m_timer && m_timer->setReading(reading.trimmed().toStdU16String()));
}

bool KanjiTimerController::useSuggestion() { return done(m_timer && m_timer->useSuggestion()); }

bool KanjiTimerController::clickCharacter(int character)
{
    return done(m_timer && character >= 0 && m_timer->clickCharacter(static_cast<std::size_t>(character)));
}

int KanjiTimerController::acceptAll()
{
    const int accepted = m_timer ? m_timer->acceptAllSuggestions() : 0;
    done(accepted > 0);
    return accepted;
}

namespace {

auto writePending(const application::KanjiTimer &timer, application::EditSession &session)
{
    const auto pending = timer.pending();
    std::set<core::LineId> touches;
    for (const auto &[id, text] : pending)
        touches.insert(id);
    return session.run(application::Command{"Kanji timing", session.revision(), touches, [&](core::Document &d) {
                                                for (const auto &[id, text] : pending)
                                                    if (!d.editLine(id, [&](core::LineRecord &l) { l.text = core::toUtf8(text); }))
                                                        return false;
                                                return true;
                                            }});
}

}

bool KanjiTimerController::apply()
{
    auto *session = target();
    if (!m_timer || !session || !m_hooks.targetId || m_hooks.targetId() != m_document)
        return false;
    auto run = [&] { return writePending(*m_timer, *session); };
    if (m_timer->pending().empty())
        return true;
    auto ran = run();
    // MUST KILL: a Line editor draft committed first makes the run stale; the retry rereads.
    if (!ran && ran.error() == application::CommandRefusal::StaleRevision) {
        readLines();
        if (!m_timer)
            return false;
        if (m_timer->pending().empty()) {
            emit changed();
            return true;
        }
        ran = run();
    }
    if (!ran)
        return false;
    m_timer->applied();
    if (m_hooks.refresh)
        m_hooks.refresh();
    emit changed();
    return true;
}

void KanjiTimerController::discard()
{
    m_timer.reset();
    if (m_open)
        readLines();
    emit changed();
}

application::KanjiTimer *KanjiTimerController::timerFor(std::uint64_t document) const
{
    if (document == m_document)
        return m_timer.get();
    const auto kept = m_kept.find(document);
    return kept != m_kept.end() ? kept->second.timer.get() : nullptr;
}

int KanjiTimerController::pendingFor(std::uint64_t document) const
{
    const auto *timer = timerFor(document);
    return timer ? static_cast<int>(timer->pending().size()) : 0;
}

bool KanjiTimerController::applyTo(std::uint64_t document, application::EditSession &session)
{
    if (document == m_document && target() == &session && m_hooks.targetId && m_hooks.targetId() == document) {
        readLines();
        if (!m_timer || m_timer->pending().empty()) {
            emit changed();
            return true;
        }
        return apply();
    }
    auto *timer = timerFor(document);
    if (!timer)
        return true;
    const auto kept = m_kept.find(document);
    const bool own = document == m_document;
    auto [romaji, kanji] = styleLines(session, own ? m_romajiStyle : kept->second.romajiStyle,
                                      own ? m_kanjiStyle : kept->second.kanjiStyle);
    timer->setLines(std::move(romaji), std::move(kanji));
    configure(*timer);
    if (timer->pending().empty())
        return true;
    if (!writePending(*timer, session))
        return false;
    timer->applied();
    emit changed();
    return true;
}

void KanjiTimerController::forget(std::uint64_t document)
{
    m_kept.erase(document);
    if (document != m_document)
        return;
    m_timer.reset();
    m_document = 0;
    if (m_open)
        readLines();
    emit changed();
}

}
