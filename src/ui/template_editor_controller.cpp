#include "template_editor_controller.h"

#include "hikari/application/macro_transaction.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/editor_syntax.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>

#include <algorithm>

namespace hikari::ui {

namespace {

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(b.begin(), b.end());
}

QString translated(const char *text)
{
    return QCoreApplication::translate("HikariSub.TemplateEditor", text);
}

// The time a Line's text shows, without its override blocks.
QString withoutTags(const std::u8string &text)
{
    QString out;
    int depth = 0;
    for (const QChar c : qs(text)) {
        if (c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char('}') && depth > 0)
            --depth;
        else if (depth == 0)
            out += c;
    }
    return out.replace(QLatin1String("\\N"), QLatin1String(" ")).simplified();
}

std::int64_t msOf(const core::TimeField &t)
{
    return t.value.microseconds() / 1000;
}

QJsonObject reportOf(const std::string &report)
{
    const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(report)).object();
    return o.value(QLatin1String("report")).toString() == QLatin1String("karaoke-templater") ? o : QJsonObject{};
}

// The stock variables (kara-templater.lua apply_line and set_ctx_syl), in
// the order the completion list shows them.
struct Variable {
    const char *name;
    const char *help;
};

const std::vector<Variable> &variables()
{
    //: Template variables' descriptions in the template editor's list (karaoke templater docs)
    static const std::vector<Variable> list = {
        {"sstart", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "syllable start, from the Line's start (ms)")},
        {"send", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "syllable end (ms)")},
        {"smid", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "middle of the syllable (ms)")},
        {"sdur", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "syllable duration (ms)")},
        {"skdur", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "syllable duration in centiseconds")},
        {"si", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "number of the syllable in the Line")},
        {"sleft", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "left edge of the syllable")},
        {"scenter", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "horizontal centre of the syllable")},
        {"sright", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "right edge of the syllable")},
        {"swidth", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "width of the syllable (px)")},
        {"stop", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "top of the syllable")},
        {"smiddle", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "vertical middle of the syllable")},
        {"sbottom", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "bottom of the syllable")},
        {"sheight", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "height of the syllable (px)")},
        {"sx", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "syllable position for \\pos, by alignment")},
        {"sy", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "syllable position for \\pos, by alignment")},
        {"layer", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the Line's layer")},
        {"lstart", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Line start (ms)")},
        {"lend", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Line end (ms)")},
        {"ldur", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Line duration (ms)")},
        {"lmid", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "middle of the Line (ms)")},
        {"style", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the Line's Style")},
        {"actor", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the Line's actor")},
        {"margin_l", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "left margin")},
        {"margin_r", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "right margin")},
        {"margin_v", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "vertical margin")},
        {"margin_t", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "top margin")},
        {"margin_b", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "bottom margin")},
        {"syln", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "number of syllables in the Line")},
        {"li", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "number of the Line")},
        {"lleft", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "left edge of the Line")},
        {"lcenter", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "horizontal centre of the Line")},
        {"lright", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "right edge of the Line")},
        {"lwidth", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "width of the Line (px)")},
        {"ltop", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "top of the Line")},
        {"lmiddle", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "vertical middle of the Line")},
        {"lbottom", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "bottom of the Line")},
        {"lheight", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "height of the Line (px)")},
        {"lx", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Line position for \\pos, by alignment")},
        {"ly", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Line position for \\pos, by alignment")},
        {"start", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sstart in a syllable template, lstart in a line one")},
        {"end", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "send in a syllable template, lend in a line one")},
        {"dur", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sdur or ldur")},
        {"kdur", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "skdur, or the Line's duration in centiseconds")},
        {"mid", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "smid or lmid")},
        {"i", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "si or li")},
        {"left", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sleft or lleft")},
        {"center", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "scenter or lcenter")},
        {"right", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sright or lright")},
        {"width", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "swidth or lwidth")},
        {"top", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "stop or ltop")},
        {"middle", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "smiddle or lmiddle")},
        {"bottom", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sbottom or lbottom")},
        {"height", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sheight or lheight")},
        {"x", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sx or lx")},
        {"y", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sy or ly")},
    };
    return list;
}

// The template environment (kara-templater.lua apply_templates), for `!`.
struct Function {
    const char *name;
    const char *insert;
    const char *help;
};

const std::vector<Function> &functions()
{
    static const std::vector<Function> list = {
        {"retime", "retime(\"syl\", 0, 0)",
         QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sets the generated Line's time, by mode")},
        {"relayer", "relayer(1)", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sets the generated Line's layer")},
        {"restyle", "restyle(\"\")", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sets the generated Line's Style")},
        {"maxloop", "maxloop(2)", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "runs the template this many times")},
        {"loopctl", "loopctl(1, 2)", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "sets the loop's counter and count")},
        {"remember", "remember(\"name\", 0)",
         QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "keeps a value for recall, and gives it")},
        {"remember_if", "remember_if(\"name\", 0, true)",
         QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "keeps a value when the condition holds")},
        {"recall", "recall.name", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "a value kept by remember")},
        {"j", "j", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the loop's counter, from 1")},
        {"maxj", "maxj", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the loop's count")},
        {"line", "line.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the Line being generated")},
        {"orgline", "orgline.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the karaoke Line")},
        {"syl", "syl.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the syllable")},
        {"basesyl", "basesyl.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the syllable a furigana part is on")},
        {"meta", "meta.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "the script's resolution and properties")},
        {"math", "math.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Lua's math library")},
        {"string", "string.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Lua's string library")},
        {"_G", "_G.", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "every global, karaskel's included")},
    };
    return list;
}

constexpr char kValueSeparator = '\x1e';

const QStringList &templateClasses()
{
    static const QStringList list = {QStringLiteral("pre-line"), QStringLiteral("line"), QStringLiteral("syl"),
                                     QStringLiteral("char"), QStringLiteral("furi")};
    return list;
}

const QStringList &codeClasses()
{
    static const QStringList list = {QStringLiteral("once"), QStringLiteral("line"), QStringLiteral("syl"),
                                     QStringLiteral("furi")};
    return list;
}

// Modifiers that take the word after them.
bool takesArgument(const QString &word)
{
    const QString w = word.toLower();
    return w == QLatin1String("loop") || w == QLatin1String("repeat") || w == QLatin1String("fx") ||
           w == QLatin1String("fxgroup");
}

const QStringList &modifierWords()
{
    static const QStringList list = {QStringLiteral("all"),     QStringLiteral("repeat"),   QStringLiteral("loop"),
                                     QStringLiteral("notext"),  QStringLiteral("keeptags"), QStringLiteral("noblank"),
                                     QStringLiteral("multi"),   QStringLiteral("fx"),       QStringLiteral("fxgroup")};
    return list;
}

// The Effect split into its kind, class (with a line template's name) and
// modifiers.
struct EffectWords {
    QString kind;
    QStringList classWords; // the class, and a named line template's name
    QStringList modifiers;
};

EffectWords splitEffect(const QString &effect)
{
    EffectWords out;
    QStringList words = effect.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return out;
    out.kind = words.takeFirst();
    const QString kind = out.kind.toLower();
    const QStringList &classes = kind == QLatin1String("code") ? codeClasses() : templateClasses();
    if (!words.isEmpty() && classes.contains(words.front().toLower())) {
        out.classWords << words.takeFirst();
        const QString cls = out.classWords.front().toLower();
        if (kind == QLatin1String("template") && (cls == QLatin1String("line") || cls == QLatin1String("pre-line")) &&
            !words.isEmpty() && !modifierWords().contains(words.front().toLower()) &&
            !templateClasses().contains(words.front().toLower()))
            out.classWords << words.takeFirst();
    }
    out.modifiers = words;
    return out;
}

QString joinEffect(const EffectWords &w)
{
    QStringList all;
    all << w.kind << w.classWords << w.modifiers;
    return all.join(QLatin1Char(' '));
}

} // namespace

TemplateEditorController::TemplateEditorController(QObject *parent) : QObject(parent)
{
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(150);
    connect(&m_previewTimer, &QTimer::timeout, this, &TemplateEditorController::runPreview);
    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(800);
    connect(&m_commitTimer, &QTimer::timeout, this, &TemplateEditorController::commit);
}

TemplateEditorController::~TemplateEditorController() = default;

void TemplateEditorController::setSessionSource(SessionSource source)
{
    m_sessionSource = std::move(source);
    refresh();
}

void TemplateEditorController::setDocumentKey(DocumentKeySource key)
{
    m_documentKey = std::move(key);
}

void TemplateEditorController::setSessionOf(SessionOf sessionOf)
{
    m_sessionOf = std::move(sessionOf);
}

void TemplateEditorController::setTemplater(application::KaraokeTemplaterPort *templater)
{
    m_templater = templater;
    refresh();
}

void TemplateEditorController::setEditable(bool editable)
{
    if (editable == m_editable)
        return;
    m_editable = editable;
    emit changed();
}

void TemplateEditorController::setDelays(int previewMs, int commitMs)
{
    m_previewTimer.setInterval(previewMs);
    m_commitTimer.setInterval(commitMs);
}

application::EditSession *TemplateEditorController::session() const
{
    return m_sessionSource ? m_sessionSource() : nullptr;
}

const core::Document *TemplateEditorController::document() const
{
    const auto *s = session();
    return s ? &s->document() : nullptr;
}

const core::LineRecord *TemplateEditorController::line(core::LineId id) const
{
    const auto *doc = document();
    if (!doc)
        return nullptr;
    for (const auto *l : doc->lines())
        if (l->id == id)
            return l;
    return nullptr;
}

std::optional<core::LineRecord> TemplateEditorController::currentRecord() const
{
    const auto *l = m_current ? line(*m_current) : nullptr;
    if (!l)
        return std::nullopt;
    if (m_pending && m_pending->line == l->id && m_pending->document == documentKey())
        return edited(*l, *m_pending);
    return *l;
}

core::LineRecord TemplateEditorController::edited(core::LineRecord line, const Pending &pending)
{
    // In TLMode the templater reads the translation (macroLineOf): the panel
    // edits that.
    if (pending.text)
        (line.translation.empty() ? line.text : line.translation) = *pending.text;
    if (pending.effect)
        line.effect = *pending.effect;
    if (pending.style)
        line.style = *pending.style;
    if (pending.layer)
        line.layer = core::IntField{u8(QString::number(*pending.layer)), *pending.layer};
    return line;
}

application::EditSession *TemplateEditorController::pendingSession() const
{
    if (!m_pending)
        return nullptr;
    if (m_pending->document == documentKey())
        return session();
    return m_sessionOf ? m_sessionOf(m_pending->document) : nullptr;
}

std::string TemplateEditorController::keyOf(const core::LineRecord &l) const
{
    const auto join = [](const std::u8string &s) { return std::string(s.begin(), s.end()); };
    return join(l.effect) + '\x1f' + join(l.style) + '\x1f' + std::to_string(l.layer.value) + '\x1f' + join(l.text) +
           '\x1f' + join(l.translation);
}

void TemplateEditorController::takeBaseline()
{
    m_baseline.clear();
    if (const auto *doc = document())
        for (const auto *l : doc->lines())
            if (application::templateKindOf(*l))
                m_baseline[l->id] = keyOf(*l);
}

void TemplateEditorController::refresh()
{
    const auto *s = session();
    if (s != m_seenSession) {
        commit(); // to the Document it was typed for, while that is open
        m_seenSession = s;
        m_pending.reset();
        m_commitTimer.stop();
        m_current.reset();
        m_source.reset();
        m_lastSource.reset();
        m_preview.reset();
        m_own.clear();
        m_issues.clear();
        m_runError.clear();
        m_stats.clear();
        m_replaced.clear();
        m_matched.clear();
        m_values.clear();
        m_lastApply.clear();
        m_previewSnapshotKey.clear();
        stopRuns();
        takeBaseline();
    }
    if (!s) {
        emit changed();
        emit previewChanged();
        emit videoChanged();
        return;
    }
    const auto &doc = s->document();
    if (const auto key = documentKey(); m_seenDocuments.insert(key).second &&
                                        std::ranges::none_of(doc.lines(), [](const core::LineRecord *l) {
                                            return application::templateKindOf(*l).has_value();
                                        }))
        m_trusted[key] = {{}, sourceFields()};
    // The current template follows the active Line when that is one.
    const auto active = s->selection().active;
    if (const auto *l = active ? line(*active) : nullptr; l && application::templateKindOf(*l) && active != m_current)
        setCurrent(active->value);
    if (m_current && (!line(*m_current) || !application::templateKindOf(*line(*m_current)))) {
        m_pending.reset();
        m_current.reset();
    }
    if (!m_current)
        for (const auto *l : doc.lines())
            if (application::templateKindOf(*l)) {
                m_current = l->id;
                break;
            }
    for (auto it = m_replaced.begin(); it != m_replaced.end();)
        it = line(it->first) ? std::next(it) : m_replaced.erase(it);
    updateSource();
    // The templater's helper loaded after the panel showed: the preview
    // skipped then runs now.
    const bool ready = available();
    const bool becameReady = ready && !m_wasAvailable;
    m_wasAvailable = ready;
    if (becameReady)
        m_previewSnapshotKey.clear(); // a reloaded script may write otherwise
    if (s->revision() != m_seenRevision || becameReady) {
        m_seenRevision = s->revision();
        if (s->revision() != m_applyRevision)
            m_lastApply.clear(); // an Undo or an edit since Apply
        schedulePreview();
    }
    emit changed();
}

void TemplateEditorController::updateSource()
{
    const auto *s = session();
    const std::optional<core::LineId> source =
        s ? application::templatePreviewSource(s->document(), s->selection().active, m_lastSource) : std::nullopt;
    if (source == m_source)
        return;
    m_source = source;
    if (source)
        m_lastSource = source;
    stopRuns(); // their answers are another Line's
    m_preview.reset();
    m_own.clear();
    m_previewKept = false;
    m_previewSeconds = 0;
    m_values.clear();
    m_previewSnapshotKey.clear();
    emit previewChanged();
    emit valuesChanged();
    emit videoChanged();
    schedulePreview();
}

void TemplateEditorController::stopRuns()
{
    ++m_generation;
    m_running = false;
    m_previewing = false;
    if (m_templater)
        m_templater->cancelPreview();
}

void TemplateEditorController::setShown(bool shown)
{
    if (shown == m_shown)
        return;
    m_shown = shown;
    if (shown) {
        m_previewSnapshotKey.clear();
        refresh();
        schedulePreview();
    } else {
        commit();
        m_previewTimer.stop();
        stopRuns();
    }
    emit changed();
    emit previewChanged();
    emit videoChanged();
}

bool TemplateEditorController::available() const
{
    return session() && m_templater && m_templater->available();
}

int TemplateEditorController::templateCount() const
{
    int n = 0;
    if (const auto *doc = document())
        for (const auto *l : doc->lines())
            n += application::templateKindOf(*l) == application::TemplateKind::Template;
    return n;
}

int TemplateEditorController::codeCount() const
{
    int n = 0;
    if (const auto *doc = document())
        for (const auto *l : doc->lines())
            n += application::templateKindOf(*l) == application::TemplateKind::Code;
    return n;
}

int TemplateEditorController::karaokeCount() const
{
    int n = 0;
    if (const auto *doc = document())
        for (const auto *l : doc->lines())
            n += application::isKaraokeLine(*l);
    return n;
}

QVariantList TemplateEditorController::templates() const
{
    QVariantList out;
    const auto *doc = document();
    if (!doc)
        return out;
    int number = 0;
    for (const auto *l : doc->lines()) {
        ++number;
        const auto kind = application::templateKindOf(*l);
        if (!kind)
            continue;
        const core::LineRecord r = (m_current == l->id) ? *currentRecord() : *l;
        const auto stats = m_stats.find(l->id);
        const auto base = m_baseline.find(l->id);
        const bool problem = std::ranges::any_of(m_issues, [&](const Issue &i) { return i.line == l->id; });
        out.append(QVariantMap{
            {QStringLiteral("id"), QVariant::fromValue<qulonglong>(l->id.value)},
            {QStringLiteral("number"), number},
            {QStringLiteral("kind"), *kind == application::TemplateKind::Code ? QStringLiteral("code")
                                                                               : QStringLiteral("template")},
            {QStringLiteral("effect"), qs(r.effect)},
            {QStringLiteral("style"), qs(r.style)},
            {QStringLiteral("text"), qs(r.text)},
            {QStringLiteral("generated"), stats == m_stats.end() ? -1 : stats->second.generated},
            {QStringLiteral("seconds"), stats == m_stats.end() ? -1.0 : stats->second.seconds},
            {QStringLiteral("changed"), base == m_baseline.end() || base->second != keyOf(r)},
            {QStringLiteral("problem"), problem},
        });
    }
    return out;
}

void TemplateEditorController::setCurrent(qulonglong id)
{
    const core::LineId next{id};
    if (m_current == next)
        return;
    const auto *l = line(next);
    if (!l || !application::templateKindOf(*l))
        return;
    commit();
    m_current = next;
    emit changed();
    emit previewChanged();
    schedulePreview();
}

int TemplateEditorController::currentNumber() const
{
    const auto *doc = document();
    if (!doc || !m_current)
        return 0;
    const auto lines = doc->lines();
    const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == *m_current; });
    return it == lines.end() ? 0 : static_cast<int>(it - lines.begin()) + 1;
}

bool TemplateEditorController::currentIsCode() const
{
    const auto r = currentRecord();
    return r && application::templateKindOf(*r) == application::TemplateKind::Code;
}

QString TemplateEditorController::effect() const
{
    const auto r = currentRecord();
    return r ? qs(r->effect) : QString();
}

QString TemplateEditorController::style() const
{
    const auto r = currentRecord();
    return r ? qs(r->style) : QString();
}

int TemplateEditorController::layer() const
{
    const auto r = currentRecord();
    return r ? static_cast<int>(r->layer.value) : 0;
}

QString TemplateEditorController::text() const
{
    // The text the templater reads: the translation in TLMode.
    const auto r = currentRecord();
    return r ? qs(line(r->id)->translation.empty() ? r->text : r->translation) : QString();
}

QStringList TemplateEditorController::styles() const
{
    QStringList out;
    if (const auto *doc = document())
        for (const auto &section : doc->sections())
            for (const auto &record : section.records)
                if (const auto *st = std::get_if<core::StyleRecord>(&record))
                    out << qs(st->name);
    return out;
}

QString TemplateEditorController::problem() const
{
    for (const auto &i : m_issues)
        if (m_current && i.line == *m_current)
            return i.message;
    return m_runError;
}

QVariantList TemplateEditorController::problemRange() const
{
    for (const auto &i : m_issues) {
        if (!m_current || i.line != *m_current || i.start < 1)
            continue;
        const QByteArray bytes = text().toUtf8();
        if (i.start - 1 + i.length > bytes.size())
            return {};
        const int start = static_cast<int>(QString::fromUtf8(bytes.left(i.start - 1)).size());
        const int length = static_cast<int>(QString::fromUtf8(bytes.mid(i.start - 1, i.length)).size());
        return {start, start + length};
    }
    return {};
}

bool TemplateEditorController::hasProblems() const
{
    return !m_issues.empty() || !m_runError.isEmpty();
}

int TemplateEditorController::sourceNumber() const
{
    const auto *doc = document();
    if (!doc || !m_source)
        return 0;
    const auto lines = doc->lines();
    const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == *m_source; });
    return it == lines.end() ? 0 : static_cast<int>(it - lines.begin()) + 1;
}

QString TemplateEditorController::sourceStyle() const
{
    const auto *l = m_source ? line(*m_source) : nullptr;
    return l ? qs(l->style) : QString();
}

QString TemplateEditorController::sourceText() const
{
    const auto *l = m_source ? line(*m_source) : nullptr;
    return l ? withoutTags(l->text) : QString();
}

int TemplateEditorController::sourceStartMs() const
{
    const auto *l = m_source ? line(*m_source) : nullptr;
    return l ? static_cast<int>(msOf(l->start)) : 0;
}

int TemplateEditorController::sourceEndMs() const
{
    const auto *l = m_source ? line(*m_source) : nullptr;
    return l ? static_cast<int>(msOf(l->end)) : 0;
}

QVariantList TemplateEditorController::previewLines() const
{
    QVariantList out;
    if (!m_preview)
        return out;
    for (std::size_t k = 0; k < m_preview->size(); ++k) {
        const auto &l = (*m_preview)[k];
        out.append(QVariantMap{
            {QStringLiteral("layer"), static_cast<int>(l.layer.value)},
            {QStringLiteral("start"), qs(core::legacy::assTimeText(msOf(l.start)))},
            {QStringLiteral("end"), qs(core::legacy::assTimeText(msOf(l.end)))},
            {QStringLiteral("style"), qs(l.style)},
            {QStringLiteral("text"), qs(l.text)},
            {QStringLiteral("own"), k < m_own.size() && m_own[k]},
        });
    }
    return out;
}

void TemplateEditorController::setPreviewOnVideo(bool on)
{
    if (on == m_previewOnVideo)
        return;
    m_previewOnVideo = on;
    emit changed();
    emit previewChanged();
    emit videoChanged();
}

bool TemplateEditorController::videoPreview() const
{
    return m_shown && m_previewOnVideo && m_source && m_preview && line(*m_source);
}

application::TemplatePreviewWindow TemplateEditorController::videoWindow(std::int64_t timeMs, bool playing) const
{
    if (!videoPreview())
        return {};
    const auto *source = line(*m_source);
    std::int64_t from = msOf(source->start), to = msOf(source->end);
    for (const auto &l : *m_preview) {
        from = std::min(from, msOf(l.start));
        to = std::max(to, msOf(l.end));
    }
    return application::templatePreviewWindow(timeMs, playing, from, to);
}

std::optional<core::Document> TemplateEditorController::videoDocument(const core::Document &doc, std::int64_t timeMs,
                                                                      bool playing) const
{
    if (!videoPreview())
        return std::nullopt;
    const auto replaced = m_replaced.find(*m_source);
    return application::templatePreviewDocument(doc, *m_source, *m_preview,
                                                replaced == m_replaced.end() ? std::set<core::LineId>{}
                                                                             : replaced->second,
                                                videoWindow(timeMs, playing));
}

bool TemplateEditorController::canApply() const
{
    const auto *s = session();
    return available() && m_editable && s && !s->isReadOnly() && templateCount() > 0 && !hasProblems() &&
           !previewPending();
}

bool TemplateEditorController::previewPending() const
{
    return m_shown && trusted() && (m_previewTimer.isActive() || m_previewing);
}

TemplateEditorController::DocumentKey TemplateEditorController::documentKey() const
{
    return m_documentKey ? m_documentKey()
                         : DocumentKey{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(session())), 0};
}

TemplateEditorController::TemplateLines TemplateEditorController::templateLines() const
{
    TemplateLines out;
    if (const auto *doc = document())
        for (const auto *l : doc->lines())
            if (application::templateKindOf(*l))
                out[l->id] = formOf(*l);
    return out;
}

std::string TemplateEditorController::formOf(const core::LineRecord &line)
{
    const auto m = application::macroLineOf(line);
    return m.effect + '\n' + m.text;
}

std::set<std::string> TemplateEditorController::sourceFields() const
{
    std::set<std::string> out;
    if (const auto *doc = document())
        for (const auto *l : doc->lines())
            if (!application::templateKindOf(*l) && !application::isGeneratedLine(*l)) {
                auto m = application::macroLineOf(*l);
                out.insert(std::move(m.actor));
                out.insert(std::move(m.style));
            }
    return out;
}

void TemplateEditorController::trust(const TemplateLines &lines, const std::set<std::string> &fields)
{
    auto &trusted = m_trusted[documentKey()];
    for (const auto &[id, runs] : lines)
        trusted.lines[id].insert(runs);
    trusted.fields.insert(fields.begin(), fields.end());
}

bool TemplateEditorController::trusted() const
{
    // A template or code Line come or changed since another way (a paste,
    // an import, a translation pasted over it) is Lua the user has not
    // chosen to run; so is a new Actor or Style the templates put into
    // theirs.
    const auto it = session() ? m_trusted.find(documentKey()) : m_trusted.end();
    if (it == m_trusted.end())
        return false;
    const auto &lines = it->second.lines;
    return std::ranges::all_of(templateLines(),
                               [&](const auto &line) {
                                   const auto forms = lines.find(line.first);
                                   return forms != lines.end() && forms->second.contains(line.second);
                               }) &&
           std::ranges::includes(it->second.fields, sourceFields());
}

void TemplateEditorController::trustDocument()
{
    if (!session() || trusted())
        return;
    trust(templateLines(), sourceFields());
    m_previewSnapshotKey.clear();
    emit changed();
    schedulePreview();
}

int TemplateEditorController::changedNumber() const
{
    for (const QVariant &v : templates())
        if (v.toMap().value(QStringLiteral("changed")).toBool())
            return v.toMap().value(QStringLiteral("number")).toInt();
    return 0;
}

int TemplateEditorController::changedCount() const
{
    int n = 0;
    for (const QVariant &v : templates())
        n += v.toMap().value(QStringLiteral("changed")).toBool();
    return n;
}

void TemplateEditorController::edit(const Pending &change)
{
    if (!m_editable || !m_current || !line(*m_current))
        return;
    if (!m_pending || m_pending->line != *m_current || m_pending->document != documentKey())
        m_pending = Pending{*m_current, {}, {}, {}, {}, documentKey()};
    if (change.text)
        m_pending->text = change.text;
    if (change.effect)
        m_pending->effect = change.effect;
    if (change.style)
        m_pending->style = change.style;
    if (change.layer)
        m_pending->layer = change.layer;
    m_lastApply.clear();
    m_commitTimer.start();
    emit changed();
    schedulePreview();
}

void TemplateEditorController::setText(const QString &text)
{
    if (text != this->text())
        edit(Pending{{}, u8(text), {}, {}, {}});
}

void TemplateEditorController::setEffect(const QString &effect)
{
    if (effect != this->effect())
        edit(Pending{{}, {}, u8(effect), {}, {}});
}

void TemplateEditorController::setStyle(const QString &style)
{
    if (style == this->style())
        return;
    edit(Pending{{}, {}, {}, u8(style), {}});
    commit();
}

void TemplateEditorController::setLayer(int layer)
{
    if (layer == this->layer())
        return;
    edit(Pending{{}, {}, {}, {}, layer});
    commit();
}

void TemplateEditorController::commit()
{
    if (!m_pending)
        return;
    auto *s = pendingSession();
    const core::LineRecord *was = nullptr;
    if (s)
        for (const auto *l : s->document().lines())
            if (l->id == m_pending->line)
                was = l;
    const Pending change = *m_pending;
    if (!was || keyOf(edited(*was, change)) == keyOf(*was)) {
        // Its Document closed, or typing that ended where it began: no step.
        m_pending.reset();
        m_commitTimer.stop();
        emit changed();
        return;
    }
    // The user's edit of a form they chose to run is theirs to run too.
    const auto key = m_trusted.find(change.document);
    const bool trustedForm = application::templateKindOf(*was) && key != m_trusted.end() &&
                             key->second.lines.contains(change.line) &&
                             key->second.lines.at(change.line).contains(formOf(*was));
    application::Command command;
    command.name = "Edit template";
    command.expectedRevision = s->revision();
    command.touches = {change.line};
    command.apply = [change](core::Document &doc) {
        return doc.editLine(change.line, [&](core::LineRecord &l) { l = edited(l, change); });
    };
    if (!s->run(command)) {
        // A macro owns the Document: try again after it.
        m_commitTimer.start();
        return;
    }
    m_pending.reset();
    m_commitTimer.stop();
    if (trustedForm)
        for (const auto *l : s->document().lines())
            if (l->id == change.line && application::templateKindOf(*l))
                key->second.lines[change.line].insert(formOf(*l));
    if (s == session()) {
        m_seenRevision = s->revision();
        emit documentEdited();
    } else {
        // Another tab's, while the views change to this one: they show it
        // after.
        QMetaObject::invokeMethod(this, &TemplateEditorController::documentEdited, Qt::QueuedConnection);
    }
    emit changed();
}

bool TemplateEditorController::apply(bool selectedOnly)
{
    commit();
    // The edit's preview runs now rather than after the pause; Apply waits
    // for it unless the last one already ran on this text.
    if (m_previewTimer.isActive()) {
        m_previewTimer.stop();
        runPreview();
    }
    if (!canApply() || m_pending)
        return false;
    return m_templater->apply(selectedOnly);
}

void TemplateEditorController::applied(const std::string &report)
{
    const QJsonObject o = reportOf(report);
    const auto *doc = document();
    if (o.isEmpty() || !doc)
        return;
    const auto lines = doc->lines();
    m_stats.clear();
    for (const QJsonValue v : o.value(QLatin1String("templates")).toArray()) {
        const QJsonObject t = v.toObject();
        const int number = t.value(QLatin1String("line")).toInt();
        if (number >= 1 && static_cast<std::size_t>(number) <= lines.size())
            m_stats[lines[static_cast<std::size_t>(number) - 1]->id] = {
                t.value(QLatin1String("generated")).toInt(), t.value(QLatin1String("seconds")).toDouble()};
    }
    m_lastApply = {{QStringLiteral("lines"), o.value(QLatin1String("generated")).toInt()},
                   {QStringLiteral("karaokeLines"), o.value(QLatin1String("karaoke_lines")).toInt()},
                   {QStringLiteral("seconds"), o.value(QLatin1String("seconds")).toDouble()},
                   {QStringLiteral("problems"), o.value(QLatin1String("problems")).toInt()}};
    takeBaseline();
    trust(templateLines(), sourceFields()); // the user ran them
    if (const auto *ss = session())
        m_applyRevision = ss->revision();
    // A file's unmarked Lines are marked now; an Undo that brings them back
    // matches again.
    m_replaced.clear();
    m_matched.clear();
    m_previewSnapshotKey.clear();
    emit changed();
}

bool TemplateEditorController::insertTemplate(const QString &effect)
{
    commit();
    auto *s = session();
    if (!s || !m_editable)
        return false;
    const auto lines = s->document().lines();
    const auto first = std::ranges::find_if(lines, [](const core::LineRecord *l) {
        return application::isKaraokeLine(*l) && !application::templateKindOf(*l);
    });
    application::MacroDialogueLine d;
    d.comment = true;
    d.effect = effect.toStdString();
    if (first != lines.end())
        d.style = std::string((*first)->style.begin(), (*first)->style.end());
    else if (const auto names = styles(); !names.isEmpty())
        d.style = names.front().toStdString();
    const core::LineRecord fresh = application::lineOfMacro(d);
    std::optional<core::LineId> added;
    application::Command command;
    command.name = "New template";
    command.expectedRevision = s->revision();
    if (first != lines.end())
        command.touches = {(*first)->id};
    const std::optional<core::LineId> before = first != lines.end() ? std::optional((*first)->id) : std::nullopt;
    command.apply = [&](core::Document &doc) {
        added = before ? doc.insertLineBefore(*before, fresh) : doc.appendLine(fresh);
        return added.has_value();
    };
    if (!s->run(command) || !added)
        return false;
    if (const auto lines = templateLines(); m_trusted.contains(documentKey()) && lines.contains(*added))
        trust({{*added, lines.at(*added)}}); // the user's own
    m_seenRevision = s->revision();
    m_current = *added;
    emit documentEdited();
    emit changed();
    schedulePreview();
    return true;
}

std::map<core::LineId, std::u8string> TemplateEditorController::pendingTexts() const
{
    std::map<core::LineId, std::u8string> out;
    if (m_pending && m_pending->text && m_pending->document == documentKey())
        out[m_pending->line] = *m_pending->text;
    return out;
}

void TemplateEditorController::schedulePreview()
{
    if (!m_shown || !m_templater || !trusted())
        return;
    const bool was = previewPending();
    m_previewTimer.start();
    if (!was)
        emit changed(); // canApply
}

void TemplateEditorController::runPreview()
{
    if (m_preview && templateCount() == 0) {
        // The last template went: its preview and the run making one too.
        stopRuns();
        m_preview.reset();
        m_own.clear();
        m_previewKept = false;
        m_previewSnapshotKey.clear();
        emit previewChanged();
        emit videoChanged();
    }
    if (!m_shown || !available() || !m_source || templateCount() == 0 || !trusted()) {
        m_running = false;
        m_previewing = false;
        emit changed(); // canApply
        return;
    }
    // The pending Effect, Style and Layer reach the run through the record.
    const auto *doc = document();
    core::Document seen = *doc;
    if (m_pending && m_pending->document == documentKey() &&
        (m_pending->effect || m_pending->style || m_pending->layer)) {
        const auto r = currentRecord();
        seen.editLine(r->id, [&](core::LineRecord &l) { l = *r; }, core::ChangeMark::Kept);
    }
    auto snapshot = application::templatePreviewSnapshot(seen, *m_source, pendingTexts());
    if (!snapshot)
        return;
    // What the run reads: Script Info (meta, karaskel's resolution), the
    // styles and the Lines; and the current template, whose own Lines the
    // chain marks.
    std::string key = std::to_string(m_current ? m_current->value : 0) + '\n';
    for (const auto &i : snapshot->info)
        key += i.key + ':' + i.value + '\n';
    for (const auto &d : snapshot->dialogues)
        key += d.raw + '\n' + d.text + '\n';
    for (const auto &st : snapshot->styles)
        for (const auto &f : st.fields)
            key += f + ',';
    if (key == m_previewSnapshotKey && !m_running) {
        emit changed(); // canApply: the preview is this text's
        return;
    }
    m_previewSnapshotKey = key;
    m_previewSnapshot = std::move(snapshot);
    m_previewSeen = std::move(seen);
    const std::uint64_t generation = ++m_generation;
    m_running = true;
    m_previewing = true;
    runChain(generation);
}

void TemplateEditorController::runChain(std::uint64_t generation)
{
    const core::Document &doc = *m_previewSeen;
    const core::LineId source = *m_source;
    const auto lines = doc.lines();
    const bool marked = std::ranges::any_of(lines, [&](const core::LineRecord *l) { return l->generatedFrom == source; });
    std::set<core::LineId> unmarked;
    for (const auto *l : lines)
        if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0)
            unmarked.insert(l->id);
    QPointer<TemplateEditorController> self = this;
    const auto matched = m_matched.find(source);
    if (!marked && !unmarked.empty() && (matched == m_matched.end() || matched->second != unmarked)) {
        // A file opened without KE1's marks: the selected-Lines run over the
        // whole Document finds which generated Lines are the source's. It
        // runs without the pending edit (the templates as they made the
        // file's Lines; KE1 refuses others), and counts once it answers: a
        // newer edit cancels it unanswered.
        application::Selection selection;
        selection.active = source;
        selection.selected = {source};
        const auto whole = application::macroSnapshotOf(*document(), selection);
        m_templater->preview(whole, [self, generation, whole, source,
                                     unmarked](application::KaraokeTemplaterPort::Preview p) {
            // Another tab or a newer chain since: its Line ids are not ours.
            if (!self || generation != self->m_generation)
                return;
            self->m_matched[source] = unmarked;
            self->m_replaced.erase(source);
            if (p.result) {
                std::set<std::uint64_t> kept;
                for (const auto &d : p.result->dialogues)
                    kept.insert(d.id);
                std::set<core::LineId> gone;
                for (const auto &d : whole.dialogues)
                    if (!kept.contains(d.id))
                        gone.insert(core::LineId{d.id});
                self->m_replaced[source] = std::move(gone);
            }
            self->runChain(generation);
        });
        return;
    }
    const auto snapshot = *m_previewSnapshot;
    m_templater->preview(snapshot, [self, generation, snapshot](application::KaraokeTemplaterPort::Preview p) {
        if (self && generation == self->m_generation)
            self->previewed(generation, std::move(p), snapshot);
    });
}

void TemplateEditorController::previewed(std::uint64_t generation, application::KaraokeTemplaterPort::Preview p,
                                         const application::MacroSnapshot &snapshot)
{
    const core::LineId source = *m_source;
    m_issues.clear();
    m_runError.clear();
    bool worked = false;
    if (!p.result) {
        m_runError = QString::fromStdString(p.error).trimmed();
        if (m_runError.isEmpty())
            m_runError = QCoreApplication::translate("HikariSub.TemplateEditor",
                                                     "The templater stopped before it finished.");
    } else {
        const QJsonObject report = reportOf(p.result->report);
        for (const QJsonValue v : report.value(QLatin1String("templates")).toArray()) {
            const QJsonObject t = v.toObject();
            if (const auto id = application::templatePreviewLineOf(snapshot, t.value(QLatin1String("line")).toInt()))
                m_stats[*id] = {t.value(QLatin1String("generated")).toInt(),
                                t.value(QLatin1String("seconds")).toDouble()};
        }
        for (const QJsonValue v : report.value(QLatin1String("issues")).toArray()) {
            const QJsonObject i = v.toObject();
            if (const auto id = application::templatePreviewLineOf(snapshot, i.value(QLatin1String("line")).toInt()))
                m_issues.push_back({*id, i.value(QLatin1String("message")).toString(),
                                    i.value(QLatin1String("start")).toInt(), i.value(QLatin1String("length")).toInt()});
        }
        worked = report.value(QLatin1String("problems")).toInt() == 0;
        if (worked) {
            m_preview = application::templatePreviewLines(*p.result, source);
            m_own.assign(m_preview->size(), false);
            m_previewSeconds = report.value(QLatin1String("seconds")).toDouble();
        }
    }
    m_previewKept = !worked && m_preview.has_value();
    m_previewing = false; // the edit's problems are known
    emit changed();
    emit previewChanged();
    emit videoChanged();
    if (!worked || currentIsCode() || !m_current || m_preview->empty()) {
        finishChain(generation);
        return;
    }
    // The current template's own Lines: those a run without it lacks.
    application::MacroSnapshot without = snapshot;
    const auto at = std::ranges::find_if(without.dialogues, [&](const auto &d) { return d.id == m_current->value; });
    if (at == without.dialogues.end()) {
        finishChain(generation);
        return;
    }
    const int sourceAt = without.active;
    const int removed = static_cast<int>(without.info.size() + without.styles.size()) + 1 +
                        static_cast<int>(at - without.dialogues.begin());
    without.dialogues.erase(at);
    if (removed < sourceAt) {
        without.active = sourceAt - 1;
        without.selected = {without.active};
    }
    QPointer<TemplateEditorController> self = this;
    m_templater->preview(without, [self, generation](application::KaraokeTemplaterPort::Preview other) {
        if (self && generation == self->m_generation)
            self->markOwn(generation, std::move(other));
    });
}

void TemplateEditorController::markOwn(std::uint64_t generation, application::KaraokeTemplaterPort::Preview without)
{
    if (without.result && m_preview && m_source) {
        std::multiset<std::u8string> others;
        for (const auto &l : application::templatePreviewLines(*without.result, *m_source))
            others.insert(core::legacy::assLineText(l));
        m_own.assign(m_preview->size(), false);
        for (std::size_t k = 0; k < m_preview->size(); ++k) {
            const auto it = others.find(core::legacy::assLineText((*m_preview)[k]));
            if (it == others.end())
                m_own[k] = true;
            else
                others.erase(it);
        }
        emit previewChanged();
    }
    finishChain(generation);
}

void TemplateEditorController::finishChain(std::uint64_t generation)
{
    m_running = false;
    m_previewing = false;
    emit changed(); // canApply
    if (m_wantValues)
        runValues(generation);
}

void TemplateEditorController::requestValues()
{
    m_wantValues = true;
    if (!m_running && !m_previewTimer.isActive())
        runValues(m_generation);
}

void TemplateEditorController::runValues(std::uint64_t generation)
{
    // m_wantValues stays until the answer comes: a preview that replaces
    // this run asks again when its chain ends.
    const auto *doc = document();
    if (!doc || !m_source || !available() || !trusted())
        return;
    auto snapshot = application::templatePreviewSnapshot(*doc, *m_source);
    const auto *source = line(*m_source);
    if (!snapshot || !source)
        return;
    // One syllable template that writes every variable, on the source alone.
    application::MacroDialogueLine probe;
    probe.comment = true;
    probe.effect = "template syl noblank notext";
    probe.style = std::string(source->style.begin(), source->style.end());
    for (const auto &v : variables()) {
        if (!probe.text.empty())
            probe.text += kValueSeparator;
        probe.text += std::string("$") + v.name;
    }
    const auto sourceLine = application::macroLineOf(*source);
    snapshot->dialogues = {probe, sourceLine};
    snapshot->active = static_cast<int>(snapshot->info.size() + snapshot->styles.size()) + 1 + 1;
    snapshot->selected = {snapshot->active};
    QPointer<TemplateEditorController> self = this;
    const core::LineId id = *m_source;
    m_running = true;
    m_templater->preview(*snapshot, [self, generation, id](application::KaraokeTemplaterPort::Preview p) {
        // Another tab or a newer run since: the same Line id may be
        // another Document's.
        if (!self || generation != self->m_generation)
            return;
        self->m_running = false;
        self->m_wantValues = false;
        if (!p.result || self->m_source != id)
            return;
        const auto lines = application::templatePreviewLines(*p.result, id);
        if (lines.empty())
            return;
        const QStringList values = qs(lines.front().text).split(QLatin1Char(kValueSeparator));
        self->m_values.clear();
        for (std::size_t k = 0; k < variables().size() && k < static_cast<std::size_t>(values.size()); ++k)
            self->m_values[QString::fromLatin1(variables()[k].name)] = values[static_cast<qsizetype>(k)];
        emit self->valuesChanged();
    });
}

QVariantList TemplateEditorController::completions(const QString &text, int cursor) const
{
    QVariantList out;
    cursor = std::clamp(cursor, 0, static_cast<int>(text.size()));
    int from = cursor;
    while (from > 0 && (text[from - 1].isLetterOrNumber() || text[from - 1] == QLatin1Char('_')))
        --from;
    const QString prefix = text.mid(from, cursor - from);
    if (from > 0 && text[from - 1] == QLatin1Char('$')) {
        for (const auto &v : variables()) {
            const QString name = QString::fromLatin1(v.name);
            if (!name.startsWith(prefix, Qt::CaseInsensitive))
                continue;
            out.append(QVariantMap{{QStringLiteral("name"), QLatin1Char('$') + name},
                                   {QStringLiteral("insert"), QLatin1Char('$') + name},
                                   {QStringLiteral("from"), from - 1},
                                   {QStringLiteral("help"), translated(v.help)},
                                   {QStringLiteral("value"), m_values.value(name).toString()}});
        }
        return out;
    }
    // Inside an expression: an odd number of `!` before the word.
    if (text.left(from).count(QLatin1Char('!')) % 2 == 1) {
        for (const auto &f : functions()) {
            const QString name = QString::fromLatin1(f.name);
            if (!name.startsWith(prefix))
                continue;
            out.append(QVariantMap{{QStringLiteral("name"), name},
                                   {QStringLiteral("insert"), QString::fromLatin1(f.insert)},
                                   {QStringLiteral("from"), from},
                                   {QStringLiteral("help"), translated(f.help)},
                                   {QStringLiteral("value"), QString()}});
        }
    }
    return out;
}

QVariantList TemplateEditorController::colourRuns(const QString &text) const
{
    QVariantList out;
    const auto runs = core::syntax::colourRuns(
        std::u16string_view(reinterpret_cast<const char16_t *>(text.utf16()), static_cast<std::size_t>(text.size())),
        currentIsCode() ? core::syntax::TextState::Code : core::syntax::TextState::Template);
    for (const auto &run : runs)
        out << static_cast<int>(run.start) << static_cast<int>(run.end) << static_cast<int>(run.role);
    return out;
}

QVariantMap TemplateEditorController::effectParts(const QString &effect) const
{
    const EffectWords w = splitEffect(effect);
    return {{QStringLiteral("kind"), w.kind.toLower()},
            {QStringLiteral("className"), w.classWords.isEmpty() ? QString() : w.classWords.front().toLower()},
            {QStringLiteral("name"), w.classWords.size() > 1 ? w.classWords[1] : QString()},
            {QStringLiteral("modifiers"), w.modifiers}};
}

QString TemplateEditorController::effectWithClass(const QString &effect, const QString &className) const
{
    EffectWords w = splitEffect(effect);
    if (w.kind.isEmpty())
        w.kind = QStringLiteral("template");
    const QString name = w.classWords.size() > 1 ? w.classWords[1] : QString();
    w.classWords.clear();
    if (!className.isEmpty())
        w.classWords << className;
    if (!name.isEmpty() && (className == QLatin1String("line") || className == QLatin1String("pre-line")))
        w.classWords << name;
    return joinEffect(w);
}

QString TemplateEditorController::effectWithModifier(const QString &effect, const QString &word, bool on) const
{
    EffectWords w = splitEffect(effect);
    const QStringList given = word.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (given.isEmpty())
        return effect;
    // Remove the modifier (with its argument) wherever it is.
    QStringList kept;
    for (qsizetype k = 0; k < w.modifiers.size(); ++k) {
        if (w.modifiers[k].compare(given.front(), Qt::CaseInsensitive) == 0) {
            if (takesArgument(given.front()) && k + 1 < w.modifiers.size())
                ++k;
            continue;
        }
        kept << w.modifiers[k];
    }
    w.modifiers = kept;
    if (on)
        w.modifiers << given;
    return joinEffect(w);
}

} // namespace hikari::ui
