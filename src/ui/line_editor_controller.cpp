#include "line_editor_controller.h"

#include "hikari/application/translation_mode.h"
#include "hikari/application/video_fullscreen.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/checked.h"
#include "hikari/core/editor_font_colour.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"
#include "hikari/core/tag_commands.h"
#include "hikari/core/text_projection.h"
#include "line_measures.h"

#include <QCoreApplication>
#include <QCollator>
#include <QTextBoundaryFinder>

#include <utility>

#include <algorithm>
#include <span>
#include <array>
#include <tuple>
#include <cstdlib>

namespace hikari::ui {

namespace {

QString qs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

std::u8string u8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(b.constData()), static_cast<std::size_t>(b.size()));
}

std::u16string u16(const QString &s)
{
    return std::u16string(reinterpret_cast<const char16_t *>(s.utf16()), static_cast<std::size_t>(s.size()));
}

QString refusalText(core::MapRefusal r)
{
    switch (r) {
    case core::MapRefusal::SplitsCharacter: return QCoreApplication::translate("HikariSub.Editor", "Select whole characters.");
    case core::MapRefusal::AssSyntax:
        return QCoreApplication::translate("HikariSub.Editor", "Braces and backslashes are ASS syntax: edit them with tags shown.");
    case core::MapRefusal::Protected:
        return QCoreApplication::translate("HikariSub.Editor", "This touches a drawing or malformed tag: edit it with tags shown.");
    case core::MapRefusal::SplitsToken:
        return QCoreApplication::translate("HikariSub.Editor", "Select the whole line break or hard space.");
    case core::MapRefusal::Reinterpreted:
        return QCoreApplication::translate("HikariSub.Editor",
                                           "Here the text would become drawing or tag content: edit it with tags shown.");
    }
    return {};
}

} // namespace

LineEditorController::LineEditorController(application::DocumentFiles &files, QObject *parent)
    : QObject(parent), m_files(files)
{
}

application::EditSession *LineEditorController::session() const
{
    return m_document ? m_files.session(*m_document) : nullptr;
}

void LineEditorController::setDocument(std::optional<application::DocumentId> document, bool editable)
{
    m_document = document;
    m_editable = editable;
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_problem.clear();
    m_attempted.clear();
    m_dismissedFor.reset(); // a Line id of the Document before
    m_dismissedRuns.clear();
    rebuildLists(); // EditBox::SetGrid
    m_shownLine.reset();
    m_carryEvaluated.reset();
    refresh();
}

std::optional<core::LineRecord> LineEditorController::record() const
{
    auto r = sessionRecord();
    if (r && !r->translation.empty()) {
        if (const auto c = carry(*r)) {
            const auto inner = r->translation.size() - c->leading.size() - c->trailing.size();
            r->translation = r->translation.substr(c->leading.size(), inner);
        }
    }
    return r;
}

std::optional<LineEditorController::Carry> LineEditorController::carry(const core::LineRecord &line) const
{
    if (!m_moveTags || !translationMode())
        return std::nullopt;
    // The runs are read against the Line's Style (legacy GetStyle(0, name):
    // the first of that name, else the first Style), and named resets
    // against the Document's.
    const auto *styles = carryStyles();
    if (!styles)
        return std::nullopt;
    const auto style = std::ranges::find(*styles, line.style, &core::StyleValues::name);
    const auto *base = style != styles->end() ? &*style : styles->empty() ? nullptr : &styles->front();
    Carry c{core::readOriginalTags(core::toUtf16(line.text), base, *styles), {}, {}};
    if (c.tags.drawing || c.tags.malformed)
        return std::nullopt;
    c.leading = core::toUtf8(c.tags.leading);
    c.trailing = core::toUtf8(c.tags.trailing);
    const auto &t = line.translation;
    if (!t.empty() && (t.size() < c.leading.size() + c.trailing.size() || !t.starts_with(c.leading) ||
                       !t.ends_with(c.trailing)))
        return std::nullopt; // the translation's own tags differ: shown whole
    return c;
}

const std::vector<core::StyleValues> *LineEditorController::carryStyles() const
{
    static const std::vector<core::StyleValues> none;
    auto *s = session();
    if (!s)
        return &none;
    const CarryStyles::Key key{m_document->value, &s->document(), s->revision()};
    if (!m_carryStyles || m_carryStyles->key != key) {
        CarryStyles styles{key, {}, core::styleCount(s->document()) <= core::kMaxTagTransferStyles};
        if (styles.within)
            styles.styles = core::decodeStyles(s->document());
        m_carryStyles = std::move(styles);
    }
    return m_carryStyles->within ? &m_carryStyles->styles : nullptr;
}

std::u8string LineEditorController::carried(const Carry &carry, std::u8string body)
{
    return body.empty() ? body : carry.leading + body + carry.trailing;
}

std::size_t LineEditorController::carriedLead() const
{
    const auto r = sessionRecord();
    const auto c = r && !r->translation.empty() ? carry(*r) : std::nullopt;
    return c ? c->tags.leading.size() : 0;
}

std::optional<core::OriginalTags> LineEditorController::runTags() const
{
    const auto r = sessionRecord();
    auto c = r ? carry(*r) : std::nullopt;
    return c ? std::optional(std::move(c->tags)) : std::nullopt;
}

namespace {

// Whether two readings have the same runs: the same formatting on the same
// text, in the same order.
bool sameRuns(const core::OriginalTags &a, const core::OriginalTags &b)
{
    return std::ranges::equal(a.runs, b.runs, [](const core::TagRun &x, const core::TagRun &y) {
        return x.differs == y.differs && x.text == y.text;
    });
}

// What a reading keeps, in UTF-16 units, near enough to bound them.
std::size_t runUnits(const core::OriginalTags &tags)
{
    const auto state = [](const core::TagState &s) {
        std::size_t n = 0;
        for (const auto &[k, v] : s)
            n += k.size() + v.size();
        return n;
    };
    const auto setters = [](const std::map<std::u16string, core::TagSetter> &m) {
        std::size_t n = 0;
        for (const auto &[k, v] : m)
            n += k.size() + v.tag.size() + 4;
        return n;
    };
    std::size_t n = tags.leading.size() + tags.trailing.size() + state(tags.defaults) + state(tags.base);
    for (const auto &[name, s] : tags.named)
        n += name.size() + state(s);
    for (const auto &run : tags.runs)
        n += run.text.size() + state(run.differs) + setters(run.opens) + setters(run.closes) + 8;
    return n;
}

} // namespace

core::TranslationDraft LineEditorController::translationDraft(const core::OriginalTags &tags,
                                                              const core::LineRecord &line) const
{
    const auto translation = core::toUtf16(line.translation);
    auto draft = core::readTranslation(tags, translation);
    const auto it = m_runOrders.find(runOrderKey(line.id));
    if (it == m_runOrders.end())
        return draft;
    const RunOrder *composed = nullptr;
    for (const auto &entry : it->second) {
        if (entry.translation != line.translation)
            continue;
        if (sameRuns(*entry.tags, tags)) {
            auto hinted = core::readTranslation(tags, translation, entry.order);
            core::relabelRuns(tags, hinted, entry.order);
            return hinted;
        }
        if (!composed)
            composed = &entry;
    }
    if (!composed)
        return draft;
    // The runs changed under the translation: its placements as composed,
    // each given to the run it became (recarried), one gone dropped.
    auto was = core::readTranslation(*composed->tags, translation, composed->order);
    if (!was.carried)
        return draft;
    core::relabelRuns(*composed->tags, was, composed->order);
    const auto became = core::matchRuns(*composed->tags, tags);
    for (auto &p : was.placements)
        p.run = p.run >= 1 && p.run <= static_cast<int>(became.size()) ? became[p.run - 1] : 0;
    std::erase_if(was.placements, [](const core::RunPlacement &p) { return p.run == 0; });
    return was;
}

std::u8string LineEditorController::composedBody(const core::OriginalTags &tags, core::TranslationDraft draft,
                                                 core::LineId line)
{
    draft.carried = false; // carried() adds the carried tags
    std::vector<core::RunPlacement> placed;
    auto body = core::toUtf8(core::composeTranslation(tags, draft, &placed));
    auto translation = body.empty() ? body : core::toUtf8(tags.leading) + body + core::toUtf8(tags.trailing);
    noteRunOrder(line, RunOrder{std::move(translation), core::runOrder(placed),
                                std::make_shared<const core::OriginalTags>(tags)});
    return body;
}

bool LineEditorController::saveTranslationDraft(const core::OriginalTags &tags, core::TranslationDraft draft)
{
    const auto real = sessionRecord();
    if (!real)
        return false;
    // The numbers the translator chose are kept before setRaw refreshes the
    // fields, which read them back.
    const auto previous = runOrderOf(real->id);
    if (setRaw(1, composedBody(tags, std::move(draft), real->id), &previous))
        return true;
    setRunOrder(real->id, previous);
    return false;
}

std::optional<LineEditorController::RunOrder> LineEditorController::runOrderOf(core::LineId line) const
{
    const auto it = m_runOrders.find(runOrderKey(line));
    return it == m_runOrders.end() || it->second.empty() ? std::nullopt : std::optional(it->second.front());
}

// The latest entry (runOrderOf) becomes `order`; nullopt drops it.
void LineEditorController::setRunOrder(core::LineId line, std::optional<RunOrder> order)
{
    if (const auto it = m_runOrders.find(runOrderKey(line)); it != m_runOrders.end()) {
        uncountRunOrder(it->second.front());
        it->second.erase(it->second.begin());
        if (it->second.empty())
            m_runOrders.erase(it);
    }
    if (order)
        noteRunOrder(line, std::move(*order));
}

void LineEditorController::countRunOrder(RunOrderKey key, const RunOrder &entry)
{
    m_runOrderAges.emplace(entry.noted, key);
    m_runOrderSize += entry.translation.size();
    if (++m_runOrderReadings[entry.tags.get()] == 1)
        m_runOrderSize += runUnits(*entry.tags);
}

void LineEditorController::uncountRunOrder(const RunOrder &entry)
{
    m_runOrderAges.erase(entry.noted);
    m_runOrderSize -= entry.translation.size();
    const auto it = m_runOrderReadings.find(entry.tags.get());
    if (--it->second == 0) {
        m_runOrderSize -= runUnits(*entry.tags);
        m_runOrderReadings.erase(it);
    }
}

// `order` the latest entry, in place of one of the same translation and runs.
void LineEditorController::noteRunOrder(core::LineId line, RunOrder order)
{
    const auto key = runOrderKey(line);
    auto &entries = m_runOrders[key];
    // An Original read the same as before is kept once (each typed edit
    // notes an entry).
    for (const auto &e : entries)
        if (e.tags != order.tags && *e.tags == *order.tags) {
            order.tags = e.tags;
            break;
        }
    std::erase_if(entries, [&](const RunOrder &e) {
        const bool same = e.translation == order.translation && sameRuns(*e.tags, *order.tags);
        if (same)
            uncountRunOrder(e);
        return same;
    });
    order.noted = ++m_runOrderClock;
    countRunOrder(key, order);
    entries.insert(entries.begin(), std::move(order));
    while (entries.size() > kRunOrderHistory) {
        uncountRunOrder(entries.back());
        entries.pop_back();
    }
    // Every Line's together kept within kRunOrderUnits, the oldest going
    // (never the entry just noted, the newest).
    while (m_runOrderSize > kRunOrderUnits && m_runOrderAges.size() > 1) {
        const auto [noted, oldestKey] = *m_runOrderAges.begin();
        const auto list = m_runOrders.find(oldestKey);
        const auto oldest = std::ranges::find(list->second, noted, &RunOrder::noted);
        uncountRunOrder(*oldest);
        list->second.erase(oldest);
        if (list->second.empty())
            m_runOrders.erase(list);
    }
}

std::u8string LineEditorController::recarried(const core::LineRecord &before, const Carry &now)
{
    // The placements read with the old runs, each given to the run it became
    // (a run put in before it renumbers it; one gone is dropped), composed
    // with the new ones, so a run whose formatting changed takes its new tags.
    const auto old = carry(before);
    if (!old)
        return carried(now, before.translation);
    auto draft = translationDraft(old->tags, before);
    const auto became = core::matchRuns(old->tags, now.tags);
    for (auto &p : draft.placements)
        p.run = p.run >= 1 && p.run <= static_cast<int>(became.size()) ? became[p.run - 1] : 0;
    std::erase_if(draft.placements, [](const core::RunPlacement &p) { return p.run == 0; });
    return carried(now, composedBody(now.tags, std::move(draft), before.id));
}

namespace {

QString qs16(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), static_cast<qsizetype>(s.size()));
}

// What a run changes, in the run strip's words.
QString runKind(const core::TagRun &run)
{
    struct Kind {
        std::u16string_view component;
        const char *name;
    };
    static constexpr Kind kinds[] = {
        {u"i", QT_TRANSLATE_NOOP("HikariSub.Translation", "Italic")},
        {u"b", QT_TRANSLATE_NOOP("HikariSub.Translation", "Bold")},
        {u"u", QT_TRANSLATE_NOOP("HikariSub.Translation", "Underline")},
        {u"s", QT_TRANSLATE_NOOP("HikariSub.Translation", "Strikeout")},
        {u"1c", QT_TRANSLATE_NOOP("HikariSub.Translation", "Colour")},
        {u"2c", QT_TRANSLATE_NOOP("HikariSub.Translation", "Colour")},
        {u"3c", QT_TRANSLATE_NOOP("HikariSub.Translation", "Colour")},
        {u"4c", QT_TRANSLATE_NOOP("HikariSub.Translation", "Colour")},
        {u"1a", QT_TRANSLATE_NOOP("HikariSub.Translation", "Transparency")},
        {u"2a", QT_TRANSLATE_NOOP("HikariSub.Translation", "Transparency")},
        {u"3a", QT_TRANSLATE_NOOP("HikariSub.Translation", "Transparency")},
        {u"4a", QT_TRANSLATE_NOOP("HikariSub.Translation", "Transparency")},
        {u"fn", QT_TRANSLATE_NOOP("HikariSub.Translation", "Font")},
        {u"fs", QT_TRANSLATE_NOOP("HikariSub.Translation", "Size")},
        {u"fscx", QT_TRANSLATE_NOOP("HikariSub.Translation", "Scale")},
        {u"fscy", QT_TRANSLATE_NOOP("HikariSub.Translation", "Scale")},
        {u"fsp", QT_TRANSLATE_NOOP("HikariSub.Translation", "Spacing")},
        {u"frx", QT_TRANSLATE_NOOP("HikariSub.Translation", "Rotation")},
        {u"fry", QT_TRANSLATE_NOOP("HikariSub.Translation", "Rotation")},
        {u"frz", QT_TRANSLATE_NOOP("HikariSub.Translation", "Rotation")},
        {u"fax", QT_TRANSLATE_NOOP("HikariSub.Translation", "Shear")},
        {u"fay", QT_TRANSLATE_NOOP("HikariSub.Translation", "Shear")},
        {u"xbord", QT_TRANSLATE_NOOP("HikariSub.Translation", "Border")},
        {u"ybord", QT_TRANSLATE_NOOP("HikariSub.Translation", "Border")},
        {u"xshad", QT_TRANSLATE_NOOP("HikariSub.Translation", "Shadow")},
        {u"yshad", QT_TRANSLATE_NOOP("HikariSub.Translation", "Shadow")},
        {u"be", QT_TRANSLATE_NOOP("HikariSub.Translation", "Blur")},
        {u"blur", QT_TRANSLATE_NOOP("HikariSub.Translation", "Blur")},
        {u"t", QT_TRANSLATE_NOOP("HikariSub.Translation", "Animation")},
        {u"r", QT_TRANSLATE_NOOP("HikariSub.Translation", "Style")},
    };
    QStringList names;
    for (const auto &kind : kinds) {
        const QString name = QCoreApplication::translate("HikariSub.Translation", kind.name);
        if (run.differs.contains(std::u16string(kind.component)) && !names.contains(name))
            names << name;
    }
    if (names.isEmpty())
        names << QCoreApplication::translate("HikariSub.Translation", "Formatting");
    return names.join(QStringLiteral(", "));
}

// Whether a flag is on in a run, for its chip's preview: a \b0 or \i0
// over a bold or italic base turns it off, and a flag the run leaves is
// the base's. A \b weight is bold from 600.
bool runTurnsOn(const core::OriginalTags &tags, const core::TagRun &run, const std::u16string &component)
{
    auto it = run.differs.find(component);
    if (it == run.differs.end()) {
        it = tags.base.find(component);
        if (it == tags.base.end())
            return false;
    }
    if (component != u"b")
        return it->second != u"0";
    bool ok = false;
    const int weight = QString::fromStdU16String(it->second).toInt(&ok);
    return ok && (weight == 1 || weight >= 600);
}

// A translation longer than Tags from original reads, which it takes as
// plain text: no run is shown or placed in it. The UTF-8 size is checked
// first, a UTF-16 unit being three bytes at most.
bool pastTransferLimit(std::u8string_view translation)
{
    return translation.size() > 3 * core::kMaxTagTransferUnits
           || core::toUtf16(translation).size() > core::kMaxTagTransferUnits;
}

// Whether a draft with `runs` just placed in it is saved as placed: each
// is found again on reading the composed translation (one past the
// reader's limits is not, its tags left as the translator's own), and the
// translator's own formatting around them draws as it did.
bool placementHolds(const core::OriginalTags &tags, const core::TranslationDraft &draft, std::span<const int> runs)
{
    const auto order = core::runOrder(draft.placements);
    auto back = core::readTranslation(tags, core::composeTranslation(tags, draft), order);
    core::relabelRuns(tags, back, order);
    for (const int run : runs)
        if (std::ranges::none_of(back.placements, [&](const core::RunPlacement &p) { return p.run == run; }))
            return false;
    return core::keepsOwnFormatting(tags, draft);
}

// "#rrggbb" for a run that changes the primary colour (stored as BBGGRR).
QString runColour(const core::TagRun &run)
{
    const auto it = run.differs.find(u"1c");
    if (it == run.differs.end() || it->second.size() != 6)
        return {};
    const QString bgr = qs16(it->second);
    return QLatin1Char('#') + (bgr.mid(4, 2) + bgr.mid(2, 2) + bgr.left(2)).toLower();
}

} // namespace

QVariantList LineEditorController::tagRuns() const
{
    QVariantList out;
    const auto tags = runTags();
    const auto real = sessionRecord();
    if (!tags || !real || tags->runs.empty() || pastTransferLimit(real->translation)
        || pastTransferLimit(record()->translation))
        return out;
    // Where each placed run's text is: in the whole translation, then in the field.
    std::vector<core::RunPlacement> landed;
    const auto full = core::toUtf16(real->translation);
    if (!full.empty())
        core::composeTranslation(*tags, translationDraft(*tags, *real), &landed);
    const auto lead = tags->leading.size();
    const auto inner = core::project(core::toUtf16(record()->translation));
    const auto original = core::project(core::toUtf16(real->text));
    // A suggestion is in the draft's body, which has the field's visible text.
    std::vector<core::RunPlacement> suggested;
    core::Projection body;
    if (!full.empty()) {
        const auto draft = translationDraft(*tags, *real);
        suggested = suggestedRuns(*tags, draft);
        body = core::project(draft.body);
    }
    auto inField = [&](std::size_t at, bool start) {
        const auto shown = core::displayOffset(body, at);
        return static_cast<int>(m_showTags ? core::rawOffset(inner, shown, start) : shown);
    };
    for (const auto &run : tags->runs) {
        auto originalStart = run.displayStart, originalEnd = run.displayEnd;
        if (m_showTags) {
            originalStart = core::rawOffset(original, originalStart, true);
            originalEnd = core::rawOffset(original, originalEnd, false);
        }
        int start = -1, end = -1;
        for (const auto &p : landed) {
            if (p.run != run.number)
                continue;
            start = static_cast<int>(m_showTags ? p.start - lead : core::displayOffset(inner, p.start - lead));
            end = static_cast<int>(m_showTags ? p.end - lead : core::displayOffset(inner, p.end - lead));
        }
        int suggestedStart = -1, suggestedEnd = -1;
        for (const auto &p : suggested) {
            if (p.run == run.number) {
                suggestedStart = inField(p.start, true);
                suggestedEnd = inField(p.end, false);
            }
        }
        out << QVariantMap{{QStringLiteral("number"), run.number},
                           {QStringLiteral("text"), qs16(run.text)},
                           {QStringLiteral("kind"), runKind(run)},
                           {QStringLiteral("colour"), runColour(run)},
                           {QStringLiteral("italic"), runTurnsOn(*tags, run, u"i")},
                           {QStringLiteral("bold"), runTurnsOn(*tags, run, u"b")},
                           {QStringLiteral("underline"), runTurnsOn(*tags, run, u"u")},
                           {QStringLiteral("strikeout"), runTurnsOn(*tags, run, u"s")},
                           {QStringLiteral("placed"), start >= 0},
                           {QStringLiteral("originalStart"), static_cast<int>(originalStart)},
                           {QStringLiteral("originalEnd"), static_cast<int>(originalEnd)},
                           {QStringLiteral("start"), start},
                           {QStringLiteral("end"), end},
                           {QStringLiteral("suggestedStart"), suggestedStart},
                           {QStringLiteral("suggestedEnd"), suggestedEnd}};
    }
    return out;
}

bool LineEditorController::placeRun(int number, int selectionStart, int selectionEnd)
{
    const auto tags = runTags();
    const auto real = sessionRecord();
    if (!editable() || !tags || !real || pastTransferLimit(real->translation))
        return false;
    // The field's selection as projection offsets: the same text in the
    // field's raw text and in the draft's body, whose run tags are taken out.
    const auto inner = core::toUtf16(record()->translation);
    const auto shown = core::project(inner);
    auto displayed = [&](int at) {
        const auto x = static_cast<std::size_t>(std::max(at, 0));
        return m_showTags ? core::displayOffset(shown, std::min(x, inner.size())) : std::min(x, shown.text.size());
    };
    const auto from = displayed(std::min(selectionStart, selectionEnd));
    const auto to = displayed(std::max(selectionStart, selectionEnd));
    if (from >= to)
        return false;
    auto draft = translationDraft(*tags, *real);
    if (number == 0)
        number = core::nextUnplacedRun(*tags, draft.placements);
    if (number < 1 || number > static_cast<int>(tags->runs.size()))
        return false;
    const auto body = core::project(draft.body);
    const auto start = core::rawOffset(body, from, true), end = core::rawOffset(body, to, false);
    // Text across the translator's own tags is in more than one state, which
    // no one run reads back over (as core::suggestPlacements).
    if (draft.body.find(u'{', start) < end)
        return false;
    if (!core::placeRun(draft.placements, start, end, number))
        return false;
    // Taking one off (the same selection again) needs no check.
    const auto isRun = [&](const core::RunPlacement &p) { return p.run == number; };
    if (std::ranges::any_of(draft.placements, isRun) && !placementHolds(*tags, draft, std::array{number}))
        return false;
    if (!saveTranslationDraft(*tags, std::move(draft)))
        return false;
    const auto after = core::project(core::toUtf16(record()->translation));
    m_selectionStart = static_cast<int>(m_showTags ? core::rawOffset(after, from, true) : from);
    m_selectionEnd = static_cast<int>(m_showTags ? core::rawOffset(after, to, false) : to);
    m_selectionRole = 1;
    emit selectionRequested();
    return true;
}

std::vector<core::RunPlacement> LineEditorController::suggestedRuns(const core::OriginalTags &tags,
                                                                    const core::TranslationDraft &draft) const
{
    auto out = core::suggestPlacements(tags, draft);
    const auto real = sessionRecord();
    if (real && m_dismissedFor == std::tuple(real->id, real->text, real->style))
        std::erase_if(out, [&](const core::RunPlacement &p) { return m_dismissedRuns.contains(p.run); });
    if (out.empty())
        return out;
    // Offered only where accepting them all is saved as placed (placeRun's check).
    auto accepted = draft;
    std::vector<int> runs;
    for (const auto &p : out) {
        core::placeRun(accepted.placements, p.start, p.end, p.run);
        runs.push_back(p.run);
    }
    if (!placementHolds(tags, accepted, runs))
        out.clear();
    return out;
}

bool LineEditorController::acceptSuggestedRuns()
{
    const auto tags = runTags();
    const auto real = sessionRecord();
    if (!editable() || !tags || !real || real->translation.empty())
        return false;
    auto draft = translationDraft(*tags, *real);
    const auto suggested = suggestedRuns(*tags, draft);
    if (suggested.empty())
        return false;
    for (const auto &p : suggested)
        core::placeRun(draft.placements, p.start, p.end, p.run);
    return saveTranslationDraft(*tags, std::move(draft));
}

bool LineEditorController::dismissSuggestedRuns()
{
    const auto tags = runTags();
    const auto real = sessionRecord();
    if (!tags || !real || real->translation.empty())
        return false;
    const auto suggested = suggestedRuns(*tags, translationDraft(*tags, *real));
    if (suggested.empty())
        return false;
    if (const auto key = std::tuple(real->id, real->text, real->style); m_dismissedFor != key) {
        m_dismissedFor = key;
        m_dismissedRuns.clear();
    }
    for (const auto &p : suggested)
        m_dismissedRuns.insert(p.run);
    emit changed();
    return true;
}

void LineEditorController::evaluateCarry()
{
    const auto r = sessionRecord();
    m_carryEvaluated = r ? std::optional(r->id) : std::nullopt;
    // Legacy SetTextWithTags (EditBox.cpp:1811-1858) gave an untranslated
    // Line's Translated field the focus when it split tags off; here the tags
    // stay with the Original, and the caret starts the translation.
    if (!r || !r->translation.empty())
        return;
    const auto c = carry(*r);
    if (!c || (c->leading.empty() && c->trailing.empty() && c->tags.runs.empty()))
        return;
    m_selectionStart = m_selectionEnd = 0;
    m_selectionRole = 1;
    m_carryFocus = true;
}

void LineEditorController::setMoveTags(bool on)
{
    if (on == m_moveTags)
        return;
    m_moveTags = on;
    m_carryEvaluated.reset(); // OnAutoMoveTags: SetTextWithTags(true)
    refresh();
}

bool LineEditorController::unconfirmed() const
{
    const auto r = sessionRecord();
    return r && r->unconfirmed;
}

bool LineEditorController::toggleUnconfirmed()
{
    auto *s = session();
    if (!editable())
        return false;
    if (!translationMode()) {
        fail(QCoreApplication::translate("HikariSub.Editor", "Unconfirmed applies in translation mode only."));
        return false;
    }
    if (!application::toggleUnconfirmed(*s)) {
        fail(problemText());
        return false;
    }
    committed();
    return true;
}

std::optional<core::LineRecord> LineEditorController::sessionRecord() const
{
    auto *s = session();
    if (!s || !s->selection().active)
        return std::nullopt;
    if (s->draftLine() == s->selection().active)
        return s->draftRecord();
    for (const auto *line : s->document().lines())
        if (line->id == *s->selection().active)
            return *line;
    return std::nullopt;
}

bool LineEditorController::hasLine() const
{
    return record().has_value();
}

void LineEditorController::setShowTags(bool show)
{
    if (m_showTags == show)
        return;
    m_showTags = show;
    m_tagList.close(); // E6: the list belongs to the raw text it was opened in
    if (m_storeShowTags)
        m_storeShowTags(show);
    refresh();
}

void LineEditorController::setShowTagsSetting(bool show, std::function<void(bool)> store)
{
    m_storeShowTags = std::move(store);
    if (m_showTags == show)
        return;
    m_showTags = show;
    m_tagList.close();
    refresh();
}

QString LineEditorController::startText() const
{
    if (m_steppedTime[1])
        return *m_steppedTime[1];
    const auto r = record();
    return r ? qs(application::editorTimeTexts(*r, session()->document().format(), frameTimebase()).start) : QString();
}

QString LineEditorController::endText() const
{
    if (m_steppedTime[2])
        return *m_steppedTime[2];
    const auto r = record();
    return r ? qs(application::editorTimeTexts(*r, session()->document().format(), frameTimebase()).end) : QString();
}

QString LineEditorController::marginLeftText() const
{
    if (m_steppedNumber[1])
        return *m_steppedNumber[1];
    const auto r = record();
    return r ? QString::number(r->marginLeft.value) : QString();
}

QString LineEditorController::marginRightText() const
{
    if (m_steppedNumber[2])
        return *m_steppedNumber[2];
    const auto r = record();
    return r ? QString::number(r->marginRight.value) : QString();
}

QString LineEditorController::marginVerticalText() const
{
    if (m_steppedNumber[3])
        return *m_steppedNumber[3];
    const auto r = record();
    return r ? QString::number(r->marginVertical.value) : QString();
}

bool LineEditorController::dirty() const
{
    auto *s = session();
    return s && s->isDirty();
}

QString LineEditorController::saveStatus() const
{
    const auto status = m_document ? m_files.lastSave(*m_document) : std::nullopt;
    if (!status)
        return {};
    if (!status->outcome)
        return QCoreApplication::translate("HikariSub.Editor", "Saving…");
    switch (*status->outcome) {
    // Edited since: "Saved" no longer holds (the tab's mark says Modified).
    case application::WriteOutcome::Written: return dirty() ? QString() : QCoreApplication::translate("HikariSub.Editor", "Saved");
    case application::WriteOutcome::DurabilityUncertain:
        return QCoreApplication::translate("HikariSub.Editor", "Saved; the disk could not confirm it is stored");
    case application::WriteOutcome::Cancelled:
        return QCoreApplication::translate("HikariSub.Editor", "Save cancelled; nothing was written");
    case application::WriteOutcome::Failed:
        return QCoreApplication::translate("HikariSub.Editor", "Save failed; the file on disk is unchanged");
    }
    return {};
}

QString LineEditorController::problemText() const
{
    auto *s = session();
    const auto problem = s ? s->draftProblem() : std::nullopt;
    if (!problem || s->invalidCommitPolicy() == application::InvalidCommitPolicy::Legacy)
        return {};
    return *problem == application::DraftProblem::EndBeforeStart
               ? QCoreApplication::translate("HikariSub.Editor", "End is before Start.")
               : QCoreApplication::translate("HikariSub.Editor", "Margins must be between 0 and 9999.");
}

bool LineEditorController::translationMode() const
{
    auto *s = session();
    return s && s->document().scriptInfo(u8"TLMode") == u8"Yes";
}

std::pair<const core::LineRecord *, const application::DraftChange *> LineEditorController::videoLine() const
{
    // The active Line as Application::showVideoSubtitles renders it: the
    // committed Line, with the pending draft's changes when live editing is
    // on (its raw text, not the Translated field's without the carried
    // tags). Both in place, so a Line of megabytes is never copied for FS1.
    auto *s = session();
    if (!s || !s->selection().active)
        return {};
    for (const auto *line : s->document().lines())
        if (line->id == *s->selection().active)
            return {line, options().liveEditing && s->draftLine() == line->id ? s->draftChangeView() : nullptr};
    return {};
}

QString LineEditorController::videoTimes() const
{
    const auto [line, change] = videoLine();
    if (!line)
        return {};
    core::LineRecord r; // the times only
    r.start = line->start;
    r.end = line->end;
    r.startFrame = line->startFrame;
    r.endFrame = line->endFrame;
    if (change) {
        if (change->start)
            r.start.value = *change->start;
        if (change->end)
            r.end.value = *change->end;
        if (change->startFrame)
            r.startFrame = *change->startFrame;
        if (change->endFrame)
            r.endFrame = *change->endFrame;
    }
    const auto times = application::editorTimeTexts(r, session()->document().format(), frameTimebase());
    return times.end.empty() ? qs(times.start) : qs(times.start) + QStringLiteral(" – ") + qs(times.end);
}

QString LineEditorController::videoText() const
{
    const auto [line, change] = videoLine();
    if (!line)
        return {};
    const std::u8string &original = change && change->text ? *change->text : line->text;
    const std::u8string &translation = change && change->translation ? *change->translation : line->translation;
    const std::u8string &text = translationMode() && !translation.empty() ? translation : original;
    // Only the part FS1's label reads, cut before the conversion.
    const std::string_view cut =
        application::fullscreenLineInput(std::string_view(reinterpret_cast<const char *>(text.data()), text.size()));
    return QString::fromUtf8(cut.data(), qsizetype(cut.size()));
}

void LineEditorController::refresh()
{
    if (const auto real = sessionRecord(); (real ? std::optional(real->id) : std::nullopt) != m_carryEvaluated)
        evaluateCarry(); // legacy SetLine runs SetTextWithTags
    const auto r = record();
    if ((r ? std::optional(r->id) : std::nullopt) != m_shownLine)
        lineShown();
    for (int role = 0; role < 2; ++role) {
        if (!r)
            m_shown[role].clear();
        else if (m_showTags)
            m_shown[role] = qs(roleText(*r, role));
        else {
            const auto p = core::project(core::toUtf16(roleText(*r, role)));
            m_shown[role] = QString::fromUtf16(p.text.data(), static_cast<qsizetype>(p.text.size()));
        }
    }
    if (m_attempted.isEmpty())
        m_problem = problemText();
    emit changed();
    if (std::exchange(m_carryFocus, false)) {
        emit selectionRequested();
        emit fieldFocusRequested(1);
    }
}

void LineEditorController::fail(const QString &problem, const QString &attempted)
{
    m_problem = problem;
    m_attempted = attempted;
    emit changed();
}

bool LineEditorController::showLine(qulonglong id)
{
    auto *s = session();
    if (!s)
        return false;
    const auto before = s->historySize();
    const auto revision = s->revision();
    const auto left = s->draftLine();
    if (!s->navigateTo(core::LineId{id})) {
        fail(problemText());
        if (s->selection().active)
            emit lineChanged(s->selection().active->value);
        return false;
    }
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_attempted.clear();
    m_shownLine.reset(); // EditBox::SetLine shows the Line again
    m_carryEvaluated.reset(); // E5: SetLine
    if (s->historySize() != before || s->revision() != revision) {
        if (left && left->value != id)
            emit leftLineCommitted(left->value);
        committed();
    }
    refresh();
    return true;
}

bool LineEditorController::translationTyped()
{
    const auto *s = session();
    const auto change = s ? s->draftChange() : std::nullopt;
    return change && change->second.translation && !change->second.translationCarried;
}

bool LineEditorController::setRaw(int role, std::u8string raw, const std::optional<RunOrder> *orderBefore)
{
    auto *s = session();
    const auto r = record();
    if (!s || !r || !m_editable)
        return false;
    if (raw == roleText(*r, role)) {
        // TM1: another of two equal runs placed on the same text changes
        // only the run order: a step of its own, for Escape and Undo.
        const auto now = runOrderOf(r->id);
        if (orderBefore && (orderBefore->has_value() != now.has_value() ||
                            (now && ((*orderBefore)->order != now->order || (*orderBefore)->translation != now->translation)))) {
            const auto real = sessionRecord();
            m_draftUndo.push_back({real->text, real->translation, translationTyped(), *orderBefore});
            m_draftRedo.clear();
            m_attempted.clear();
            refresh();
        }
        return true;
    }
    auto order = orderBefore ? *orderBefore : runOrderOf(r->id);
    const auto real = sessionRecord();
    const auto c = carry(*real);
    const bool typed = translationTyped();
    application::DraftChange change;
    if (role == 1) {
        change.translation = c ? carried(*c, std::move(raw)) : std::move(raw);
    } else {
        // TM1: the translation takes the Original's new carried tags, in the
        // same step; with several Lines selected each keeps its own.
        auto after = *real;
        after.text = raw;
        after.translation.clear();
        if (const auto next = c && !r->translation.empty() ? carry(after) : std::nullopt) {
            change.translation = recarried(*real, *next);
            change.translationCarried = true;
        } else if (c && !r->translation.empty()) {
            // The new Original carries nothing (a drawing, unclosed braces):
            // the translation keeps its own text and tags, the carried ones
            // and the placed runs' gone.
            const auto was = translationDraft(c->tags, *real);
            if (was.carried) {
                change.translation = core::toUtf8(was.body);
                change.translationCarried = true;
            }
        }
        change.text = std::move(raw);
    }
    if (!s->editDraft(r->id, change))
        return false;
    // TM1: the runs placed keep their numbers through a raw edit of the
    // translation (text typed inside a run with tags shown): equal runs read
    // back as the ones the translator chose. (A caller giving the order
    // before composed the new one already.)
    if (role == 1 && !orderBefore && c && order && change.translation && sameRuns(*order->tags, c->tags))
        noteRunOrder(r->id, RunOrder{*change.translation, order->order, order->tags});
    m_draftUndo.push_back({real->text, real->translation, typed, std::move(order)});
    m_draftRedo.clear();
    m_attempted.clear();
    refresh();
    return true;
}

void LineEditorController::textEdited(const QString &newText, int cursor)
{
    edit(0, newText, cursor);
}

void LineEditorController::translationEdited(const QString &newText, int cursor)
{
    edit(1, newText, cursor);
}

void LineEditorController::edit(int role, const QString &newText, int cursor)
{
    if (!editable()) {
        refresh(); // a protected or empty editor never changes content
        return;
    }
    if (newText == m_shown[role])
        return;
    const QString old = m_shown[role];
    // Locate the change: the common suffix may not reach past the caret, so
    // repeated characters resolve to where the user actually typed.
    const qsizetype caret = std::clamp<qsizetype>(cursor, 0, newText.size());
    qsizetype suffix = 0;
    while (suffix < old.size() && suffix < newText.size() - caret &&
           old[old.size() - 1 - suffix] == newText[newText.size() - 1 - suffix])
        ++suffix;
    qsizetype prefix = 0;
    const qsizetype maxPrefix = std::min(old.size(), newText.size()) - suffix;
    while (prefix < maxPrefix && old[prefix] == newText[prefix])
        ++prefix;
    const QString inserted = newText.mid(prefix, newText.size() - suffix - prefix);
    const auto start = static_cast<std::size_t>(prefix);
    const auto end = static_cast<std::size_t>(old.size() - suffix);

    auto graphemes = [](const QString &text) {
        std::vector<std::size_t> bounds{0};
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
        for (qsizetype b; (b = finder.toNextBoundary()) >= 0;)
            bounds.push_back(static_cast<std::size_t>(b));
        return bounds;
    };
    // TM1: with the Original's runs placed, the edit goes to the draft's body
    // (the field's text without the runs' tags) and the placements move with
    // it, so a run whose text is deleted takes its tags along. `from` and `to`
    // are visible-text offsets, the same in the field and in the body.
    const auto tags = role == 1 ? runTags() : std::nullopt;
    auto editDraft = [&](std::size_t from, std::size_t to, const std::vector<std::size_t> &bounds) {
        auto draft = translationDraft(*tags, *sessionRecord());
        const auto result = core::mappedReplace(draft.body, from, to, u16(inserted), bounds);
        if (!result) {
            refresh();
            fail(refusalText(result.error()), newText);
            return false;
        }
        const auto body = core::project(draft.body);
        const auto rawStart = core::rawOffset(body, from, true);
        const auto rawEnd = std::max(rawStart, core::rawOffset(body, to, false));
        core::shiftPlacements(draft.placements, rawStart, rawEnd, result->size() + (rawEnd - rawStart) - draft.body.size());
        draft.body = *result;
        return saveTranslationDraft(*tags, std::move(draft));
    };
    if (m_showTags) {
        // With the tags shown, an edit that removes visible text only (no
        // tag in what it takes or types) goes to the draft as above, so a
        // placed run's emptied wrapper goes too. Any other edit (one typing
        // a \N among them), and typing on either side of a tag, is raw text.
        const auto shown = core::project(u16(old));
        const auto from = core::displayOffset(shown, start), to = core::displayOffset(shown, end);
        const auto real = sessionRecord();
        const bool placed = tags && real && !real->translation.empty() && [&] {
            const auto draft = translationDraft(*tags, *real);
            return draft.carried && !draft.placements.empty();
        }();
        if (placed && end > start && to - from == end - start && !inserted.contains(u'{') && !inserted.contains(u'}')
            && !inserted.contains(u'\\')) {
            if (editDraft(from, to, graphemes(QString::fromStdU16String(shown.text)))) {
                const auto field = core::project(core::toUtf16(record()->translation));
                m_selectionStart = m_selectionEnd =
                    static_cast<int>(core::rawOffset(field, from + static_cast<std::size_t>(inserted.size()), false));
                m_selectionRole = 1;
                emit selectionRequested();
            }
            return;
        }
        setRaw(role, u8(newText));
        return;
    }
    const auto bounds = graphemes(old);
    if (tags && !tags->runs.empty()) {
        editDraft(start, end, bounds);
        return;
    }
    const auto raw = core::toUtf16(roleText(*record(), role));
    const auto result = core::mappedReplace(raw, start, end, u16(inserted), bounds);
    if (!result) {
        refresh(); // the field shows the unchanged source again
        fail(refusalText(result.error()), newText);
        return;
    }
    setRaw(role, core::toUtf8(*result));
}

void LineEditorController::setStartText(const QString &text)
{
    setTime(application::TimeFieldRole::Start, text);
}

void LineEditorController::setEndText(const QString &text)
{
    setTime(application::TimeFieldRole::End, text);
}

// EditBox::Send's StartEdit/EndEdit->GetTime (EditBox.cpp:571-580); with live
// editing OnEdit also shows the new Duration (EditBox.cpp:1526-1544).
bool LineEditorController::setTime(application::TimeFieldRole role, const QString &text)
{
    const auto r = record();
    if (!editable() || !r)
        return false;
    auto *s = session();
    const auto format = s->document().format();
    const auto typed = application::typedTime(u8(text), role, format, s->document().frameRate(), frameTimebase());
    const bool start = role == application::TimeFieldRole::Start;
    if (!typed) {
        if (frameTimebase())
            fail(start ? QCoreApplication::translate("HikariSub.Editor", "Start is not a frame number.")
                       : QCoreApplication::translate("HikariSub.Editor", "End is not a frame number."),
                 text);
        else if (format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText)
            fail(start ? QCoreApplication::translate("HikariSub.Editor", "Start is not a time such as 0:00:01.00.")
                       : QCoreApplication::translate("HikariSub.Editor", "End is not a time such as 0:00:01.00."),
                 text);
        else
            fail(start ? QCoreApplication::translate("HikariSub.Editor", "Start is not a time such as %1.")
                             .arg(qs(application::editorTimeTexts(core::LineRecord{}, format, nullptr).start))
                       : QCoreApplication::translate("HikariSub.Editor", "End is not a time such as %1.")
                             .arg(qs(application::editorTimeTexts(core::LineRecord{}, format, nullptr).end)),
                 text);
        return false;
    }
    m_steppedTime[static_cast<int>(role)].reset(); // the field's text is the Line's now
    application::DraftChange change;
    if (typed->timeResolved)
        (start ? change.start : change.end) = core::DocumentTime(typed->ms * 1000);
    if (typed->frame)
        (start ? change.startFrame : change.endFrame) = std::optional<std::int64_t>(*typed->frame);
    m_attempted.clear();
    m_typedDuration.reset();
    m_durationEdited = options().liveEditing;
    s->editDraft(r->id, change);
    refresh();
    return true;
}

void LineEditorController::setMarginText(int which, const QString &text)
{
    const auto r = record();
    if (!editable() || !r)
        return;
    if (which >= 0 && which <= 2)
        m_steppedNumber[which + 1].reset();
    bool ok = false;
    const qlonglong value = text.trimmed().toLongLong(&ok);
    if (!ok) {
        if (session()->invalidCommitPolicy() == application::InvalidCommitPolicy::Legacy) {
            refresh(); // NumCtrl keeps the previous value
            return;
        }
        fail(QCoreApplication::translate("HikariSub.Editor", "Margins must be whole numbers."), text);
        return;
    }
    application::DraftChange change;
    (which == 0 ? change.marginLeft : which == 1 ? change.marginRight : change.marginVertical) = value;
    m_attempted.clear();
    session()->editDraft(r->id, change);
    refresh();
}

bool LineEditorController::commit()
{
    auto *s = session();
    // Send runs even with nothing changed, and puts the time fields back in
    // the text colour first (EditBox.cpp:546-554).
    if (m_timeWarning[0] || m_timeWarning[1] || m_timeWarning[2]) {
        clearTimeWarnings();
        emit changed();
    }
    applyStepped(); // the modified fields first (EditBox.cpp:560-605)
    if (!s || !s->draftLine())
        return false;
    return sendDraft();
}

bool LineEditorController::applyChanges()
{
    // EditBox::OnCommit: Send(EDITBOX_LINE_EDITION, false, false, Visual != 0)
    // (EditBox.cpp:972); with a visual tool on (any but the crosshair) its
    // visualdummy keeps the time fields' warning colour.
    if (!visualToolOn())
        return commit();
    applyStepped();
    auto *s = session();
    if (!s || !s->draftLine())
        return false;
    return sendDraft(false, true);
}

// EditBox::Send(EDITBOX_LINE_EDITION, false): the draft's fields go to the
// Line, or to every selected Line with several selected (SubsGrid::ChangeLine);
// an Actor or Effect change rebuilds their lists (EditBox.cpp:633-636).
bool LineEditorController::sendDraft(bool leaving, bool visual)
{
    if (!visual)
        clearTimeWarnings(); // Send, EditBox.cpp:546-554
    applyStepped();
    auto *s = session();
    if (!s || !s->draftLine())
        return true;
    const auto change = s->draftChange();
    const bool lists = change && (change->second.actor || change->second.effect);
    // TM1: another selected Line's translation follows its own new text or
    // Style, as the edited Line's did (setRaw, chooseStyle).
    const auto companion = [this](const core::LineRecord &before, core::LineRecord &after) {
        const auto c = before.translation.empty() ? std::nullopt : carry(before);
        if (!c)
            return;
        auto bare = after;
        bare.translation.clear();
        if (const auto next = carry(bare)) {
            after.translation = recarried(before, *next);
        } else if (const auto was = translationDraft(c->tags, before); was.carried) {
            // Its new Original carries nothing (a drawing): its own text and
            // tags stay, the carried ones and the placed runs' go.
            after.translation = core::toUtf8(was.body);
        }
    };
    if (!s->commitDraftToSelected("Edit Line", leaving, companion)) {
        if (s->draftLine()) {
            fail(problemText());
            return false;
        }
    }
    if (lists)
        rebuildLists();
    committed();
    return true;
}

bool LineEditorController::commitAndAdvance()
{
    // EditBox::OnNewline -> Send(..., gotoNextLine) -> SubsGrid::NextLine.
    return nextLine(1);
}

// SubsGrid::NextLine (SubsGridBase.cpp:1382-1415), also GLOBAL_PREVIOUS_LINE /
// GLOBAL_NEXT_LINE (E6, HikariSubFrame::OnChangeLine): the shown Line after
// or before the active one (GetKeyFromPosition(currentLine, direction, false)
// skips hidden Lines). Before the first one nothing happens; after the last
// shown one a Line is appended.
bool LineEditorController::nextLine(int direction)
{
    auto *s = session();
    const auto r = record();
    if (!s || !r)
        return false;
    // EditBox::Send(EDITBOX_LINE_EDITION, true): the fields first (to every
    // selected Line with several selected), then SubsGrid::NextLine, whose
    // SetLine corrects the left Line under the legacy preference.
    {
        const auto all = s->document().lines();
        const bool last = !all.empty() && all.back()->id == r->id;
        applyStepped();
        if (s->draftLine() && !sendDraft(!last))
            return false;
    }
    const auto lines = s->document().lines();
    const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == r->id; });
    if (it == lines.end())
        return false;
    const std::ptrdiff_t at = it - lines.begin();
    for (std::ptrdiff_t i = at + direction; i >= 0 && i < static_cast<std::ptrdiff_t>(lines.size()); i += direction) {
        const core::LineRecord *line = lines[static_cast<std::size_t>(i)];
        if (line->visibility == core::LineVisibility::Hidden)
            continue;
        if (!showLine(line->id.value))
            return false;
        emit lineChanged(line->id.value);
        return true;
    }
    if (direction < 0)
        return false;
    {
        // No shown Line after it: append a copy of the Line
        // GetDialogue(GetElementByKey(size - 1)) names, starting at its End,
        // five seconds long, with no text (its own step). GetElementByKey
        // gives the number of shown Lines before the last one, read as a
        // row: the last Line when nothing is hidden.
        // (E4: the draft was sent above.)
        const auto now = s->document().lines();
        std::size_t source = 0;
        for (std::size_t i = 0; i + 1 < now.size(); ++i)
            source += now[i]->visibility != core::LineVisibility::Hidden;
        const core::LineRecord &copied = *now[std::min(source, now.size() - 1)];
        const core::LineRecord &last = *now.back();
        core::LineRecord next = copied;
        next.text.clear();
        next.translation.clear();
        next.start.value = copied.end.value;
        next.end.value = core::DocumentTime(copied.end.value.microseconds() + 5'000'000);
        if (s->document().format() == core::SubtitleFormat::MicroDvd) {
            // Frames: the new Line starts at the copied end frame; its end is the
            // frame at End + 5 s rounded up, as legacy SubsTime computes it.
            // Without the Document's own rate (C01-fps-isolation) the end
            // frame is left empty rather than guessed.
            next.startFrame = copied.endFrame;
            next.endFrame.reset();
            if (const auto &rate = s->document().frameRate()) {
                const auto &fps = rate->framesPerSecond();
                if (const auto frame = core::mulDiv(next.end.value.microseconds(), fps.numerator(),
                                                    fps.denominator(), 1'000'000, core::Rounding::Ceil))
                    next.endFrame = *frame;
            }
        }
        std::optional<core::LineId> added;
        const auto result = s->run(application::Command{
            "Append Line", s->revision(), {last.id}, [&](core::Document &d) {
                added = d.insertLineAfter(last.id, next); // AddLine: at the end
                return added.has_value();
            }});
        if (!result || !added)
            return false;
        committed();
        if (!showLine(added->value))
            return false;
        emit lineChanged(added->value);
        return true;
    }
}

namespace {

// EditBox::SetAlignment: the Style's alignment (GetStyle(0, name): the first
// Style of that name, else the first Style), unless the text has \an.
int shownAlignment(const core::LineRecord &line, const std::vector<core::StyleValues> &styles)
{
    int styleAlignment = 2; // a Document without Styles gets a default one
    const core::StyleValues *chosen = styles.empty() ? nullptr : &styles.front();
    for (const auto &style : styles)
        if (style.name == line.style) {
            chosen = &style;
            break;
        }
    if (chosen)
        styleAlignment = std::atoi(reinterpret_cast<const char *>(chosen->alignment.c_str()));
    return application::legacyAlignment(line, styleAlignment);
}

// Back at the committed Line (Undo or Redo of the draft's steps): no draft
// remains.
void dropUnchangedDraft(application::EditSession &s, core::LineId id)
{
    const auto lines = s.document().lines();
    const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == id; });
    const auto d = s.draftRecord();
    if (it != lines.end() && d && d->text == (*it)->text && d->translation == (*it)->translation &&
        d->start.value == (*it)->start.value && d->end.value == (*it)->end.value &&
        d->marginLeft.value == (*it)->marginLeft.value && d->marginRight.value == (*it)->marginRight.value &&
        d->marginVertical.value == (*it)->marginVertical.value && d->comment == (*it)->comment &&
        d->layer.value == (*it)->layer.value && d->style == (*it)->style && d->actor == (*it)->actor &&
        d->effect == (*it)->effect && d->startFrame == (*it)->startFrame && d->endFrame == (*it)->endFrame)
        s.discardDraft();
}

} // namespace

void LineEditorController::discard()
{
    auto *s = session();
    // TM1: the run order goes back with the text, to the draft's first
    // step's (a step that changed only the order leaves no draft).
    const auto active = s ? record() : std::nullopt;
    if (const auto line = s && s->draftLine() ? s->draftLine() : active ? std::optional(active->id) : std::nullopt;
        line && !m_draftUndo.empty())
        setRunOrder(*line, m_draftUndo.front().order);
    if (s)
        s->discardDraft();
    clearTimeWarnings(); // the shown times are the Line's again
    dropStepped();
    // Duration shows End - Start again, not a Duration typed before
    // (E4-esc-resets-duration; legacy DurEdit kept it until SetLine).
    m_typedDuration.reset();
    m_durationEdited = false;
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_attempted.clear();
    // LE4: the position shown is the Line's again, not the discarded choice.
    if (const auto r = s ? committedRecord() : std::nullopt)
        m_alignment = shownAlignment(*r, core::decodeStyles(s->document()));
    refresh();
    emit discarded();
}

void LineEditorController::placeCaretAfterChange(const QString (&before)[2])
{
    for (int role = 0; role < 2; ++role) {
        const QString &a = before[role], &b = m_shown[role];
        if (a == b)
            continue;
        qsizetype prefix = 0;
        while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
            ++prefix;
        qsizetype suffix = 0;
        while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
               a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
            ++suffix;
        qsizetype caret = b.size() - suffix;
        if (caret > 0 && caret < b.size() && b[caret].isLowSurrogate())
            ++caret; // never between the halves of a surrogate pair
        m_selectionStart = m_selectionEnd = static_cast<int>(caret);
        m_selectionRole = role;
        emit selectionRequested();
        return;
    }
}

bool LineEditorController::undo()
{
    auto *s = session();
    if (!s)
        return false;
    const auto r = record();
    const QString before[2] = {m_shown[0], m_shown[1]};
    const auto line = s->selection().active;
    if (!m_draftUndo.empty() && r) {
        // The whole translation, the carried tags too (record() strips them).
        const auto whole = sessionRecord();
        m_draftRedo.push_back({whole->text, whole->translation, translationTyped(), runOrderOf(r->id)});
        const Snapshot previous = std::move(m_draftUndo.back());
        m_draftUndo.pop_back();
        setRunOrder(r->id, previous.order);
        s->editDraft(r->id, application::DraftChange{.text = previous.text,
                                                     .translation = previous.translation,
                                                     .translationCarried = !previous.translationTyped,
                                                     .restoresProvenance = true});
        dropUnchangedDraft(*s, r->id);
        m_attempted.clear();
        refresh();
        placeCaretAfterChange(before);
        return true;
    }
    const auto revision = s->revision();
    if (!s->undo()) {
        fail(problemText());
        return false;
    }
    m_shownLine.reset(); // the restored Line is shown again (EditBox::SetLine)
    if (s->revision() != revision)
        committed();
    if (s->selection().active == line)
        placeCaretAfterChange(before);
    if (s->selection().active)
        emit lineChanged(s->selection().active->value);
    return true;
}

bool LineEditorController::redo()
{
    auto *s = session();
    if (!s)
        return false;
    const auto r = record();
    const QString before[2] = {m_shown[0], m_shown[1]};
    const auto line = s->selection().active;
    if (!m_draftRedo.empty() && r) {
        const auto whole = sessionRecord(); // as Undo's
        m_draftUndo.push_back({whole->text, whole->translation, translationTyped(), runOrderOf(r->id)});
        const Snapshot next = std::move(m_draftRedo.back());
        m_draftRedo.pop_back();
        setRunOrder(r->id, next.order);
        s->editDraft(r->id, application::DraftChange{.text = next.text,
                                                     .translation = next.translation,
                                                     .translationCarried = !next.translationTyped,
                                                     .restoresProvenance = true});
        dropUnchangedDraft(*s, r->id); // a step that changed only the run order
        refresh();
        placeCaretAfterChange(before);
        return true;
    }
    if (!s->redo())
        return false;
    m_shownLine.reset();
    committed();
    if (s->selection().active == line)
        placeCaretAfterChange(before);
    if (s->selection().active)
        emit lineChanged(s->selection().active->value);
    return true;
}

bool LineEditorController::save()
{
    if (!m_document)
        return false;
    auto plan = m_files.prepareSave(*m_document);
    if (!plan) {
        fail(plan.error() == application::SaveRefusal::InvalidDraft
                 ? problemText()
                 : QCoreApplication::translate("HikariSub.Editor", "This document can't be saved here."));
        return false;
    }
    committed(); // Save committed the draft first
    auto started = m_files.startSave(std::move(*plan));
    if (!started) {
        switch (started.error()) {
        case application::SaveRefusal::ExternalChange:
            fail(QCoreApplication::translate("HikariSub.Editor",
                                             "The file changed on disk since it was opened. Reload it, or save elsewhere."));
            break;
        case application::SaveRefusal::Collision:
            fail(QCoreApplication::translate("HikariSub.Editor", "Another open document uses that file."));
            break;
        default:
            fail(QCoreApplication::translate("HikariSub.Editor", "The document can't be saved right now."));
            break;
        }
        return false;
    }
    refresh();
    return true;
}

bool LineEditorController::toggleTag(const QString &tag, int selectionStart, int selectionEnd)
{
    return toggleTagIn(0, tag, selectionStart, selectionEnd);
}

bool LineEditorController::toggleTagIn(int role, const QString &tag, int selectionStart, int selectionEnd)
{
    const auto r = record();
    if (!editable() || !r || tag.size() != 1 || !QStringLiteral("bius").contains(tag))
        return false;
    // TM1: over a selection in the Translated field, the Original's run of
    // that style is placed, as a word processor's Ctrl+I would.
    if (role == 1 && selectionStart != selectionEnd) {
        if (const auto tags = runTags()) {
            const auto draft = translationDraft(*tags, *sessionRecord());
            if (const int run = core::runWithProperty(*tags, draft.placements, u16(tag)))
                return placeRun(run, selectionStart, selectionEnd);
        }
    }
    // The Style's value; legacy GetStyle(0, name) takes the first Style of that name.
    bool styleValue = false;
    for (const auto &style : core::decodeStyles(session()->document()))
        if (style.name == r->style) {
            const char16_t t = tag[0].unicode();
            styleValue = t == u'b' ? style.bold : t == u'i' ? style.italic : t == u'u' ? style.underline
                                                                                      : style.strikeOut;
            break;
        }
    // Legacy subsFormat < SRT (ASS, and plain text, which loads as ASS) uses
    // override tags; SRT and MicroDVD use their own markup (PutinNonass).
    const auto format = session()->document().format();
    if (format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::toggleTag(std::move(t), tag[0].unicode(), styleValue);
        });
    if (format == core::SubtitleFormat::Srt || format == core::SubtitleFormat::MicroDvd)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::toggleNonAssTag(std::move(t), tag[0].unicode(), format == core::SubtitleFormat::Srt);
        });
    return false; // MPL2 and TMPlayer: no legacy action
}

bool LineEditorController::editRaw(int role, int selectionStart, int selectionEnd,
                                   const std::function<core::legacy::EditorText(core::legacy::EditorText)> &change)
{
    const auto r = record();
    if (!r)
        return false;
    if (role == 1 && editPlacedBody(selectionStart, selectionEnd, change))
        return true;
    const std::u16string raw = core::toUtf16(roleText(*r, role));
    long from = selectionStart, to = selectionEnd;
    std::optional<core::Projection> projection;
    if (!m_showTags) {
        projection = core::project(raw);
        // The caret goes after boundary tags; a selection's end before them.
        from = static_cast<long>(core::rawOffset(*projection, static_cast<std::size_t>(selectionStart), true));
        to = selectionEnd == selectionStart
                 ? from
                 : static_cast<long>(core::rawOffset(*projection, static_cast<std::size_t>(selectionEnd), false));
    }
    const core::legacy::EditorText result = change({raw, from, to});
    if (!setRaw(role, core::toUtf8(result.text)))
        return false;
    if (m_showTags) {
        m_selectionStart = static_cast<int>(result.selectionStart);
        m_selectionEnd = static_cast<int>(result.selectionEnd);
    } else {
        const auto after = core::project(result.text);
        m_selectionStart = static_cast<int>(core::displayOffset(after, static_cast<std::size_t>(result.selectionStart)));
        m_selectionEnd = static_cast<int>(core::displayOffset(after, static_cast<std::size_t>(result.selectionEnd)));
    }
    m_selectionRole = role;
    emit selectionRequested();
    return true;
}

// TM1: a tag edit in a Translated field with placed runs goes to the draft's
// body, the translator's own text, and the placements stay where their text
// is: the run tags around it are written again, not edited as raw text,
// which would no longer read as the Original's runs.
bool LineEditorController::editPlacedBody(int selectionStart, int selectionEnd,
                                          const std::function<core::legacy::EditorText(core::legacy::EditorText)> &change)
{
    const auto tags = runTags();
    const auto real = sessionRecord();
    if (!tags || !real || real->translation.empty())
        return false;
    auto draft = translationDraft(*tags, *real);
    if (!draft.carried || draft.placements.empty())
        return false;
    // The field's selection as visible-text offsets, the same in the field
    // and in the body (placeRun).
    const auto inner = core::toUtf16(record()->translation);
    const auto shown = core::project(inner);
    auto displayed = [&](int at) {
        const auto x = static_cast<std::size_t>(std::max(at, 0));
        return m_showTags ? core::displayOffset(shown, std::min(x, inner.size())) : std::min(x, shown.text.size());
    };
    const auto fromShown = displayed(selectionStart), toShown = displayed(selectionEnd);
    const auto before = core::project(draft.body);
    std::vector<std::pair<std::size_t, std::size_t>> placed;
    for (const auto &p : draft.placements)
        placed.emplace_back(core::displayOffset(before, p.start), core::displayOffset(before, p.end));
    const long from = static_cast<long>(core::rawOffset(before, fromShown, true));
    const long to = selectionStart == selectionEnd ? from : static_cast<long>(core::rawOffset(before, toShown, false));
    const core::legacy::EditorText result = change({draft.body, from, to});
    const auto after = core::project(result.text);
    if (after.text != before.text)
        return false; // the edit changed the text itself: as raw text
    draft.body = result.text;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        draft.placements[i].start = core::rawOffset(after, placed[i].first, true);
        draft.placements[i].end = core::rawOffset(after, placed[i].second, false);
    }
    if (!saveTranslationDraft(*tags, std::move(draft)))
        return false;
    const auto selStart = core::displayOffset(after, static_cast<std::size_t>(result.selectionStart));
    const auto selEnd = core::displayOffset(after, static_cast<std::size_t>(result.selectionEnd));
    const auto field = core::project(core::toUtf16(record()->translation));
    m_selectionStart = static_cast<int>(m_showTags ? core::rawOffset(field, selStart, true) : selStart);
    m_selectionEnd = static_cast<int>(m_showTags ? core::rawOffset(field, selEnd, false) : selEnd);
    m_selectionRole = 1;
    emit selectionRequested();
    return true;
}

std::optional<std::u16string> LineEditorController::styleTagValue(std::u16string_view tag) const
{
    const auto r = record();
    if (!r)
        return std::nullopt;
    // Legacy GetStyle(0, name): the first Style of that name.
    for (const auto &style : core::decodeStyles(session()->document()))
        if (style.name == r->style) {
            const auto value = core::legacy::styleTagValue(style, core::toUtf8(std::u16string(tag)));
            return value ? std::optional(core::toUtf16(*value)) : std::nullopt;
        }
    return std::nullopt;
}

bool LineEditorController::applyTagButton(int role, const QString &tag, int type, int selectionStart, int selectionEnd)
{
    const auto r = record();
    if (!editable() || !r || tag.isEmpty() || type < 0 || type > 2)
        return false;
    const std::u16string text = tag.toStdU16String();
    if (type != 2)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::applyTagButton(std::move(t), text, type == 1,
                                                [this](std::u16string_view name) { return styleTagValue(name); });
        });
    auto *s = session();
    if (s->selection().selected.size() < 2)
        return editRaw(role, selectionStart, selectionEnd, [&](core::legacy::EditorText t) {
            return core::legacy::insertTagButtonText(std::move(t), text);
        });
    // Several Lines: the text goes into each selected Line's text (its
    // translation when it has one) at the caret, as one step. With the tags
    // hidden the caret is a place in the visible text, found in each Line's.
    const auto shown = static_cast<std::size_t>(std::max(selectionStart, 0));
    if (s->draftLine() && !s->commitDraft())
        return false;
    std::vector<core::LineId> lines;
    for (const auto *l : s->document().lines())
        if (s->selection().selected.contains(l->id))
            lines.push_back(l->id);
    const auto ran = s->run(application::Command{
        "Editing multiple lines", s->revision(), {lines.begin(), lines.end()}, [&](core::Document &d) {
            for (const auto id : lines)
                if (!d.editLine(id, [&](core::LineRecord &l) {
                        auto &field = l.translation.empty() ? l.text : l.translation;
                        auto units = core::toUtf16(field);
                        const auto c = l.translation.empty() ? std::nullopt : carry(l);
                        // TM1: placed runs stay placed: the text goes into the
                        // draft's body and the runs' tags are written again
                        // around the same text (editPlacedBody).
                        if (c && !m_showTags) {
                            auto draft = translationDraft(c->tags, l);
                            if (draft.carried && !draft.placements.empty()) {
                                const auto body = core::project(draft.body);
                                const auto at = core::rawOffset(body, std::min(shown, body.text.size()), true);
                                auto inserted = core::legacy::insertTagButtonTextAt(draft.body, static_cast<long>(at), text);
                                core::shiftPlacements(draft.placements, at, at, inserted.size() - draft.body.size());
                                draft.body = std::move(inserted);
                                composedBody(c->tags, draft, l.id);
                                field = core::toUtf8(core::composeTranslation(c->tags, draft));
                                return;
                            }
                        }
                        // Else the caret is in the Translated field's text, after
                        // each Line's own carried leading block and before its
                        // trailing one.
                        const std::size_t lead = c ? std::min(c->tags.leading.size(), units.size()) : 0;
                        const std::size_t trail = c ? std::min(c->tags.trailing.size(), units.size() - lead) : 0;
                        std::size_t at = shown;
                        if (!m_showTags) {
                            const auto visible =
                                core::project(std::u16string_view(units).substr(lead, units.size() - lead - trail));
                            at = core::rawOffset(visible, std::min(shown, visible.text.size()), true);
                        }
                        at = std::min(at + lead, units.size() - trail);
                        field = core::toUtf8(core::legacy::insertTagButtonTextAt(std::move(units), static_cast<long>(at), text));
                    }))
                    return false;
            return true;
        }});
    if (!ran)
        return false;
    reloadFromSession();
    if (m_onCommitted)
        m_onCommitted();
    return true;
}

std::optional<core::StyleValues> LineEditorController::lineStyle() const
{
    const auto r = record();
    if (!r)
        return std::nullopt;
    // Legacy GetStyle(0, name): the first Style of that name.
    for (auto &style : core::decodeStyles(session()->document()))
        if (style.name == r->style)
            return style;
    return std::nullopt;
}

bool LineEditorController::beginDialog(int role, int selectionStart, int selectionEnd)
{
    m_dialog.reset();
    const auto r = record();
    if (!editable() || !r)
        return false;
    auto *s = session();
    DialogSession d;
    d.role = role;
    d.several = s->selection().selected.size() >= 2;
    const auto format = s->document().format();
    d.ass = format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText;
    d.format = format == core::SubtitleFormat::Srt        ? core::legacy::NonAssFormat::Srt
               : format == core::SubtitleFormat::MicroDvd ? core::legacy::NonAssFormat::MicroDvd
               : format == core::SubtitleFormat::Mpl2     ? core::legacy::NonAssFormat::Mpl2
                                                          : core::legacy::NonAssFormat::TmPlayer;
    d.original = roleText(*r, role);
    const std::u16string raw = core::toUtf16(d.original);
    long from = selectionStart, to = selectionEnd;
    if (!m_showTags) {
        const auto projection = core::project(raw);
        from = static_cast<long>(core::rawOffset(projection, static_cast<std::size_t>(selectionStart), true));
        to = selectionEnd == selectionStart
                 ? from
                 : static_cast<long>(core::rawOffset(projection, static_cast<std::size_t>(selectionEnd), false));
    }
    // Several Lines: legacy FindTag searches from position 0.
    d.state = d.several ? core::legacy::EditorText{raw, 0, 0} : core::legacy::EditorText{raw, from, to};
    d.draftUndo = m_draftUndo.size();
    d.hadDraft = s->draftLine().has_value();
    d.opened = core::legacy::EditorText{raw, from, to};
    d.historyCursor = s->historyCursor();
    m_dialog = std::move(d);
    return true;
}

void LineEditorController::publishRawSelection(const core::legacy::EditorText &state)
{
    if (m_showTags) {
        m_selectionStart = static_cast<int>(state.selectionStart);
        m_selectionEnd = static_cast<int>(state.selectionEnd);
    } else {
        const auto shown = core::project(state.text);
        m_selectionStart = static_cast<int>(core::displayOffset(shown, static_cast<std::size_t>(state.selectionStart)));
        m_selectionEnd = static_cast<int>(core::displayOffset(shown, static_cast<std::size_t>(state.selectionEnd)));
    }
    m_selectionRole = m_dialog ? m_dialog->role : 0;
    emit selectionRequested();
}

// TM1: a dialog's change on one Line whose translation has placed runs goes
// to the draft's body, the translator's own text, and the runs' tags are
// written again around the same text (editPlacedBody): a tag put in over a
// placed run is not merged into the run's own block. False when it does not
// apply (nothing placed, or a change to the text itself).
bool LineEditorController::applyDialogToPlacedBody(
    DialogSession &d, const std::function<core::legacy::StepResult(const core::legacy::EditorText &, long)> &one)
{
    const auto tags = runTags();
    const auto real = sessionRecord();
    if (!tags || !real || real->translation.empty())
        return false;
    auto draft = translationDraft(*tags, *real);
    if (!draft.carried || draft.placements.empty())
        return false;
    // The field's raw offsets and the body's meet at the visible text.
    const auto field = core::project(d.state.text);
    const auto before = core::project(draft.body);
    auto toBody = [&](long at, bool afterTags) {
        const auto shown = core::displayOffset(field, static_cast<std::size_t>(std::max(at, 0L)));
        return static_cast<long>(core::rawOffset(before, shown, afterTags));
    };
    std::vector<std::pair<std::size_t, std::size_t>> placed;
    for (const auto &p : draft.placements)
        placed.emplace_back(core::displayOffset(before, p.start), core::displayOffset(before, p.end));
    const core::legacy::EditorText state{draft.body, toBody(d.state.selectionStart, true),
                                         toBody(d.state.selectionEnd, d.state.selectionEnd == d.state.selectionStart)};
    const auto result = one(state, toBody(d.position, false));
    const auto after = core::project(result.state.text);
    if (after.text != before.text)
        return false;
    draft.body = result.state.text;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        draft.placements[i].start = core::rawOffset(after, placed[i].first, true);
        draft.placements[i].end = core::rawOffset(after, placed[i].second, false);
    }
    if (result.state.text != state.text && !saveTranslationDraft(*tags, std::move(draft)))
        return false;
    const auto text = core::toUtf16(record()->translation);
    const auto now = core::project(text);
    auto toField = [&](long at, bool afterTags) {
        const auto shown = core::displayOffset(after, static_cast<std::size_t>(std::max(at, 0L)));
        return static_cast<long>(core::rawOffset(now, shown, afterTags));
    };
    d.state = core::legacy::EditorText{text, toField(result.state.selectionStart, true),
                                       toField(result.state.selectionEnd, false)};
    d.position = toField(result.position, false);
    publishRawSelection(d.state);
    return true;
}

bool LineEditorController::applyDialogChange(
    const std::function<core::legacy::StepResult(const core::legacy::EditorText &, long)> &one,
    const std::function<void(std::u16string &, std::u16string &)> &several)
{
    if (!m_dialog || !editable())
        return false;
    auto &d = *m_dialog;
    auto *s = session();
    if (!d.several) {
        if (d.role == 1 && applyDialogToPlacedBody(d, one))
            return true;
        auto result = one(d.state, d.position);
        if (result.state.text != d.state.text && !setRaw(d.role, core::toUtf8(result.state.text)))
            return false;
        d.state = std::move(result.state);
        d.position = result.position;
        publishRawSelection(d.state);
        return true;
    }
    // Legacy PutTagInText/PutinNonass with several Lines: every selected Line,
    // one "Editing multiple lines" step per change.
    if (s->draftLine() && !s->commitDraft())
        return false;
    std::vector<core::LineId> lines;
    for (const auto *l : s->document().lines())
        if (s->selection().selected.contains(l->id))
            lines.push_back(l->id);
    bool changedAny = false;
    const auto ran = s->run(application::Command{
        "Editing multiple lines", s->revision(), {lines.begin(), lines.end()}, [&](core::Document &doc) {
            for (const auto id : lines)
                if (!doc.editLine(id, [&](core::LineRecord &l) {
                        std::u16string text = core::toUtf16(l.text), translation = core::toUtf16(l.translation);
                        // TM1: a translation with tags carried changes between
                        // them, as the Translated field shows it.
                        const auto c = translation.empty() ? std::nullopt : carry(l);
                        const std::size_t lead = c ? c->tags.leading.size() : 0;
                        const std::size_t trail = c ? c->tags.trailing.size() : 0;
                        auto draft = c ? translationDraft(c->tags, l) : core::TranslationDraft{};
                        if (c && draft.carried && !draft.placements.empty()) {
                            // Placed runs stay placed: the change goes to the
                            // draft's body and the runs' tags are written
                            // again around the same text (editPlacedBody).
                            const auto before = core::project(draft.body);
                            std::vector<std::pair<std::size_t, std::size_t>> placed;
                            for (const auto &p : draft.placements)
                                placed.emplace_back(core::displayOffset(before, p.start),
                                                    core::displayOffset(before, p.end));
                            several(text, draft.body);
                            const auto after = core::project(draft.body);
                            if (after.text != before.text)
                                draft.placements.clear();
                            for (std::size_t i = 0; i < draft.placements.size(); ++i) {
                                draft.placements[i].start = core::rawOffset(after, placed[i].first, true);
                                draft.placements[i].end = core::rawOffset(after, placed[i].second, false);
                            }
                            composedBody(c->tags, draft, l.id);
                            translation = core::composeTranslation(c->tags, draft);
                        } else if (c && translation.size() > lead + trail) {
                            auto body = translation.substr(lead, translation.size() - lead - trail);
                            several(text, body);
                            translation = c->tags.leading + body + c->tags.trailing;
                        } else {
                            several(text, translation);
                        }
                        const auto t8 = core::toUtf8(text), tr8 = core::toUtf8(translation);
                        changedAny = changedAny || t8 != l.text || tr8 != l.translation;
                        l.text = t8;
                        l.translation = tr8;
                    }))
                    return false;
            return changedAny;
        }});
    if (!ran)
        return !changedAny;
    reloadFromSession();
    if (m_onCommitted)
        m_onCommitted();
    return true;
}

namespace {

QVariantMap fontMap(const core::legacy::FontValues &f)
{
    return {{QStringLiteral("name"), QString::fromStdU16String(f.name)},
            {QStringLiteral("size"), QString::fromStdU16String(f.size)},
            {QStringLiteral("bold"), f.bold},
            {QStringLiteral("italic"), f.italic},
            {QStringLiteral("underline"), f.underline},
            {QStringLiteral("strikeOut"), f.strikeOut}};
}

core::legacy::FontValues fontOf(const QVariantMap &m)
{
    return {m.value(QStringLiteral("name")).toString().toStdU16String(),
            m.value(QStringLiteral("size")).toString().toStdU16String(),
            m.value(QStringLiteral("bold")).toBool(),
            m.value(QStringLiteral("italic")).toBool(),
            m.value(QStringLiteral("underline")).toBool(),
            m.value(QStringLiteral("strikeOut")).toBool()};
}

QVariantMap colourMap(const core::legacy::TagColour &c)
{
    return {{QStringLiteral("r"), c.r}, {QStringLiteral("g"), c.g}, {QStringLiteral("b"), c.b}, {QStringLiteral("a"), c.a}};
}

core::legacy::TagColour colourOf(const QVariantMap &m)
{
    return {m.value(QStringLiteral("r")).toInt(), m.value(QStringLiteral("g")).toInt(), m.value(QStringLiteral("b")).toInt(),
            m.value(QStringLiteral("a")).toInt()};
}

core::legacy::TagColour tagColour(const core::Colour &c)
{
    return {static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b), static_cast<int>(c.a)};
}

} // namespace

QVariantMap LineEditorController::beginFont(int role, int selectionStart, int selectionEnd)
{
    if (!beginDialog(role, selectionStart, selectionEnd))
        return {};
    auto &d = *m_dialog;
    core::legacy::FontValues font = core::legacy::defaultFontValues();
    if (d.ass) {
        // Legacy GetStyle(0, name) falls back to the default Style.
        if (const auto style = lineStyle())
            font = {core::toUtf16(style->fontname), core::toUtf16(style->fontsize), style->bold, style->italic,
                    style->underline, style->strikeOut};
        font = core::legacy::fontInEffect(d.state, font, &d.position);
    }
    d.actualFont = d.editedFont = font;
    return fontMap(font);
}

bool LineEditorController::changeFont(const QVariantMap &font)
{
    if (!m_dialog)
        return false;
    auto &d = *m_dialog;
    const auto result = fontOf(font);
    const auto steps = core::legacy::fontSteps(d.editedFont, result, d.actualFont, d.ass);
    d.editedFont = result;
    if (steps.empty())
        return true;
    return applyDialogChange(
        [&](const core::legacy::EditorText &state, long position) {
            return core::legacy::applySteps(state, steps, d.format, position);
        },
        [&](std::u16string &text, std::u16string &translation) {
            core::legacy::applyStepsToLine(text, translation, steps, d.format);
        });
}

QVariantMap LineEditorController::beginColour(int number, int role, int selectionStart, int selectionEnd)
{
    if (number < 1 || number > 4 || !beginDialog(role, selectionStart, selectionEnd))
        return {};
    m_dialog->number = number;
    return switchColour(number);
}

QVariantMap LineEditorController::switchColour(int number)
{
    if (!m_dialog || number < 1 || number > 4)
        return {};
    auto &d = *m_dialog;
    d.number = number;
    if (d.ass) {
        core::legacy::TagColour style{255, 255, 255, 0};
        if (const auto s = lineStyle())
            style = tagColour(number == 1 ? s->primary : number == 2 ? s->secondary : number == 3 ? s->outline : s->back);
        m_lastColour = core::legacy::colourInEffect(d.state, number, style, &d.position);
    }
    // The line formats keep the last colour (legacy actualColor is not read).
    d.actualColour = d.editedColour = m_lastColour;
    return colourMap(d.actualColour);
}

QVariantMap LineEditorController::simplePickerColour(int number)
{
    if (!m_dialog || number < 1 || number > 4)
        return {};
    // Y7-simple-picker-type (approved departure from EditBox.cpp:908-912,
    // which read the colour into a fresh AssColor and left actualColor as
    // it was): ASS formats take that colour in effect as the reset of later
    // changes, as "Choose color" does; the line formats, with one colour,
    // keep the colour the picker shows.
    if (m_dialog->ass)
        return switchColour(number);
    m_dialog->number = number;
    return colourMap(m_dialog->editedColour);
}

bool LineEditorController::changeColour(const QVariantMap &colour)
{
    if (!m_dialog)
        return false;
    auto &d = *m_dialog;
    const auto chosen = colourOf(colour);
    d.editedColour = chosen;
    if (!d.ass) {
        const std::vector<core::legacy::EditStep> steps{core::legacy::colourNonAssStep(chosen)};
        return applyDialogChange(
            [&](const core::legacy::EditorText &state, long position) {
                return core::legacy::applySteps(state, steps, d.format, position);
            },
            [&](std::u16string &text, std::u16string &translation) {
                core::legacy::applyStepsToLine(text, translation, steps, d.format);
            });
    }
    return applyDialogChange(
        [&](const core::legacy::EditorText &state, long) {
            return core::legacy::changeColour(state, d.number, d.actualColour, chosen);
        },
        [&](std::u16string &text, std::u16string &translation) {
            std::u16string &field = translation.empty() ? text : translation;
            field = core::legacy::changeColourInLine(std::move(field), d.number, d.actualColour, chosen);
        });
}

void LineEditorController::endDialog(bool accepted)
{
    if (!m_dialog)
        return;
    DialogSession d = std::move(*m_dialog);
    m_dialog.reset();
    auto *s = session();
    if (!s)
        return;
    if (!accepted) {
        // Legacy DummyUndo: every change of the dialog is taken back.
        if (d.several) {
            if (s->historyCursor() != d.historyCursor)
                s->goTo(d.historyCursor);
            reloadFromSession();
            return;
        }
        if (d.hadDraft) {
            setRaw(d.role, d.original);
            if (m_draftUndo.size() > d.draftUndo)
                m_draftUndo.resize(d.draftUndo);
            refresh();
        } else if (s->draftLine()) {
            s->discardDraft();
            m_draftUndo.resize(std::min(m_draftUndo.size(), d.draftUndo));
            reloadFromSession();
        }
        m_dialog = std::move(d);
        publishRawSelection(m_dialog->opened);
        m_dialog.reset();
        return;
    }
    if (d.several)
        return;
    const long caret = core::legacy::caretAfterDialog(d.state.text, d.position);
    d.state.selectionStart = d.state.selectionEnd = caret;
    m_dialog = std::move(d);
    publishRawSelection(m_dialog->state);
    m_dialog.reset();
}

bool LineEditorController::splitLine(int role, int selectionStart, int selectionEnd)
{
    const auto r = record();
    if (!editable() || !r)
        return false;
    // "\N" for ASS and SRT, "|" for the line-based formats.
    const auto format = session()->document().format();
    const std::u16string split = format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::Srt
                                     ? std::u16string(u"\\N")
                                     : std::u16string(u"|");
    std::u16string raw = core::toUtf16(roleText(*r, role));
    long start = selectionStart, end = selectionEnd;
    if (!m_showTags) {
        const auto p = core::project(raw);
        start = static_cast<long>(core::rawOffset(p, static_cast<std::size_t>(selectionStart), true));
        end = selectionEnd == selectionStart
                  ? start
                  : static_cast<long>(core::rawOffset(p, static_cast<std::size_t>(selectionEnd), false));
    }
    if (start > 0 && raw[static_cast<std::size_t>(start - 1)] == u' ')
        --start;
    if (end < static_cast<long>(raw.size()) && raw[static_cast<std::size_t>(end)] == u' ')
        ++end;
    if (start != end)
        raw.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    raw.insert(static_cast<std::size_t>(start), split);
    const long caret = start + static_cast<long>(split.size());
    if (!setRaw(role, core::toUtf8(raw)))
        return false;
    const auto after = core::project(raw);
    m_selectionStart = m_selectionEnd =
        m_showTags ? static_cast<int>(caret) : static_cast<int>(core::displayOffset(after, static_cast<std::size_t>(caret)));
    m_selectionRole = role;
    emit selectionRequested();
    return true;
}

bool LineEditorController::toggleUnconfirmedAndAdvance()
{
    auto *s = session();
    const auto r = record();
    if (!editable() || !r)
        return false;
    if (!translationMode()) {
        fail(QCoreApplication::translate("HikariSub.Editor", "Unconfirmed applies in translation mode only."));
        return false;
    }
    // E5: EditBox::OnDoubtfulTl flips every selected Line (ChangeState(4)).
    const auto result = application::toggleUnconfirmed(*s);
    if (!result) {
        fail(problemText());
        return false;
    }
    committed();
    return commitAndAdvance();
}

bool LineEditorController::findNext(std::size_t &cursor, const std::function<bool(const core::LineRecord &)> &match,
                                    const QString &none)
{
    auto *s = session();
    if (!s)
        return false;
    if (!translationMode()) {
        fail(QCoreApplication::translate("HikariSub.Editor", "This search applies in translation mode only."));
        return false;
    }
    const auto lines = s->document().lines();
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = cursor; i < lines.size(); ++i) {
            if (lines[i]->visibility == core::LineVisibility::Hidden)
                continue;
            if (match(*lines[i])) {
                cursor = i + 1;
                if (!showLine(lines[i]->id.value))
                    return false;
                emit lineChanged(lines[i]->id.value);
                return true;
            }
        }
        if (cursor == 0)
            break;
        cursor = 0; // wrap once, as the legacy goto does
    }
    fail(none);
    return false;
}

bool LineEditorController::findNextUnconfirmed()
{
    return findNext(m_nextUnconfirmed, [](const core::LineRecord &l) { return l.unconfirmed; },
                    QCoreApplication::translate("HikariSub.Editor", "No unconfirmed lines found"));
}

bool LineEditorController::findNextUntranslated()
{
    return findNext(m_nextUntranslated, [](const core::LineRecord &l) { return l.translation.empty(); },
                    QCoreApplication::translate("HikariSub.Editor", "No untranslated lines found"));
}

void LineEditorController::writeFinished()
{
    refresh();
}

void LineEditorController::committed()
{
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_carryEvaluated.reset(); // DoUndo and the commands show the Line again (SetLine)
    if (m_onCommitted)
        m_onCommitted();
    refresh();
}

bool LineEditorController::pasteAllToTranslation()
{
    const auto r = record();
    if (!translationMode() || !editable() || !r)
        return false;
    // TM1: with tags carried, the Translated field takes the Original's text
    // with its runs placed; the carried tags are added as for typing.
    if (const auto c = carry(*sessionRecord()))
        return saveTranslationDraft(c->tags, core::draftOfOriginal(core::toUtf16(r->text), c->tags));
    return setRaw(1, r->text);
}

bool LineEditorController::pasteSelectionToTranslation(int originalStart, int originalEnd, int translationCaret)
{
    if (!translationMode() || !editable() || originalStart == originalEnd)
        return false;
    const QString original = m_shown[0];
    const int from = std::clamp(std::min(originalStart, originalEnd), 0, static_cast<int>(original.size()));
    const int to = std::clamp(std::max(originalStart, originalEnd), 0, static_cast<int>(original.size()));
    const QString piece = original.mid(from, to - from);
    const int caret = std::clamp(translationCaret, 0, static_cast<int>(m_shown[1].size()));
    QString translated = m_shown[1];
    translated.insert(caret, piece);
    edit(1, translated, caret + static_cast<int>(piece.size()));
    if (m_shown[1] != translated)
        return false; // the mapped edit was refused
    m_selectionStart = m_selectionEnd = caret + static_cast<int>(piece.size());
    m_selectionRole = 1;
    emit selectionRequested();
    return true;
}

bool LineEditorController::commentOutOriginal()
{
    const auto r = record();
    if (!translationMode() || !editable() || !r)
        return false;
    return setRaw(0, u8"{" + r->text + u8"}");
}

bool LineEditorController::insertTimeDifference(bool fromEnd, int selectionStart, int selectionEnd)
{
    const auto r = record();
    const auto video = m_videoTime ? m_videoTime() : std::nullopt;
    if (!editable() || !r || !video)
        return false;
    const std::int64_t start = r->start.value.microseconds() / 1000, end = r->end.value.microseconds() / 1000;
    if (*video < start || *video > end)
        return false; // legacy rings the bell
    const auto zeroIt = [](std::int64_t ms) { return ms / 10 * 10; };
    const std::int64_t diff = fromEnd ? std::abs(*video - zeroIt(end)) : *video - zeroIt(start);
    const int role = translationMode() ? 1 : 0;
    const QString number = QString::number(diff);
    const int from = std::clamp(std::min(selectionStart, selectionEnd), 0, static_cast<int>(m_shown[role].size()));
    const int to = std::clamp(std::max(selectionStart, selectionEnd), 0, static_cast<int>(m_shown[role].size()));
    QString text = m_shown[role];
    text.replace(from, to - from, number);
    edit(role, text, from + static_cast<int>(number.size()));
    if (m_shown[role] != text)
        return false;
    m_selectionStart = m_selectionEnd = from + static_cast<int>(number.size());
    m_selectionRole = role;
    emit selectionRequested();
    return true;
}

QStringList LineEditorController::history() const
{
    QStringList out;
    auto *s = session();
    if (!s)
        return out;
    for (const auto &step : s->history()) {
        const QString name = QString::fromStdString(step.name);
        out << (step.activeRow ? QCoreApplication::translate("HikariSub.Editor", "%1, active line %2")
                                     .arg(name).arg(step.activeRow) : name);
    }
    return out;
}

int LineEditorController::historyCursor() const
{
    auto *s = session();
    return s ? static_cast<int>(s->historyCursor()) : -1;
}

bool LineEditorController::canUndoToLastSave() const
{
    auto *s = session();
    const auto saved = s ? s->savedStep() : std::nullopt;
    return saved && *saved != s->historyCursor();
}

bool LineEditorController::goToHistory(int step)
{
    auto *s = session();
    if (!s || step < 0)
        return false;
    const auto revision = s->revision();
    if (!s->goTo(static_cast<std::size_t>(step))) {
        fail(problemText());
        return false;
    }
    if (s->revision() != revision)
        committed();
    if (s->selection().active)
        emit lineChanged(s->selection().active->value);
    return true;
}

bool LineEditorController::undoToLastSave()
{
    auto *s = session();
    const auto saved = s ? s->savedStep() : std::nullopt;
    return saved && goToHistory(static_cast<int>(*saved));
}

void LineEditorController::reportFieldSelection(int role, int start, int end)
{
    if (role == 0 || role == 1)
        m_fieldSelection[role] = {std::min(start, end), std::max(start, end)};
}

void LineEditorController::selectInField(int start, int end, int role)
{
    if (role != 0 && role != 1)
        role = translationMode() ? 1 : 0;
    const int size = static_cast<int>(m_shown[role].size());
    m_selectionStart = std::clamp(start, 0, size);
    m_selectionEnd = std::clamp(end, 0, size);
    m_selectionRole = role;
    m_fieldSelection[role] = {std::min(m_selectionStart, m_selectionEnd), std::max(m_selectionStart, m_selectionEnd)};
    emit selectionRequested();
}

std::pair<int, int> LineEditorController::displaySpan(int role, int start, int end) const
{
    const auto r = record();
    if (!r || role < 0 || role > 1)
        return {start, end};
    const std::u16string raw = core::toUtf16(roleText(*r, role));
    const int size = static_cast<int>(raw.size());
    if (role == 1) { // TM1: offsets in the whole translation
        const int lead = static_cast<int>(carriedLead());
        start -= lead;
        end -= lead;
    }
    start = std::clamp(start, 0, size);
    end = std::clamp(end, 0, size);
    if (!m_showTags) {
        const auto projection = core::project(raw);
        start = static_cast<int>(core::displayOffset(projection, static_cast<std::size_t>(start)));
        end = static_cast<int>(core::displayOffset(projection, static_cast<std::size_t>(end)));
    }
    return {start, end};
}

std::pair<long, long> LineEditorController::rawFieldSelection(int role) const
{
    role = role == 1 ? 1 : 0;
    const auto [start, end] = m_fieldSelection[role];
    long from = start, to = end;
    if (!m_showTags) {
        if (const auto r = record()) {
            const auto projection = core::project(core::toUtf16(roleText(*r, role)));
            from = static_cast<long>(core::rawOffset(projection, static_cast<std::size_t>(start), true));
            to = end == start ? from : static_cast<long>(core::rawOffset(projection, static_cast<std::size_t>(end), false));
        }
    }
    if (role == 1) { // TM1: offsets in the whole translation
        const long lead = static_cast<long>(carriedLead());
        from += lead;
        to += lead;
    }
    return {from, to};
}

void LineEditorController::selectRaw(int role, int start, int end)
{
    if (!record() || role < 0 || role > 1)
        return;
    std::tie(start, end) = displaySpan(role, start, end);
    m_selectionStart = start;
    m_selectionEnd = end;
    m_selectionRole = role;
    m_fieldSelection[role] = {std::min(start, end), std::max(start, end)};
    emit selectionRequested();
}

void LineEditorController::reloadFromSession()
{
    m_draftUndo.clear();
    m_draftRedo.clear();
    m_carryEvaluated.reset();
    refresh();
}

// E4: the metadata fields.

std::optional<core::LineRecord> LineEditorController::committedRecord() const
{
    auto *s = session();
    if (!s || !s->selection().active)
        return std::nullopt;
    for (const auto *line : s->document().lines())
        if (line->id == *s->selection().active)
            return *line;
    return std::nullopt;
}

bool LineEditorController::assFields() const
{
    // EditBox::HideControls (EditBox.cpp:1420-1454): subsFormat < SRT.
    auto *s = session();
    if (!s)
        return false;
    const auto format = s->document().format();
    return format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText;
}

bool LineEditorController::hasEnd() const
{
    auto *s = session();
    return s && s->document().format() != core::SubtitleFormat::TMPlayer;
}

int LineEditorController::timeForm() const
{
    auto *s = session();
    return static_cast<int>(s ? application::timeFieldForm(s->document().format(), frameTimebase())
                              : application::TimeFieldForm::Ass);
}

int LineEditorController::timeCtrlForm() const
{
    auto *s = session();
    return static_cast<int>(s ? application::timeCtrlForm(s->document().format()) : application::TimeCtrlForm::Ass);
}

bool LineEditorController::comment() const
{
    const auto r = record();
    return r && r->comment;
}

QString LineEditorController::layerText() const
{
    if (m_steppedNumber[0])
        return *m_steppedNumber[0];
    const auto r = record();
    // NumCtrl::SetInt clamps to the range (EditBox.cpp:256).
    return r ? QString::number(std::clamp<std::int64_t>(r->layer.value, -10000000, 10000000)) : QString();
}

QString LineEditorController::durationText() const
{
    const auto r = record();
    if (!r)
        return {};
    if (m_steppedTime[0])
        return *m_steppedTime[0];
    if (m_typedDuration)
        return *m_typedDuration;
    const auto format = session()->document().format();
    if (m_durationEdited)
        return qs(application::editedDurationText(*r, format, frameTimebase()));
    // Without live editing the Line copy keeps its committed times until SetLine.
    const auto shown = options().liveEditing ? r : committedRecord();
    return shown ? qs(application::editorTimeTexts(*shown, format, frameTimebase()).duration) : QString();
}

QString LineEditorController::style() const
{
    const auto r = record();
    return r ? qs(r->style) : QString();
}

QStringList LineEditorController::styleNames() const
{
    // EditBox::RefreshStyle: the Document's Styles sorted by the locale's
    // collation (HikariChoice::Sort).
    auto *s = session();
    if (!s)
        return {};
    QStringList names;
    for (const auto &style : core::decodeStyles(s->document()))
        names.push_back(qs(style.name));
    const QCollator collator;
    std::stable_sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    return names;
}

int LineEditorController::styleIndex() const
{
    const auto r = record();
    if (!r || r->style.empty())
        return -1; // HikariChoice::FindString: none for an empty name
    return static_cast<int>(styleNames().indexOf(qs(r->style))); // case-sensitive
}

QString LineEditorController::actor() const
{
    const auto r = record();
    return r ? qs(r->actor) : QString();
}

QString LineEditorController::effect() const
{
    const auto r = record();
    return r ? qs(r->effect) : QString();
}

void LineEditorController::rebuildLists()
{
    // EditBox::RebuildActorEffectLists (EditBox.cpp:2091-2106): every
    // non-empty value once, sorted by the locale's collation.
    m_actors.clear();
    m_effects.clear();
    auto *s = session();
    if (s)
        for (const auto *line : s->document().lines()) {
            const QString a = qs(line->actor), e = qs(line->effect);
            if (!a.isEmpty() && !m_actors.contains(a))
                m_actors.push_back(a);
            if (!e.isEmpty() && !m_effects.contains(e))
                m_effects.push_back(e);
        }
    const QCollator collator;
    const auto less = [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; };
    std::stable_sort(m_actors.begin(), m_actors.end(), less);
    std::stable_sort(m_effects.begin(), m_effects.end(), less);
}

void LineEditorController::lineShown()
{
    const auto r = committedRecord();
    m_shownLine = r ? std::optional(r->id) : std::nullopt;
    m_durationEdited = false;
    clearTimeWarnings(); // SetLine, EditBox.cpp:394-402
    m_typedDuration.reset();
    dropStepped(); // SetLine's SetTime and SetInt replace the fields' text
    // TextEdit->SetState(...) (EditBox.cpp:430-432)
    m_textState = r ? core::syntax::textState(r->comment, core::toUtf16(r->effect)) : core::syntax::TextState::Dialogue;
    if (!r)
        return;
    m_alignment = shownAlignment(*r, core::decodeStyles(session()->document()));
}

bool LineEditorController::alignmentMixed() const
{
    const auto *s = session();
    if (!s || s->selection().selected.size() < 2 || !hasLine())
        return false;
    if (m_mixedAlignment && m_mixedAlignment->session == s && m_mixedAlignment->revision == s->revision()
        && m_mixedAlignment->shownAlignment == m_alignment && m_mixedAlignment->selected == s->selection().selected)
        return m_mixedAlignment->mixed;
    // The shown Line as the editor holds it (its draft), the others as committed.
    const auto styles = core::decodeStyles(s->document());
    const auto shown = record();
    std::optional<int> first;
    bool mixed = false;
    for (const auto *l : s->document().lines()) {
        if (!s->selection().selected.contains(l->id))
            continue;
        const int a = shown && shown->id == l->id ? m_alignment : shownAlignment(*l, styles);
        if (!first)
            first = a;
        else if (a != *first) {
            mixed = true;
            break;
        }
    }
    m_mixedAlignment = MixedAlignment{s, s->revision(), m_alignment, s->selection().selected, mixed};
    return mixed;
}

QString LineEditorController::charsText() const
{
    const auto r = record();
    if (!r)
        return QCoreApplication::translate("HikariSub.Editor", "Wraps: 0/86"); // the label before any Line (EditBox.cpp:219)
    if (r->comment)
        return {};
    // EditBox::UpdateChars (EditBox.cpp:497-525): the edited field's
    // TextData (GetEditor: the translation, or the original while it is empty).
    const bool translated = translationMode() && !r->translation.empty();
    const auto o = options();
    const auto m = measureLine(qs(translated ? r->translation : r->text), session()->document().format(),
                               MeasureOptions{o.allCharsForCps, o.allCharsForWraps});
    QString wraps = m.wraps + QLatin1Char('/');
    if (wraps.size() >= 50) {
        const QString head = wraps.left(50);
        const qsizetype slash = head.lastIndexOf(QLatin1Char('/'));
        wraps = (slash < 0 ? QString() : head.left(slash)) + QStringLiteral("/.../");
    }
    return QCoreApplication::translate("HikariSub.Editor", "Wraps: ") + wraps + QStringLiteral("43");
}

bool LineEditorController::charsWarning() const
{
    const auto r = record();
    if (!r || r->comment)
        return false;
    const bool translated = translationMode() && !r->translation.empty();
    const auto o = options();
    return measureLine(qs(translated ? r->translation : r->text), session()->document().format(),
                       MeasureOptions{o.allCharsForCps, o.allCharsForWraps})
        .badWraps;
}

namespace {

int editorCps(const core::LineRecord &text, const core::LineRecord &times, bool translationMode,
              core::SubtitleFormat format, const LineEditorController::Options &o)
{
    const bool translated = translationMode && !text.translation.empty();
    const auto m = measureLine(qs(translated ? text.translation : text.text), format,
                               MeasureOptions{o.allCharsForCps, o.allCharsForWraps});
    return legacyCps(m.chars, times.start.value.microseconds() / 1000, times.end.value.microseconds() / 1000);
}

} // namespace

QString LineEditorController::cpsText() const
{
    const auto r = record();
    if (!r)
        return QCoreApplication::translate("HikariSub.Editor", "Characters per second: %1<=15").arg(0);
    if (r->comment)
        return {};
    // TextData::GetCPS over the Line copy's times: the draft's with live
    // editing (OnEdit), the committed ones without.
    const auto o = options();
    const auto times = o.liveEditing ? r : committedRecord();
    return QCoreApplication::translate("HikariSub.Editor", "Characters per second: %1<=15")
        .arg(editorCps(*r, times ? *times : *r, translationMode(), session()->document().format(), o));
}

bool LineEditorController::cpsWarning() const
{
    const auto r = record();
    if (!r || r->comment)
        return false;
    const auto o = options();
    const auto times = o.liveEditing ? r : committedRecord();
    return editorCps(*r, times ? *times : *r, translationMode(), session()->document().format(), o) > 15;
}

void LineEditorController::setVideoTimebase(std::optional<application::LegacyTimebase> timebase)
{
    m_timebase = std::move(timebase);
    // Notebook::LoadVideo enables Times/Frames; nothing disables them again.
    if (m_timebase)
        m_framesAvailable = true;
    emit frameDisplayChanged();
    refresh();
}

void LineEditorController::setShowFramesSetting(bool on, std::function<void(bool)> store)
{
    m_showFrames = on;
    m_storeShowFrames = std::move(store);
    emit frameDisplayChanged();
    refresh();
}

const application::LegacyTimebase *LineEditorController::frameTimebase() const
{
    return m_showFrames && m_timebase && m_timebase->exact() ? &*m_timebase : nullptr;
}

void LineEditorController::setShowFrames(bool on)
{
    if (on == m_showFrames)
        return;
    // EditBox::OnChangeTimeDisplay (EditBox.cpp:1885-1907): the Line is read
    // again from the Grid, so time field edits not yet sent are dropped; the
    // switch is saved.
    m_showFrames = on;
    if (auto *s = session(); s && s->draftLine()) {
        auto change = s->draftChange()->second;
        const auto line = s->draftLine();
        change.start.reset();
        change.end.reset();
        change.startFrame.reset();
        change.endFrame.reset();
        s->discardDraft();
        if (change.text || change.translation || change.marginLeft || change.marginRight || change.marginVertical ||
            change.comment || change.layer || change.style || change.actor || change.effect)
            s->editDraft(*line, change);
    }
    m_durationEdited = false;
    m_typedDuration.reset();
    m_steppedTime[0].reset(); // OnChangeTimeDisplay's SetTime on the three
    m_steppedTime[1].reset();
    m_steppedTime[2].reset();
    if (m_storeShowFrames)
        m_storeShowFrames(on);
    emit frameDisplayChanged();
    refresh();
}

std::optional<core::Document> LineEditorController::draftDocument() const
{
    auto *s = session();
    const auto draft = s ? s->draftRecord() : std::nullopt;
    if (!draft)
        return std::nullopt;
    core::Document document = s->document();
    document.editLine(draft->id, [&](core::LineRecord &line) {
        line.text = draft->text;
        line.translation = draft->translation;
        line.start.value = draft->start.value;
        line.end.value = draft->end.value;
        line.marginLeft.value = draft->marginLeft.value;
        line.marginRight.value = draft->marginRight.value;
        line.marginVertical.value = draft->marginVertical.value;
        line.comment = draft->comment;
        line.layer.value = draft->layer.value;
        line.style = draft->style;
        line.actor = draft->actor;
        line.effect = draft->effect;
        line.startFrame = draft->startFrame;
        line.endFrame = draft->endFrame;
    });
    return document;
}

bool LineEditorController::setComment(bool on)
{
    const auto r = record();
    if (!editable() || !r || !assFields())
        return false;
    session()->editDraft(r->id, application::DraftChange{.comment = on});
    m_attempted.clear();
    const bool sent = sendDraft(false, visualToolOn()); // OnCommit
    // OnCommit's TextEdit->SetState(..., true) for the Comment box
    // (EditBox.cpp:974-977): the edited Line's Comment and Effect.
    updateTextState();
    refresh();
    return sent;
}

void LineEditorController::updateTextState()
{
    const auto r = record();
    m_textState = r ? core::syntax::textState(r->comment, core::toUtf16(r->effect)) : core::syntax::TextState::Dialogue;
}

QVariantList LineEditorController::colourRuns(int role, const QString &shown) const
{
    QVariantList out;
    if (role != 0 && role != 1)
        return out;
    const auto state = role == (translationMode() ? 1 : 0) ? m_textState : core::syntax::TextState::Dialogue;
    const auto runs = core::syntax::colourRuns(
        std::u16string_view(reinterpret_cast<const char16_t *>(shown.utf16()), static_cast<std::size_t>(shown.size())),
        state);
    for (const auto &run : runs)
        out << static_cast<int>(run.start) << static_cast<int>(run.end) << static_cast<int>(run.role);
    return out;
}

bool LineEditorController::chooseStyle(const QString &name)
{
    const auto r = record();
    if (!editable() || !r || !assFields() || name.isEmpty())
        return false;
    application::DraftChange change{.style = u8(name)};
    // TM1: the runs are read against the Style: the placed ones are written
    // again for the new one (core::matchRuns; one it no longer has is dropped).
    if (const auto real = sessionRecord(); real && !real->translation.empty() && carry(*real)) {
        auto after = *real;
        after.style = *change.style;
        after.translation.clear();
        if (const auto next = carry(after)) {
            change.translation = recarried(*real, *next);
            change.translationCarried = true;
        }
    }
    session()->editDraft(r->id, change);
    m_attempted.clear();
    const bool sent = sendDraft(false, visualToolOn()); // OnCommit
    refresh();
    return sent;
}

void LineEditorController::setActorText(const QString &text)
{
    const auto r = record();
    if (!editable() || !r || !assFields() || qs(r->actor) == text)
        return;
    session()->editDraft(r->id, application::DraftChange{.actor = u8(text)});
    refresh();
}

void LineEditorController::setEffectText(const QString &text)
{
    const auto r = record();
    if (!editable() || !r || !assFields() || qs(r->effect) == text)
        return;
    session()->editDraft(r->id, application::DraftChange{.effect = u8(text)});
    refresh();
}

bool LineEditorController::chooseActor(const QString &text)
{
    const auto r = record();
    if (!editable() || !r || !assFields())
        return false;
    session()->editDraft(r->id, application::DraftChange{.actor = u8(text)});
    const bool sent = sendDraft(false, visualToolOn()); // OnCommit
    refresh();
    return sent;
}

bool LineEditorController::chooseEffect(const QString &text)
{
    const auto r = record();
    if (!editable() || !r || !assFields())
        return false;
    session()->editDraft(r->id, application::DraftChange{.effect = u8(text)});
    const bool sent = sendDraft(false, visualToolOn()); // OnCommit
    refresh();
    return sent;
}

void LineEditorController::setLayerText(const QString &text)
{
    const auto r = record();
    if (!editable() || !r || !assFields())
        return;
    m_steppedNumber[0].reset();
    const auto value = application::layerValue(u8(text));
    if (value && *value != r->layer.value)
        session()->editDraft(r->id, application::DraftChange{.layer = *value});
    refresh(); // text NumCtrl cannot read shows the previous value again
}

void LineEditorController::setDurationText(const QString &text)
{
    const auto r = record();
    if (!editable() || !r || !hasEnd())
        return;
    // E4-duration-live-off: the Duration moves End whatever the live-editing
    // option. Legacy moved it only in OnEdit, which runs only with live
    // editing on; without it Send took End from EndEdit (EditBox.cpp:576-580)
    // and the typed Duration changed nothing. Without live editing it applies
    // when the field does (Enter or leaving it), as Start and End do.
    auto *s = session();
    const auto format = s->document().format();
    const auto typed = application::typedTime(u8(text), application::TimeFieldRole::Duration, format,
                                              s->document().frameRate(), frameTimebase());
    if (!typed) {
        fail(frameTimebase() ? QCoreApplication::translate("HikariSub.Editor", "Duration is not a frame number.")
                             : QCoreApplication::translate("HikariSub.Editor", "Duration is not a time."),
             text);
        return;
    }
    // line->End = line->Start + DurEdit->GetTime() (SubsTime::operator+ adds the frames too).
    application::DraftChange change;
    if (typed->timeResolved)
        change.end = core::DocumentTime(r->start.value.microseconds() + typed->ms * 1000);
    if (format == core::SubtitleFormat::MicroDvd && typed->frame)
        change.endFrame = std::optional<std::int64_t>(r->startFrame.value_or(0) + *typed->frame);
    m_attempted.clear();
    m_durationEdited = false;
    m_typedDuration = text; // DurEdit keeps the typed text
    m_steppedTime[0].reset();
    s->editDraft(r->id, change);
    refresh();
}

void LineEditorController::clearTimeWarnings()
{
    // SetForegroundColour(WINDOW_TEXT); changedBackGround stays set.
    m_timeWarning[0] = m_timeWarning[1] = m_timeWarning[2] = false;
}

void LineEditorController::timesFromAudio(std::int64_t startMs, std::int64_t endMs, bool duration)
{
    const auto r = record();
    if (!r)
        return;
    // TimeCtrl::SetTime: `if (mTime == newtime && stillModified) return;`,
    // otherwise SetForegroundColour(WINDOW_WARNING_ELEMENTS) and
    // changedBackGround (TimeCtrl.cpp:233-254).
    const std::int64_t start = r->start.value.microseconds() / 1000, end = r->end.value.microseconds() / 1000;
    bool changedAny = false;
    const auto mark = [&](int field, bool changes) {
        if (!changes)
            return;
        m_timeWarning[field] = m_timeWarned[field] = true;
        changedAny = true;
    };
    // SetTime replaces the text of each field it sets, a step not yet sent included.
    changedAny = m_steppedTime[1] || m_steppedTime[2] || (duration && m_steppedTime[0]);
    m_steppedTime[1].reset();
    m_steppedTime[2].reset();
    if (duration)
        m_steppedTime[0].reset();
    mark(0, startMs != start);
    mark(1, endMs != end);
    // DurEdit: gtime.NewTime(curEndMS - curStartMS), never below 0.
    if (duration && hasEnd())
        mark(2, std::max<std::int64_t>(0, endMs - startMs) != std::max<std::int64_t>(0, end - start));
    if (changedAny)
        emit changed();
}

void LineEditorController::audioSent()
{
    if (!(m_timeWarning[0] || m_timeWarning[1] || m_timeWarning[2]))
        return;
    clearTimeWarnings();
    emit changed();
}

void LineEditorController::timeTyped(int role, const QString &text)
{
    // EditBox::OnEdit, connected only without DISABLE_LIVE_VIDEO_EDITING
    // (EditBox.cpp:343-346, 1520-1560).
    const auto r = record();
    if (!options().liveEditing || !editable() || !r || role < 0 || role > 2)
        return;
    auto *s = session();
    const auto field = static_cast<application::TimeFieldRole>(role);
    m_steppedTime[role].reset(); // the focused field holds what was typed
    if (!application::typedTime(u8(text), field, s->document().format(), s->document().frameRate(), frameTimebase()))
        return; // not yet the field's form: the field's apply reports it
    if (field == application::TimeFieldRole::Duration) {
        // durFocus: End = Start + DurEdit (EditBox.cpp:1546-1550), and
        // EndEdit shows the draft's End again.
        m_steppedTime[2].reset();
        if (hasEnd())
            setDurationText(text);
        return;
    }
    // startEndFocus: the Line takes both times, Duration follows
    // (EditBox.cpp:1526-1545).
    if (!setTime(field, text))
        return;
    liveStartEnd(field == application::TimeFieldRole::Start ? 0 : 1);
}

void LineEditorController::liveStartEnd(int focused)
{
    // OnEdit reads EndEdit and StartEdit as they are, so a field the mouse
    // stepped goes to the draft too; DurEdit->SetTime replaces a stepped
    // Duration.
    for (const auto role : {application::TimeFieldRole::End, application::TimeFieldRole::Start}) {
        auto &stepped = m_steppedTime[static_cast<int>(role)];
        if (!stepped)
            continue;
        const QString text = *std::exchange(stepped, std::nullopt);
        setTime(role, text);
    }
    m_steppedTime[0].reset();
    const auto line = record();
    // SubsTime::operator> compares the milliseconds (SubsTime.cpp:184-187).
    if (line && line->start.value.microseconds() / 1000 > line->end.value.microseconds() / 1000)
        m_timeWarning[focused] = m_timeWarned[focused] = true;
    else if (m_timeWarned[0]) // legacy's else-if: Start first, End only when Start never warned
        m_timeWarning[0] = false;
    else if (m_timeWarned[1])
        m_timeWarning[1] = false;
    emit changed();
}

void LineEditorController::timeStepped(int role, const QString &text, int focused)
{
    if (!editable() || role < 0 || role > 2 || (role != 1 && !hasEnd()))
        return;
    m_steppedTime[role] = text;
    if (options().liveEditing) {
        // EditBox::OnEdit for the focused field (EditBox.cpp:1524-1556).
        if (focused == 1 || focused == 2) {
            liveStartEnd(focused == 1 ? 0 : 1);
            return;
        }
        if (focused == 0)
            m_steppedTime[2].reset(); // EndEdit->SetTime(line->End, false, 2)
    }
    emit changed();
}

void LineEditorController::numberStepped(int role, const QString &text)
{
    if (!editable() || !assFields() || role < 0 || role > 3)
        return;
    m_steppedNumber[role] = text;
    emit changed();
}

bool LineEditorController::timeModified(int role) const
{
    return role >= 0 && role <= 2 && m_steppedTime[role].has_value();
}

bool LineEditorController::numberModified(int role) const
{
    return role >= 0 && role <= 3 && m_steppedNumber[role].has_value();
}

int LineEditorController::steppedTimes() const
{
    int bits = 0;
    for (int role = 0; role < 3; ++role)
        bits |= timeModified(role) ? 1 << role : 0;
    return bits;
}

int LineEditorController::steppedNumbers() const
{
    int bits = 0;
    for (int role = 0; role < 4; ++role)
        bits |= numberModified(role) ? 1 << role : 0;
    return bits;
}

void LineEditorController::dropStepped()
{
    for (auto &t : m_steppedTime)
        t.reset();
    for (auto &n : m_steppedNumber)
        n.reset();
}

// EditBox::Send's modified fields in its order (EditBox.cpp:560-605): the
// Layer, Start, End, the Duration (End = Start + Duration), the margins.
bool LineEditorController::applyStepped()
{
    std::optional<QString> time[3], number[4];
    for (int i = 0; i < 3; ++i)
        time[i] = std::exchange(m_steppedTime[i], std::nullopt);
    for (int i = 0; i < 4; ++i)
        number[i] = std::exchange(m_steppedNumber[i], std::nullopt);
    bool any = false;
    if (number[0]) {
        setLayerText(*number[0]);
        any = true;
    }
    if (time[1]) {
        setTime(application::TimeFieldRole::Start, *time[1]);
        any = true;
    }
    if (time[2]) {
        setTime(application::TimeFieldRole::End, *time[2]);
        any = true;
    }
    if (time[0]) {
        setDurationText(*time[0]);
        any = true;
    }
    for (int i = 1; i < 4; ++i)
        if (number[i]) {
            setMarginText(i - 1, *number[i]);
            any = true;
        }
    return any;
}

bool LineEditorController::chooseAlignment(int index)
{
    const auto r = record();
    if (!editable() || !r || !assFields() || index < 0 || index > 8)
        return false;
    m_alignment = index + 1;
    // FindTag("an([0-9])", "", 1) then PutTagInText("\\an<n>", "", true).
    const std::u16string tag = u"\\an" + std::u16string(1, static_cast<char16_t>(u'1' + index));
    auto *s = session();
    if (s->selection().selected.size() < 2) {
        // GetEditor/PutTagInText: TextEdit (the translation in translation
        // mode), or TextEditOrig while the translation is empty. TM1: with
        // tags carried, \an is the Original's, and the translation follows.
        const bool follows = r->translation.empty() || carry(*sessionRecord());
        const int role = translationMode() && !follows ? 1 : 0;
        const auto [from, to] = m_fieldSelection[role];
        return editRaw(role, from, to, [&](core::legacy::EditorText t) {
            core::legacy::TagEditor editor(std::move(t));
            editor.findTag(u"an([0-9])", 1, false);
            editor.putTagInText(tag, u"");
            return editor.state();
        });
    }
    // Several Lines: each one's text (its translation when it has one) gets
    // the tag in its first block, as one "Editing multiple lines" step
    // (TagFindReplace.cpp:715-736).
    if (s->draftLine() && !s->commitDraft())
        return false;
    std::vector<core::LineId> lines;
    for (const auto *l : s->document().lines())
        if (s->selection().selected.contains(l->id))
            lines.push_back(l->id);
    const auto ran = s->run(application::Command{
        "Editing multiple lines", s->revision(), {lines.begin(), lines.end()}, [&](core::Document &d) {
            for (const auto id : lines)
                if (!d.editLine(id, [&](core::LineRecord &l) {
                        // TM1: with tags carried, \an is the Original's and the
                        // translation follows, as for one Line.
                        if (carry(l)) {
                            auto after = l;
                            after.text = core::toUtf8(core::legacy::putTagInLine(core::toUtf16(l.text), u"an([0-9])", tag));
                            after.translation.clear();
                            if (const auto next = carry(after)) {
                                if (!l.translation.empty())
                                    l.translation = recarried(l, *next);
                                l.text = std::move(after.text);
                                return;
                            }
                        }
                        auto &field = l.translation.empty() ? l.text : l.translation;
                        field = core::toUtf8(core::legacy::putTagInLine(core::toUtf16(field), u"an([0-9])", tag));
                    }))
                    return false;
            return true;
        }});
    if (!ran)
        return false;
    reloadFromSession();
    if (m_onCommitted)
        m_onCommitted();
    return true;
}

bool LineEditorController::commitFromField(bool timeField)
{
    // EditBox::OnNewline (EditBox.cpp:987-1001)
    if (timeField && options().dontAdvanceOnTimes) {
        if (!sendDraft())
            return false;
        refresh();
        return true;
    }
    return commitAndAdvance();
}

void LineEditorController::selectInBox(int role, int start, int end)
{
    if (!record() || (role != 2 && role != 3))
        return;
    const int size = static_cast<int>((role == 2 ? actor() : effect()).size());
    m_selectionStart = std::clamp(start, 0, size);
    m_selectionEnd = std::clamp(end, 0, size);
    m_selectionRole = role;
    emit selectionRequested();
}

} // namespace hikari::ui
