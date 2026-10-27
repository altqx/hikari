#include "template_editor_controller.h"

#include "hikari/application/karaoke_presets.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/editor_syntax.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>

#include <algorithm>
#include <span>

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

// KE3: the built-in karaoke effect presets (docs/qt/karaoke-presets.md), in
// list order, with their files' Title and Description for the translators.
struct PresetEntry {
    const char *file, *title, *description;
};
constexpr PresetEntry kPresets[] = {
    {"01-classic-fill.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Classic fill"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor",
                       "The karaoke fill from left to right, fading in with the Line and out after it.")},
    {"02-staggered-fade.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Staggered fade"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor",
                       "Syllables fade in one after another, light up as they are sung and fade out together.")},
    {"03-colour-sweep.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Colour sweep"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor",
                       "A second colour wipes across each syllable from left to right while it is sung.")},
    {"04-scale-pop.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Scale pop"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor",
                       "Each syllable swells as it is sung and settles back to its Style's size in the sung colour.")},
    {"05-bounce.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Bounce"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Each syllable hops up and lands again while it is sung.")},
    {"06-glow.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Glow"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "A soft halo flares behind each syllable as it is sung, then fades.")},
    {"07-sparkles.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Sparkles"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Small sparks scatter from each syllable as it is sung.")},
    {"08-echo.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Echo"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "A copy of each syllable grows and fades away as it is sung.")},
    {"09-falling-letters.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Falling letters"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor",
                       "Letters drop into place one by one, then light up syllable by syllable.")},
    {"10-palette.ass", QT_TRANSLATE_NOOP("HikariSub.TemplateEditor", "Colour per Line"),
     QT_TRANSLATE_NOOP("HikariSub.TemplateEditor",
                       "Lines take turns through a short palette of fill colours, by their place in the file.")},
};

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

// One modifier by either of its words: the templater reads `repeat` as
// `loop`.
bool sameModifier(const QString &a, const QString &b)
{
    const auto word = [](const QString &w) {
        return w.compare(QLatin1String("repeat"), Qt::CaseInsensitive) == 0 ? QStringLiteral("loop") : w.toLower();
    };
    return word(a) == word(b);
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
    QStringList otherClasses; // combined with it
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
    // The first class word wherever it is, as the stock parser reads the
    // words in turn; a modifier's argument is none. The others it combines
    // with it (`syl furi`, `line syl`) go with it: the picker's class is one
    // choice.
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString word = words[i].toLower();
        if (!out.classWords.isEmpty() && classes.contains(word)) {
            out.otherClasses << words[i];
        } else if (out.classWords.isEmpty() && classes.contains(word)) {
            out.classWords << words[i];
            const bool lineClass = word == QLatin1String("line") || word == QLatin1String("pre-line");
            if (kind == QLatin1String("template") && lineClass && i + 1 < words.size() &&
                !modifierWords().contains(words[i + 1].toLower()) && !templateClasses().contains(words[i + 1].toLower()))
                out.classWords << words[++i];
        } else {
            out.modifiers << words[i];
            if (takesArgument(word) && i + 1 < words.size())
                out.modifiers << words[++i];
        }
    }
    return out;
}

QString joinEffect(const EffectWords &w)
{
    QStringList all;
    all << w.kind << w.classWords << w.otherClasses << w.modifiers;
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

void TemplateEditorController::setHostState(HostState state)
{
    m_hostState = std::move(state);
}

void TemplateEditorController::hostChanged()
{
    std::string now = m_hostState ? m_hostState() : std::string();
    if (now == m_hostSeen)
        return;
    m_hostSeen = std::move(now);
    schedulePreview();
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

namespace {
// The Lines as a run reads them: Apply commits the Line editor's draft
// before it runs, so the draft stands in for its Line.
template <class F> void forEachRunLine(const application::EditSession &s, F &&f, bool withDraft = true)
{
    const auto draft = withDraft ? s.draftRecord() : std::nullopt;
    for (const auto *l : s.document().lines())
        f(draft && draft->id == l->id ? *draft : *l);
}
} // namespace

core::Document TemplateEditorController::runDocument() const
{
    const auto *s = session();
    core::Document doc = s->document();
    if (const auto draft = s->draftRecord())
        doc.editLine(draft->id, [&](core::LineRecord &l) { l = *draft; }, core::ChangeMark::Kept);
    return doc;
}

core::Document TemplateEditorController::pendingDocument() const
{
    core::Document doc = runDocument();
    if (m_pending && m_pending->document == documentKey() &&
        (m_pending->effect || m_pending->style || m_pending->layer)) {
        Pending fields = *m_pending;
        fields.text.reset();
        doc.editLine(fields.line, [&](core::LineRecord &l) { l = edited(l, fields); }, core::ChangeMark::Kept);
    }
    return doc;
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
    // The Line editor's draft of it, as the preview runs it (runDocument).
    core::LineRecord out = *l;
    if (const auto *s = session())
        if (auto draft = s->draftRecord(); draft && draft->id == l->id)
            out = *draft;
    if (m_pending && m_pending->line == l->id && m_pending->document == documentKey())
        return edited(out, *m_pending);
    return out;
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
    auto &baseline = m_baselines[documentKey()];
    baseline.clear();
    if (const auto *doc = document())
        for (const auto *l : doc->lines())
            if (application::templateKindOf(*l))
                baseline[l->id] = keyOf(*l);
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
        m_owners.clear(); // another Document's Line ids
        m_ownersRevision.reset();
        m_values.clear();
        m_lastApply.clear();
        m_previewSnapshotKey.clear();
        stopRuns();
    }
    if (!s) {
        emit changed();
        emit previewChanged();
        emit videoChanged();
        return;
    }
    if (!m_baselines.contains(documentKey()))
        takeBaseline(); // first seen, or reloaded
    if (const auto it = m_appliedElsewhere.find(documentKey()); it != m_appliedElsewhere.end()) {
        // Its Apply finished while another tab was shown.
        const std::string report = std::move(it->second);
        m_appliedElsewhere.erase(it);
        applied(report);
    }
    const auto &doc = s->document();
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
    const bool unloaded = !ready && m_wasAvailable;
    m_wasAvailable = ready;
    if (becameReady)
        m_previewSnapshotKey.clear(); // a reloaded script may write otherwise
    // Made before trust went, or by a script no longer loaded: not this
    // file's result, nor are its problems.
    if ((!trusted() || unloaded) && (m_preview || m_previewing || !m_issues.empty() || !m_runError.isEmpty()))
        clearPreview();
    // The Line editor's draft is what Apply runs on too.
    const auto draft = s->draftRecord();
    const std::string drafted =
        draft ? std::to_string(draft->id.value) + '\n' + application::macroLineOf(*draft).raw + '\x1f' +
                    std::string(draft->translation.begin(), draft->translation.end())
              : std::string();
    if (s->revision() != m_seenRevision || becameReady || drafted != m_seenDraft) {
        m_seenDraft = drafted;
        m_seenRevision = s->revision();
        if (s->revision() != m_applyRevision)
            m_lastApply.clear(); // an Undo or an edit since Apply
        schedulePreview();
    }
    hostChanged(); // e.g. the audio selection, the active Line's times
    emit changed();
}

void TemplateEditorController::updateSource()
{
    const auto *s = session();
    // The active Line as Apply runs on it: with the Line editor's draft and
    // an Effect still pending (one that makes a template a karaoke Line).
    std::optional<core::Document> drafted;
    if (s && (s->draftRecord() || (m_pending && m_pending->document == documentKey())))
        drafted = pendingDocument();
    const core::Document *doc = drafted ? &*drafted : s ? &s->document() : nullptr;
    std::optional<core::LineId> source =
        doc ? application::templatePreviewSource(*doc, s->selection().active, m_lastSource) : std::nullopt;
    // A generated Line of a file opened without marks: its karaoke Line is
    // found first; the preview stays until then. One no karaoke Line gives
    // has none, and no other Line stands in for it.
    const auto active = s ? s->selection().active : std::nullopt;
    const core::LineRecord *activeLine = nullptr; // as the draft has it
    if (active && doc)
        for (const auto *l : doc->lines())
            if (l->id == *active)
                activeLine = l;
    bool unknown = false;
    if (const auto *l = activeLine;
        l && application::isGeneratedLine(*l) && l->generatedFrom.value == 0) {
        if (m_ownersRevision == s->revision()) {
            const auto owner = m_owners.find(*active);
            unknown = owner == m_owners.end();
            source = unknown ? std::nullopt : std::optional(owner->second);
        } else if (m_ownersAsked == s->revision() || askOwners()) {
            return;
        }
    }
    if (unknown != m_sourceUnknown) {
        m_sourceUnknown = unknown;
        emit previewChanged();
    }
    if (source == m_source)
        return;
    m_source = source;
    if (source)
        m_lastSource = source;
    stopRuns(); // their answers are another Line's
    m_preview.reset();
    m_own.clear();
    m_issues.clear(); // that Line's run's; the next one's replace them
    m_runError.clear();
    m_previewKept = false;
    m_previewSeconds = 0;
    m_values.clear();
    m_previewSnapshotKey.clear();
    emit previewChanged();
    emit valuesChanged();
    emit videoChanged();
    schedulePreview();
}

void TemplateEditorController::clearPreview()
{
    stopRuns();
    m_preview.reset();
    m_own.clear();
    m_issues.clear();
    m_runError.clear();
    m_previewKept = false;
    m_previewSnapshotKey.clear();
    emit previewChanged();
    emit videoChanged();
}

void TemplateEditorController::stopRuns()
{
    ++m_generation;
    m_running = false;
    m_previewing = false;
    m_ownersAsked.reset();
    if (m_templater)
        m_templater->cancelPreview();
}

bool TemplateEditorController::askOwners()
{
    // It runs the templates as the file holds them: the Line editor's draft
    // aside, that state has to be one the user chose too.
    auto *s = session();
    if (!s || !m_shown || !available() || !trusted() || !trustedIn(*s, documentKey(), false))
        return false;
    const auto snapshot = application::templateOwnersSnapshot(s->document());
    if (!snapshot) {
        m_owners.clear();
        m_ownersRevision = s->revision();
        return false;
    }
    stopRuns(); // one run at a time: this one first
    const std::uint64_t revision = s->revision();
    m_ownersAsked = revision;
    emit changed(); // canApply waits for the preview it leads to
    QPointer<TemplateEditorController> self = this;
    const std::uint64_t generation = m_generation;
    const DocumentKey key = documentKey();
    m_templater->preview(*snapshot, [self, generation, key, revision](application::KaraokeTemplaterPort::Preview p) {
        if (!self || generation != self->m_generation)
            return;
        self->m_ownersAsked.reset();
        const auto *now = self->session();
        if (now && self->documentKey() == key && now->revision() == revision) {
            self->m_owners = p.result ? application::templateOwners(now->document(), *p.result)
                                      : std::map<core::LineId, core::LineId>{};
            self->m_ownersRevision = revision;
        }
        self->m_previewSnapshotKey.clear();
        self->refresh();
        self->schedulePreview();
    });
    return true;
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
        static const std::map<core::LineId, std::string> none;
        const auto ours = m_baselines.find(documentKey());
        const auto &baseline = ours == m_baselines.end() ? none : ours->second;
        const auto base = baseline.find(l->id);
        const bool problem = std::ranges::any_of(m_issues, [&](const Issue &i) { return i.line == l->id; });
        out.append(QVariantMap{
            {QStringLiteral("id"), QVariant::fromValue<qulonglong>(l->id.value)},
            {QStringLiteral("number"), number},
            {QStringLiteral("kind"), *kind == application::TemplateKind::Code ? QStringLiteral("code")
                                                                               : QStringLiteral("template")},
            {QStringLiteral("effect"), qs(r.effect)},
            {QStringLiteral("style"), qs(r.style)},
            {QStringLiteral("text"), qs(r.translation.empty() ? r.text : r.translation)},
            {QStringLiteral("generated"), stats == m_stats.end() ? -1 : stats->second.generated},
            {QStringLiteral("seconds"), stats == m_stats.end() ? -1.0 : stats->second.seconds},
            {QStringLiteral("changed"), base == baseline.end() || base->second != keyOf(r)},
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
    if (m_pending)
        return; // an edit that can't go in yet (a macro holds the Document) stays with its template
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
    return r ? qs(r->translation.empty() ? r->text : r->translation) : QString();
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
    const auto l = sourceRecord();
    return l ? qs(l->style) : QString();
}

QString TemplateEditorController::sourceText() const
{
    const auto l = sourceRecord();
    // In TLMode the run reads the translation (macroLineOf).
    return l ? withoutTags(l->translation.empty() ? l->text : l->translation) : QString();
}

std::optional<core::LineRecord> TemplateEditorController::sourceRecord() const
{
    // The preview ran on the Line editor's draft (runDocument) and the
    // panel's pending edit (a `template line karaoke` source's own).
    std::optional<core::LineRecord> out;
    if (const auto *s = m_source ? session() : nullptr)
        if (const auto draft = s->draftRecord(); draft && draft->id == *m_source)
            out = draft;
    if (const auto *l = !out && m_source ? line(*m_source) : nullptr)
        out = *l;
    if (out && m_pending && m_pending->line == out->id && m_pending->document == documentKey())
        out = edited(*out, *m_pending);
    return out;
}

int TemplateEditorController::sourceStartMs() const
{
    const auto r = sourceRecord();
    return r ? static_cast<int>(msOf(r->start)) : 0;
}

int TemplateEditorController::sourceEndMs() const
{
    const auto r = sourceRecord();
    return r ? static_cast<int>(msOf(r->end)) : 0;
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
            // In TLMode the run wrote the translation (lineOfMacro).
            {QStringLiteral("text"), qs(l.translation.empty() ? l.text : l.translation)},
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
    std::vector<std::int64_t> ends;
    for (const auto &l : *m_preview) {
        from = std::min(from, msOf(l.start));
        to = std::max(to, msOf(l.end));
        ends.push_back(msOf(l.end));
    }
    if (playing && (timeMs < from || timeMs >= to)) {
        for (const auto *l : document()->lines())
            ends.push_back(msOf(l->end));
        std::ranges::sort(ends);
    }
    return application::templatePreviewWindow(timeMs, playing, from, to, ends);
}

std::optional<core::Document> TemplateEditorController::videoDocument(const core::Document &doc, std::int64_t timeMs,
                                                                      bool playing) const
{
    if (!videoPreview())
        return std::nullopt;
    const auto replaced = m_replaced.find(*m_source);
    const auto *s = session();
    const bool ownersKnown = s && m_ownersRevision == s->revision();
    return application::templatePreviewDocument(
        doc, *m_source, *m_preview, replaced == m_replaced.end() ? std::set<core::LineId>{} : replaced->second,
        videoWindow(timeMs, playing), ownersKnown ? m_owners : std::map<core::LineId, core::LineId>{},
        m_previewStyles);
}

bool TemplateEditorController::canApply() const
{
    const auto *s = session();
    return available() && m_editable && s && !s->isReadOnly() && templateCount() > 0 && !hasProblems() &&
           !previewPending();
}

bool TemplateEditorController::previewPending() const
{
    return m_shown && trusted() && (m_previewTimer.isActive() || m_previewing || m_ownersAsked);
}

TemplateEditorController::DocumentKey TemplateEditorController::documentKey() const
{
    return m_documentKey ? m_documentKey()
                         : DocumentKey{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(session())), 0};
}

std::string TemplateEditorController::formOf(const core::LineRecord &line)
{
    // The Style too: it picks the karaoke Lines the template runs on, and so
    // whose Actor goes into its `$actor`.
    const auto m = application::macroLineOf(line);
    return std::to_string(m.style.size()) + ':' + m.style + m.effect + '\n' + m.text;
}

std::string TemplateEditorController::fieldOf(const core::LineRecord &line, bool whole)
{
    // The pairing: the Style picks the templates the Actor goes into
    // ($actor), so a trusted Actor under another Style, or a Style's name as
    // an Actor, is new.
    const auto m = application::macroLineOf(line);
    std::string field = std::to_string(m.style.size()) + ':' + m.style + m.actor;
    // A `!` in either can make an expression of what the templates write;
    // whether it runs hangs on the rest of the Line (a blank one `noblank`
    // passes over, its Effect, its times), so the whole Line is the choice.
    if (field.find('!') != std::string::npos)
        field += '\n' + m.raw + '\x1f' + m.text + '\x1f' + m.translation;
    // Lua reading the Lines (`orgline.text`) takes their strings; times are
    // only numbers to it.
    else if (whole)
        field += '\n' + m.effect + '\x1f' + m.text + '\x1f' + m.translation;
    return field;
}

std::string TemplateEditorController::generatedKey(const core::Document &doc)
{
    std::string out;
    for (const auto *l : doc.lines())
        if (application::isGeneratedLine(*l) && l->generatedFrom.value == 0) {
            const auto text = core::legacy::assLineText(*l);
            out += std::to_string(l->id.value) + ':' + std::string(text.begin(), text.end()) + '\x1f' +
                   std::string(l->translation.begin(), l->translation.end()) + '\n';
        }
    return out;
}

std::string TemplateEditorController::headOf(const core::Document &doc)
{
    std::string out;
    const auto add = [&](std::u8string_view v) {
        out += std::to_string(v.size()) + ':';
        out.append(v.begin(), v.end());
    };
    for (const auto &section : doc.sections())
        for (const auto &record : section.records) {
            if (section.kind == core::SectionKind::ScriptInfo) {
                if (const auto *p = std::get_if<core::PropertyRecord>(&record)) {
                    out += 'I';
                    add(p->key);
                    add(p->value);
                }
            } else if (section.kind == core::SectionKind::Styles) {
                if (const auto *st = std::get_if<core::StyleRecord>(&record)) {
                    out += 'S';
                    add(st->name);
                    for (const auto &f : st->fields)
                        add(f);
                }
            }
        }
    return out;
}

TemplateEditorController::TrustState TemplateEditorController::stateOf(const application::EditSession &s,
                                                                       bool withDraft)
{
    TrustState out;
    // Lua in the templates (a code Line, or a template's `!` expression) can
    // read every karaoke Line (`orgline.text`, `$actor` in `!$actor!`): then
    // each one is trusted with its text, translation and Effect too. A `!`
    // in a karaoke Line's Style or Actor is Lua as well once a template puts
    // it in (`$actor`).
    bool whole = false;
    forEachRunLine(s, [&](const core::LineRecord &l) {
        if (const auto kind = application::templateKindOf(l)) {
            const std::string form = formOf(l);
            out.templates += std::to_string(l.id.value) + ':' + std::to_string(form.size()) + ':' + form;
            whole = whole || *kind == application::TemplateKind::Code ||
                    application::macroLineOf(l).text.find('!') != std::string::npos;
        }
        if (!whole && application::isKaraokeLine(l)) {
            const auto m = application::macroLineOf(l);
            whole = m.style.find('!') != std::string::npos || m.actor.find('!') != std::string::npos;
        }
    }, withDraft);
    forEachRunLine(s, [&](const core::LineRecord &l) {
        if (application::isKaraokeLine(l)) // a `template line karaoke` too
            out.fields.insert(fieldOf(l, whole));
    }, withDraft);
    // Lua reads the Styles (`orgline.styleref`) and Script Info (`meta`) as
    // well.
    if (whole)
        out.head = headOf(s.document());
    return out;
}

bool TemplateEditorController::trustedIn(const application::EditSession &s, const DocumentKey &document,
                                         bool withDraft) const
{
    // A template or code Line come or changed since another way (a paste,
    // an import, a translation pasted over it, a sort) is Lua the user has
    // not chosen to run; so is a new Actor or Style the templates put into
    // theirs. Lines deleted or copied with a pairing there leave a state
    // chosen.
    const auto it = m_trusted.find(document);
    if (it == m_trusted.end())
        return false;
    const TrustState now = stateOf(s, withDraft);
    return std::ranges::any_of(it->second, [&](const TrustState &chosen) {
        return chosen.templates == now.templates && chosen.head == now.head &&
               std::ranges::includes(chosen.fields, now.fields);
    });
}

void TemplateEditorController::trust()
{
    if (const auto *s = session())
        trust(*s, documentKey());
}

void TemplateEditorController::trust(const application::EditSession &s, const DocumentKey &document)
{
    auto &states = m_trusted[document];
    TrustState now = stateOf(s);
    if (std::ranges::none_of(states, [&](const TrustState &chosen) {
            return chosen.templates == now.templates && chosen.head == now.head &&
               std::ranges::includes(chosen.fields, now.fields);
        }))
        states.push_back(std::move(now));
}

bool TemplateEditorController::trusted() const
{
    const auto *s = session();
    return s && trustedIn(*s, documentKey());
}

void TemplateEditorController::trustDocument()
{
    if (!session() || trusted())
        return;
    trust();
    m_previewSnapshotKey.clear();
    emit changed();
    updateSource(); // an unmarked generated Line's karaoke Line can be found now
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
    if (m_pending && (m_pending->line != *m_current || m_pending->document != documentKey())) {
        commit();
        if (m_pending)
            return; // another Line's edit that can't go in yet is not dropped
    }
    if (!m_pending)
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
    if (change.effect)
        updateSource(); // it may make the template a karaoke Line, or not
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
    const Pending change = *m_pending;
    const auto lineOf = [&]() -> const core::LineRecord * {
        for (const auto *l : s->document().lines())
            if (l->id == change.line)
                return l;
        return nullptr;
    };
    const auto drop = [&] {
        m_pending.reset();
        m_commitTimer.stop();
        emit changed();
    };
    const core::LineRecord *was = s ? lineOf() : nullptr;
    if (!was)
        return drop(); // its Document closed
    // A Line editor draft of this Line goes in first, as its own step (the
    // run would, and then find the revision moved); the panel's edit goes
    // over it, one that ends at the committed text too.
    if (s->draftLine() == change.line) {
        // false also for a draft that changed nothing, gone now.
        if (!s->commitDraft() && s->draftLine() == change.line) {
            m_commitTimer.start(); // a draft that can't go in yet
            return;
        }
        was = lineOf();
    }
    if (!was || keyOf(edited(*was, change)) == keyOf(*was))
        return drop(); // typing that ended where it began: no step
    // The user's edit of a state they chose to run is theirs to run too; a
    // draft that went in with it is not, so the state is read after it.
    const bool chosen = was && application::templateKindOf(*was) && trustedIn(*s, change.document);
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
    if (chosen) // with what it changed: an Effect, a `template line karaoke`'s pairing
        trust(*s, change.document);
    if (s == session()) {
        m_seenRevision = s->revision();
        emit documentEdited();
    } else {
        // Another tab's, while the views change to this one: they show it
        // after, and it waits for its autosave.
        emit otherDocumentEdited(change.document);
        QMetaObject::invokeMethod(this, &TemplateEditorController::documentEdited, Qt::QueuedConnection);
    }
    emit changed();
}

bool TemplateEditorController::canApplySelected() const
{
    const auto *s = session();
    if (!canApply() || !s)
        return false;
    // macro_apply_templates_to_selection: a karaoke Line, or a generated
    // Line whose mark names one (a file's unmarked Lines name none).
    const core::Document doc = runDocument();
    std::set<core::LineId> karaoke;
    for (const auto *l : doc.lines())
        if (application::isKaraokeLine(*l))
            karaoke.insert(l->id);
    return std::ranges::any_of(doc.lines(), [&](const core::LineRecord *l) {
        return s->selection().selected.contains(l->id) &&
               (karaoke.contains(l->id) || (application::isGeneratedLine(*l) && karaoke.contains(l->generatedFrom)));
    });
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
    if (!(selectedOnly ? canApplySelected() : canApply()) || m_pending)
        return false;
    return m_templater->apply(selectedOnly);
}

void TemplateEditorController::applied(const std::string &report, std::optional<DocumentKey> ranOn)
{
    if (ranOn && *ranOn != documentKey()) {
        m_appliedElsewhere[*ranOn] = report; // taken when it is current again
        return;
    }
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
    // A selected-Lines run leaves the other Lines' output from the templates
    // as they were: what changed since the last whole Apply stays marked.
    if (o.value(QLatin1String("mode")).toString() != QLatin1String("selected"))
        takeBaseline();
    trust(); // the user ran them
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
    application::MacroDialogueLine d;
    d.effect = effect.toStdString();
    return insertLines("New template", {d});
}

QVariantList TemplateEditorController::presets()
{
    QVariantList out;
    for (const auto &p : kPresets)
        out << QVariantMap{{QStringLiteral("title"), translated(p.title)},
                           {QStringLiteral("description"), translated(p.description)},
                           {QStringLiteral("file"), QLatin1String(p.file)}};
    return out;
}

bool TemplateEditorController::insertPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(std::size(kPresets)))
        return false;
    QFile file(QStringLiteral(":/hikari/ui/karaoke-presets/") + QLatin1String(kPresets[index].file));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    const auto preset = application::readKaraokePreset(std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
    if (!preset)
        return false;
    std::vector<application::MacroDialogueLine> lines;
    for (const auto &l : preset->lines) {
        application::MacroDialogueLine d;
        d.layer = static_cast<int>(l.layer);
        d.effect = std::string(l.effect.begin(), l.effect.end());
        d.text = std::string(l.text.begin(), l.text.end());
        lines.push_back(std::move(d));
    }
    return insertLines("New template from preset", std::move(lines));
}

bool TemplateEditorController::insertLines(const char *commandName, std::vector<application::MacroDialogueLine> made)
{
    commit();
    auto *s = session();
    if (!s || !m_editable || made.empty())
        return false;
    // A `template line karaoke` too: the templater runs it as one.
    const auto firstOf = [](const auto &lines) {
        return std::ranges::find_if(lines, [](const core::LineRecord *l) { return application::isKaraokeLine(*l); });
    };
    // A Line editor draft of the Line they go above goes in first, as the
    // run would (and then find the revision moved); the Line is read after.
    if (const auto lines = s->document().lines(); firstOf(lines) != lines.end() && s->draftLine() == (*firstOf(lines))->id)
        if (!s->commitDraft() && s->draftLine())
            return false; // a draft that can't go in yet
    const auto all = s->document().lines();
    const auto first = firstOf(all);
    std::string style;
    if (first != all.end())
        style = std::string((*first)->style.begin(), (*first)->style.end());
    else if (const auto names = styles(); !names.isEmpty())
        style = names.front().toStdString();
    std::vector<core::LineRecord> fresh;
    for (auto &d : made) {
        d.comment = true;
        d.style = style;
        fresh.push_back(application::lineOfMacro(d));
    }
    std::optional<core::LineId> added;
    application::Command command;
    command.name = commandName;
    command.expectedRevision = s->revision();
    if (first != all.end())
        command.touches = {(*first)->id};
    const std::optional<core::LineId> before = first != all.end() ? std::optional((*first)->id) : std::nullopt;
    command.apply = [&](core::Document &doc) {
        for (const auto &l : fresh) {
            const auto id = before ? doc.insertLineBefore(*before, l) : doc.appendLine(l);
            if (!id)
                return false;
            added = added.value_or(*id);
        }
        return true;
    };
    const bool chosen = trusted();
    if (!s->run(command) || !added)
        return false;
    if (chosen)
        trust(); // with the user's own templates
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
    if (m_ownersAsked)
        return; // its answer previews
    if (!m_shown || !available() || !m_source || !trusted()) {
        m_running = false;
        m_previewing = false;
        emit changed(); // canApply
        return;
    }
    // The Line editor's draft, and the pending Effect, Style and Layer on it.
    core::Document seen = pendingDocument();
    // A template made by an Effect still pending counts.
    if (std::ranges::none_of(seen.lines(), [](const core::LineRecord *l) {
            return application::templateKindOf(*l) == application::TemplateKind::Template;
        })) {
        // The last template went: its preview too, a run still going (its
        // answer is no template's now) and its problems.
        if (m_preview || m_running || m_previewing || !m_issues.empty() || !m_runError.isEmpty())
            clearPreview();
        m_running = false;
        m_previewing = false;
        emit changed(); // canApply
        return;
    }
    auto snapshot = application::templatePreviewSnapshot(seen, *m_source, pendingTexts());
    if (!snapshot)
        return;
    // What the run reads: Script Info (meta, karaskel's resolution), the
    // styles, the Lines and the host; and the current template, whose own
    // Lines the chain marks.
    std::string key = std::to_string(m_current ? m_current->value : 0) + '\n';
    m_hostSeen = m_hostState ? m_hostState() : std::string();
    key += m_hostSeen + '\n';
    for (const auto &i : snapshot->info)
        key += i.key + ':' + i.value + '\n';
    for (const auto &d : snapshot->dialogues)
        key += d.raw + '\n' + d.text + '\n';
    for (const auto &st : snapshot->styles)
        for (const auto &f : st.fields)
            key += f + ',';
    // And the file's unmarked generated Lines, which the matching run reads.
    key += '\n' + generatedKey(*document());
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
    std::string generated = unmarked.empty() ? std::string() : generatedKey(*document());
    // It runs the templates as the file holds them, not the Line editor's
    // draft: only once the user chose that state too (a draft not yet in
    // previews without matching).
    const auto *s = session();
    const bool fileChosen = s && trustedIn(*s, documentKey(), false);
    if (!marked && !unmarked.empty() && fileChosen &&
        (matched == m_matched.end() || matched->second != generated)) {
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
                                     generated](application::KaraokeTemplaterPort::Preview p) {
            // Another tab or a newer chain since: its Line ids are not ours.
            if (!self || generation != self->m_generation)
                return;
            if (!p.result) {
                // The templates no longer give the file's Lines: Apply would
                // refuse as this run did. Nothing is matched, so the next
                // run asks again.
                self->m_previewSnapshotKey.clear();
                self->previewed(generation, std::move(p), *self->m_previewSnapshot);
                return;
            }
            self->m_matched[source] = generated;
            std::set<std::uint64_t> kept;
            for (const auto &d : p.result->dialogues)
                kept.insert(d.id);
            std::set<core::LineId> gone;
            for (const auto &d : whole.dialogues)
                if (!kept.contains(d.id))
                    gone.insert(core::LineId{d.id});
            self->m_replaced[source] = std::move(gone);
            self->runChain(generation);
        });
        return;
    }
    // The source's first Lines in such a file stand before those of the
    // karaoke Lines after it (templatePreviewDocument): which those are is
    // found first. The preview runs again once they are.
    const auto replaced = m_replaced.find(source);
    if (!marked && !unmarked.empty() && (replaced == m_replaced.end() || replaced->second.empty()) && s &&
        m_ownersRevision != s->revision() && askOwners())
        return;
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
        // Every template this run reports; one it doesn't (now a part of a
        // named template) has no counts of its own.
        m_stats.clear();
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
            m_previewStyles = application::templatePreviewStyles(snapshot, *p.result);
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
    // The source keeps its index; a `template line karaoke` source stays
    // the karaoke Line, only its template goes.
    if (m_source && *m_source == *m_current)
        at->effect = "karaoke";
    else
        *at = application::templatePlaceholder();
    QPointer<TemplateEditorController> self = this;
    m_templater->preview(without, [self, generation](application::KaraokeTemplaterPort::Preview other) {
        if (self && generation == self->m_generation)
            self->markOwn(generation, std::move(other));
    });
}

void TemplateEditorController::markOwn(std::uint64_t generation, application::KaraokeTemplaterPort::Preview without)
{
    if (without.result && m_preview && m_source) {
        // A TLMode Line's are told apart by their translations.
        const auto keyOf = [](const core::LineRecord &l) {
            return core::legacy::assLineText(l) + u8'\x1f' + l.translation;
        };
        std::multiset<std::u8string> others;
        for (const auto &l : application::templatePreviewLines(*without.result, *m_source))
            others.insert(keyOf(l));
        m_own.assign(m_preview->size(), false);
        for (std::size_t k = 0; k < m_preview->size(); ++k) {
            const auto it = others.find(keyOf((*m_preview)[k]));
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
    if (!session() || !m_source || !available() || !trusted() || m_ownersAsked)
        return;
    // As the preview reads it: the draft, the pending fields and text (a
    // `template line karaoke` source's own).
    const core::Document doc = pendingDocument();
    auto snapshot = application::templatePreviewSnapshot(doc, *m_source, pendingTexts());
    const core::LineRecord *source = nullptr;
    for (const auto *l : doc.lines())
        if (l->id == *m_source)
            source = l;
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
    // The templates' places stay, so the source keeps its index ($li); a
    // `template line karaoke` source is only a karaoke Line here, so its
    // own template writes nothing before the probe's Line.
    for (auto &d : snapshot->dialogues)
        if (d.id != m_source->value)
            d = application::templatePlaceholder();
        else if (application::templateKindOf(*source))
            d.effect = "karaoke";
    snapshot->dialogues.push_back(probe);
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
        // A TLMode Line's are written into its translation.
        const auto &written = lines.front().translation.empty() ? lines.front().text : lines.front().translation;
        const QStringList values = qs(written).split(QLatin1Char(kValueSeparator));
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
    w.otherClasses.clear();
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
    // Remove the modifier (with its argument) wherever it is; another
    // modifier's argument (`fx loop`) is not one.
    QStringList kept;
    for (qsizetype k = 0; k < w.modifiers.size(); ++k) {
        const bool argument = takesArgument(w.modifiers[k]) && k + 1 < w.modifiers.size();
        if (sameModifier(w.modifiers[k], given.front())) {
            k += argument;
            continue;
        }
        kept << w.modifiers[k];
        if (argument)
            kept << w.modifiers[++k];
    }
    w.modifiers = kept;
    if (on)
        w.modifiers << given;
    return joinEffect(w);
}

bool TemplateEditorController::hasModifier(const QString &effect, const QString &word) const
{
    const QStringList given = word.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList modifiers = splitEffect(effect).modifiers;
    for (qsizetype k = 0; !given.isEmpty() && k < modifiers.size(); ++k) {
        if (sameModifier(modifiers[k], given.front()))
            return true;
        if (takesArgument(modifiers[k]))
            ++k; // its argument, not a modifier
    }
    return false;
}

} // namespace hikari::ui
