#include "tag_list_controller.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPointer>

#include <algorithm>
#include <array>

namespace hikari::ui {

namespace {

// MUST KILL: kDescriptions is index-parallel to core::taglist::entries()
constexpr std::array<const char *, core::taglist::kEntryCount> kDescriptions{{
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of primary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of secondary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of border colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of shadow colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Primary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Secondary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Border colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Shadow colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of primary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of secondary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of border colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of shadow colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of primary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of secondary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of border colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of shadow colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of primary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of secondary colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of border colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of shadow colour"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text alignment (SSA)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text position"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Bold text"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Edge blur"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Blur of border, shadow or font"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Thickness of border"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Vector clip / rectangle clip"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font distortion"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Fading in / fading out of text"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Fading in / fading out of text (advanced version)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Skew on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Skew on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text encoding"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font name"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text rounding"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Rotation on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Rotation on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Rotation on Z axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font size"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Scale on X and Y axes"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Scale on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Scale on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font spacing"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Vertical font spacing"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Italic text"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Reverse vector clip or rectangle clip"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text jitter"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing smooth transition"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing border"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing (not supported)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement along a circle"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement along a curve (3 points)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement along a curve (4 points)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Vector drawing movement"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Anchor for rotation"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Drawing and its scale"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Offset of Y vector points"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text position"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text wrap method"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Reset tags"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness seed"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness on Z axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text strikethrough"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text shadow"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text animation"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text underline"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Border on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Border on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Shadow on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Shadow on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Z coordinate for tags \\frx and \\fry"),
}};

std::u16string u16(const QString &s)
{
    return s.toStdU16String();
}

struct Expanded {
    QString text;
    int caret = 0;
};

Expanded withDefaultsAndCaretAtFirstStop(const QString &insert)
{
    Expanded out;
    int first = -1, last = -1;
    for (qsizetype i = 0; i < insert.size(); ++i) {
        const QChar c = insert.at(i);
        if (c == u'\\' && i + 1 < insert.size() && (insert.at(i + 1) == u'$' || insert.at(i + 1) == u'}')) {
            out.text += insert.at(++i);
            continue;
        }
        if (c != u'$' || i + 1 >= insert.size()) {
            out.text += c;
            continue;
        }
        qsizetype j = i + 1;
        const bool braced = insert.at(j) == u'{';
        if (braced)
            ++j;
        const qsizetype digits = j;
        while (j < insert.size() && insert.at(j).isDigit())
            ++j;
        if (j == digits) {
            out.text += c;
            continue;
        }
        const int stop = insert.mid(digits, j - digits).toInt();
        QString fallback;
        if (braced) {
            if (j < insert.size() && insert.at(j) == u':') {
                const qsizetype close = insert.indexOf(u'}', j);
                if (close < 0) {
                    out.text += c;
                    continue;
                }
                fallback = insert.mid(j + 1, close - j - 1);
                j = close;
            }
            if (j >= insert.size() || insert.at(j) != u'}') {
                out.text += c;
                continue;
            }
            ++j;
        }
        const int at = static_cast<int>(out.text.size());
        if (stop == 1 && first < 0)
            first = at;
        if (stop == 0 && last < 0)
            last = at;
        out.text += fallback;
        i = j - 1;
    }
    out.caret = first >= 0 ? first : last >= 0 ? last : static_cast<int>(out.text.size());
    return out;
}

}

TagListController::TagListController(QObject *parent) : QObject(parent) {}

void TagListController::setOptionsStore(ReadOptions read, WriteOptions write)
{
    m_read = std::move(read);
    m_write = std::move(write);
}

QString TagListController::description(std::size_t entry)
{
    return entry < kDescriptions.size() ? QCoreApplication::translate("HikariSub.TagList", kDescriptions[entry])
                                        : QString();
}

QString TagListController::rowText(std::size_t entry) const
{
    QString text = QString::fromStdU16String(std::u16string(core::taglist::entries()[entry].tag));
    if (m_options & core::taglist::ShowDescription)
        text += QStringLiteral(" - ") + description(entry);
    return text;
}

void TagListController::setProviders(Triggers triggers, AskProviders ask)
{
    m_triggers = std::move(triggers);
    m_ask = std::move(ask);
}

bool TagListController::tagsShown() const
{
    return m_completion.list() && m_completion.list()->popupShown();
}

int TagListController::tagRows() const
{
    return tagsShown() ? static_cast<int>(m_completion.list()->count()) : 0;
}

QStringList TagListController::rows() const
{
    QStringList out;
    if (const auto *list = m_completion.list(); list && (m_provided.empty() || list->popupShown()))
        for (const auto entry : list->shownEntries())
            out << rowText(entry);
    for (const ProviderRow &row : m_provided)
        out << row.label;
    return out;
}

QStringList TagListController::providerDetails() const
{
    QStringList out;
    for (const ProviderRow &row : m_provided)
        out << (row.detail.isEmpty() ? row.source : row.detail + QStringLiteral(" · ") + row.source);
    return out;
}

int TagListController::selection() const
{
    if (m_providedSelection >= 0)
        return tagRows() + m_providedSelection;
    return m_completion.list() ? m_completion.list()->selection() : -1;
}

void TagListController::setHover(Hovers hovers, AskHover ask)
{
    m_hovers = std::move(hovers);
    m_askHover = std::move(ask);
}

void TagListController::hoverAt(int role, const QString &text, int position)
{
    if (!m_askHover || !hovers(role))
        return clearHover();
    const std::uint64_t asked = ++m_hoverAsked;
    m_askHover(role, text, position, [self = QPointer(this), asked, role, position](std::optional<ProviderHover> hover) {
        if (!self || asked != self->m_hoverAsked)
            return;
        self->m_hover = hover ? QVariantMap{{QStringLiteral("role"), role},
                                            {QStringLiteral("at"), position},
                                            {QStringLiteral("text"), hover->text},
                                            {QStringLiteral("source"), hover->source},
                                            {QStringLiteral("from"), hover->from},
                                            {QStringLiteral("to"), hover->to}}
                              : QVariantMap();
        emit self->hoverChanged();
    });
}

void TagListController::clearHover()
{
    ++m_hoverAsked;
    if (m_hover.isEmpty())
        return;
    m_hover.clear();
    emit hoverChanged();
}

int TagListController::scrollPosition() const
{
    const int tags = tagsShown() ? m_completion.list()->scrollPosition() : 0;
    if (m_providedSelection < 0)
        return m_completion.list() ? m_completion.list()->scrollPosition() : 0;
    const int at = selection();
    return at < tags ? at : at >= tags + core::taglist::kMaxVisible ? at - core::taglist::kMaxVisible + 1 : tags;
}

void TagListController::ask(int role, const QString &text, int caret, const QString &trigger)
{
    const std::uint64_t asked = ++m_asked;
    m_ask(role, text, caret, trigger, [self = QPointer(this), asked](std::vector<ProviderRow> rows) {
        if (!self || asked != self->m_asked)
            return;
        self->m_provided = std::move(rows);
        self->m_providedSelection = self->m_provided.empty() || self->tagsShown() ? -1 : 0;
        emit self->changed();
    });
}

QString TagListController::selectedAnnouncement() const
{
    if (m_providedSelection >= 0) {
        const ProviderRow &row = m_provided[static_cast<std::size_t>(m_providedSelection)];
        return QCoreApplication::translate("HikariSub.TagList", "%1, %2, %3 of %4")
            .arg(row.label, providerDetails().at(m_providedSelection))
            .arg(selection() + 1)
            .arg(tagRows() + static_cast<int>(m_provided.size()));
    }
    const auto *list = m_completion.list();
    if (!list || !list->popupShown())
        return {};
    const auto entry = list->entryAt(list->selection());
    if (!entry)
        return {};
    const QString tag = QStringLiteral("\\") + QString::fromStdU16String(std::u16string(core::taglist::entries()[*entry].tag));
    return QCoreApplication::translate("HikariSub.TagList", "%1, %2, %3 of %4")
        .arg(tag, description(*entry))
        .arg(list->selection() + 1)
        .arg(list->count());
}

bool TagListController::typed(const QString &text, int caret, const QString &key, int role)
{
    if (key.size() != 1)
        return false;
    const bool wasOpen = m_completion.open();
    const int options = m_read ? m_read() : m_options;
    if (!wasOpen)
        m_options = options;
    const bool acted = m_completion.typed(u16(text), static_cast<std::size_t>(std::max(0, caret)), key.at(0).unicode(),
                                          m_options);
    const bool asks = m_ask && ((m_triggers && m_triggers(role).contains(key)) || !m_provided.empty());
    if (asks)
        ask(role, text, caret, m_provided.empty() ? key : QString());
    if (!acted)
        return false;
    if (m_providedSelection >= 0 && tagsShown())
        m_providedSelection = -1;
    emit changed();
    return true;
}

bool TagListController::trigger(const QString &text, int caret, int role)
{
    const int at = std::clamp(caret, 0, static_cast<int>(text.size()));
    int start = at;
    while (start > 0 && !text.at(start - 1).isSpace() && text.at(start - 1) != u'\\' && text.at(start - 1) != u'{'
           && text.at(start - 1) != u'}')
        --start;
    if (!m_completion.open() && start > 0 && text.at(start - 1) == u'\\') {
        if (!m_completion.list())
            m_options = m_read ? m_read() : m_options;
        if (m_completion.typed(u16(text), static_cast<std::size_t>(at), u'\\', m_options) && start < at)
            m_completion.list()->filterByKeyword(u16(text.mid(start, at - start)));
    }
    if (m_ask)
        ask(role, text, at, {});
    emit changed();
    return m_completion.open() || m_ask != nullptr;
}

bool TagListController::move(int delta)
{
    if (m_provided.empty()) {
        if (!m_completion.move(delta))
            return false;
        emit changed();
        return true;
    }
    const int tags = tagRows();
    const int total = tags + static_cast<int>(m_provided.size());
    const int from = selection() < 0 ? (delta > 0 ? -1 : total) : selection();
    const int to = ((from + delta) % total + total) % total;
    if (to < tags) {
        m_providedSelection = -1;
        m_completion.list()->setSelection(to);
    } else {
        m_providedSelection = to - tags;
    }
    emit changed();
    return true;
}

QVariantMap TagListController::put(const QString &text, int caret)
{
    if (m_providedSelection >= 0) {
        const ProviderRow row = m_provided[static_cast<std::size_t>(m_providedSelection)];
        close();
        if (row.to > text.size())
            return {};
        const Expanded inserted = row.snippet ? withDefaultsAndCaretAtFirstStop(row.insert) : Expanded{row.insert, static_cast<int>(row.insert.size())};
        return {{QStringLiteral("text"), text.left(row.from) + inserted.text + text.mid(row.to)},
                {QStringLiteral("caret"), row.from + inserted.caret}};
    }
    const auto put = m_completion.put(u16(text), static_cast<std::size_t>(std::max(0, caret)));
    if (!put)
        return {};
    m_provided.clear();
    ++m_asked;
    emit changed();
    return {{QStringLiteral("text"), QString::fromStdU16String(put->text)},
            {QStringLiteral("caret"), static_cast<int>(put->caret)}};
}

void TagListController::close()
{
    ++m_asked;
    if (!open())
        return;
    m_completion.close();
    m_provided.clear();
    m_providedSelection = -1;
    emit changed();
}

bool TagListController::pointerAt(int row)
{
    if (const int tags = tagRows(); !m_provided.empty() && row >= tags) {
        const int at = std::min(row - tags, static_cast<int>(m_provided.size()) - 1);
        if (at != m_providedSelection) {
            m_providedSelection = at;
            emit changed();
        }
        return true;
    }
    if (m_providedSelection >= 0) {
        m_providedSelection = -1;
        emit changed();
    }
    auto *list = m_completion.list();
    if (!list)
        return false;
    const int before = list->selection();
    const bool acts = list->pointerAt(row);
    if (list->selection() != before)
        emit changed();
    return acts;
}

void TagListController::scrollBy(int rows)
{
    auto *list = m_completion.list();
    if (!list)
        return;
    const int before = list->scrollPosition();
    list->scrollBy(rows);
    if (list->scrollPosition() != before)
        emit changed();
}

void TagListController::wheel(int notches)
{
    auto *list = m_completion.list();
    if (!list)
        return;
    const int before = list->scrollPosition();
    list->wheel(-3 * notches);
    if (list->scrollPosition() != before)
        emit changed();
}

void TagListController::toggleOption(int bit)
{
    auto *list = m_completion.list();
    if (!list || (bit != 1 && bit != 2 && bit != 4))
        return;
    const int options = (m_read ? m_read() : m_options) ^ bit;
    if (m_write)
        m_write(options);
    m_options = options;
    list->filterByOptions(options);
    emit changed();
}

double TagListController::popupWidth(const QFont &font) const
{
    const QFontMetricsF metrics(font);
    double width = 0;
    for (std::size_t entry = 0; entry < core::taglist::kEntryCount; ++entry)
        width = std::max(width, metrics.horizontalAdvance(rowText(entry)));
    width += 18;
    const auto *list = m_completion.list();
    if (list && list->count() > static_cast<std::size_t>(core::taglist::kMaxVisible))
        width += 20;
    for (const ProviderRow &row : m_provided)
        width = std::max(width, metrics.horizontalAdvance(row.label) + 24 +
                                    metrics.horizontalAdvance(row.detail.isEmpty() ? row.source : row.detail + QStringLiteral(" · ") + row.source));
    width = std::min(width, 800.0);
    // MUST KILL: the 100 minimum width applies only from the second popup on
    if (list && list->popupCalls() > 1)
        width = std::max(width, 100.0);
    return width;
}

double TagListController::rowHeight(const QFont &font) const
{
    return QFontMetricsF(font).height() + 6;
}

}
