#include "quick_pick_model.h"

#include <algorithm>
#include <limits>
#include <numeric>

namespace hikari::ui {

namespace {

constexpr int kNone = std::numeric_limits<int>::min() / 2;
constexpr int kLetter = 1;
constexpr int kWordStart = 8;
constexpr int kTextStart = 2;
constexpr int kRun = 10;
constexpr int kNameFirst = 100000;

bool wordStart(const QString &text, int at)
{
    if (at == 0)
        return true;
    const QChar before = text.at(at - 1);
    const QChar here = text.at(at);
    if (!before.isLetterOrNumber())
        return here.isLetterOrNumber();
    return before.isLower() && here.isUpper();
}

QString markup(const QString &text, const std::vector<int> &matched)
{
    if (matched.empty())
        return text.toHtmlEscaped();
    QString out;
    std::size_t next = 0;
    bool bold = false;
    for (int i = 0; i < text.size(); ++i) {
        const bool hit = next < matched.size() && matched[next] == i;
        if (hit)
            ++next;
        if (hit != bold) {
            out += hit ? QStringLiteral("<b>") : QStringLiteral("</b>");
            bold = hit;
        }
        out += QString(text.at(i)).toHtmlEscaped();
    }
    if (bold)
        out += QStringLiteral("</b>");
    return out;
}

QString text(const QVariantMap &item, const char *key)
{
    return item.value(QLatin1String(key)).toString();
}

}

std::optional<FuzzyMatch> fuzzyMatch(const QString &query, const QString &text)
{
    QString q;
    for (const QChar c : query)
        if (!c.isSpace())
            q += c.toCaseFolded();
    const int n = int(q.size());
    const int m = int(text.size());
    if (n == 0)
        return FuzzyMatch{};
    if (n > m)
        return std::nullopt;
    std::vector<QChar> t(static_cast<std::size_t>(m));
    std::vector<int> bonus(static_cast<std::size_t>(m));
    for (int j = 0; j < m; ++j) {
        t[std::size_t(j)] = text.at(j).toCaseFolded();
        bonus[std::size_t(j)] = kLetter + (wordStart(text, j) ? kWordStart : 0) + (j == 0 ? kTextStart : 0);
    }
    std::vector<std::vector<int>> score(static_cast<std::size_t>(n), std::vector<int>(static_cast<std::size_t>(m), kNone));
    std::vector<std::vector<int>> from(static_cast<std::size_t>(n), std::vector<int>(static_cast<std::size_t>(m), -1));
    for (int j = 0; j < m; ++j)
        if (t[std::size_t(j)] == q.at(0))
            score[0][std::size_t(j)] = bonus[std::size_t(j)];
    for (int i = 1; i < n; ++i) {
        int bestBefore = kNone;
        int bestAt = -1;
        const auto &prev = score[std::size_t(i - 1)];
        for (int j = i; j < m; ++j) {
            if (j >= 2 && prev[std::size_t(j - 2)] > bestBefore) {
                bestBefore = prev[std::size_t(j - 2)];
                bestAt = j - 2;
            }
            if (t[std::size_t(j)] != q.at(i))
                continue;
            int best = bestBefore;
            int at = bestAt;
            if (prev[std::size_t(j - 1)] != kNone && prev[std::size_t(j - 1)] + kRun >= best) {
                best = prev[std::size_t(j - 1)] + kRun;
                at = j - 1;
            }
            if (best == kNone)
                continue;
            score[std::size_t(i)][std::size_t(j)] = best + bonus[std::size_t(j)];
            from[std::size_t(i)][std::size_t(j)] = at;
        }
    }
    const auto &last = score[std::size_t(n - 1)];
    const auto end = std::max_element(last.begin(), last.end());
    if (*end == kNone)
        return std::nullopt;
    FuzzyMatch match;
    match.score = *end;
    match.positions.resize(static_cast<std::size_t>(n));
    int j = int(end - last.begin());
    for (int i = n - 1; i >= 0; --i) {
        match.positions[std::size_t(i)] = j;
        j = from[std::size_t(i)][std::size_t(j)];
    }
    return match;
}

QuickPickModel::QuickPickModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QuickPickModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant QuickPickModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
        return {};
    const Row &row = m_rows[std::size_t(index.row())];
    if (row.item < 0) {
        switch (role) {
        case KindRole:
            return QStringLiteral("heading");
        case LabelRole:
        case Qt::DisplayRole:
            return row.heading;
        case LabelMarkupRole:
            return row.heading.toHtmlEscaped();
        case FirstHeadingRole:
            return row.firstHeading;
        default:
            return {};
        }
    }
    const QVariantMap item = m_items.at(row.item).toMap();
    switch (role) {
    case KindRole:
        return QStringLiteral("item");
    case IdRole:
        return text(item, "id");
    case LabelRole:
    case Qt::DisplayRole:
        return text(item, "label");
    case LabelMarkupRole:
        return markup(text(item, "label"), row.labelMatch);
    case DescriptionRole:
        return text(item, "description");
    case DescriptionMarkupRole:
        return markup(text(item, "description"), row.descriptionMatch);
    case DetailRole:
        return text(item, "detail");
    case IconRole:
        return text(item, "icon");
    case ShortcutRole:
        return text(item, "shortcut");
    case TrailingRole:
        return text(item, "trailing");
    case DisabledRole:
        return text(item, "disabled");
    case PickedRole:
        return m_picked.contains(text(item, "id"));
    case TooltipRole:
        return text(item, "tooltip");
    case FirstHeadingRole:
        return false;
    case DataRole:
        return item;
    default:
        return {};
    }
}

QHash<int, QByteArray> QuickPickModel::roleNames() const
{
    return {{KindRole, "kind"},
            {IdRole, "itemId"},
            {LabelRole, "label"},
            {LabelMarkupRole, "labelMarkup"},
            {DescriptionRole, "description"},
            {DescriptionMarkupRole, "descriptionMarkup"},
            {DetailRole, "detail"},
            {IconRole, "icon"},
            {ShortcutRole, "shortcut"},
            {TrailingRole, "trailing"},
            {DisabledRole, "disabledReason"},
            {PickedRole, "picked"},
            {TooltipRole, "tooltip"},
            {FirstHeadingRole, "firstHeading"},
            {DataRole, "item"}};
}

void QuickPickModel::setItems(const QVariantList &items)
{
    m_items = items;
    m_groups.assign(std::size_t(items.size()), QString());
    QString group;
    QSet<QString> picked;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QVariantMap item = items.at(i).toMap();
        if (text(item, "kind") == QLatin1String("separator")) {
            group = text(item, "label");
            continue;
        }
        if (item.contains(QStringLiteral("group")))
            group = text(item, "group");
        m_groups[std::size_t(i)] = group;
        if (item.value(QStringLiteral("picked")).toBool())
            picked.insert(text(item, "id"));
    }
    if (picked != m_picked) {
        m_picked = picked;
        emit pickedChanged();
    }
    emit itemsChanged();
    rebuild();
}

void QuickPickModel::setFilter(const QString &filter)
{
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void QuickPickModel::setMatchOn(const QStringList &fields)
{
    if (fields == m_matchOn)
        return;
    m_matchOn = fields;
    emit optionsChanged();
    rebuild();
}

void QuickPickModel::setKeepOrder(bool keep)
{
    if (keep == m_keepOrder)
        return;
    m_keepOrder = keep;
    emit optionsChanged();
    rebuild();
}

void QuickPickModel::setFiltering(bool filtering)
{
    if (filtering == m_filtering)
        return;
    m_filtering = filtering;
    emit optionsChanged();
    rebuild();
}

void QuickPickModel::setMany(bool many)
{
    if (many == m_many)
        return;
    m_many = many;
    emit optionsChanged();
}

void QuickPickModel::rebuild()
{
    const QString previous = currentId();
    struct Hit {
        int item;
        long long score;
        int rank;
        std::vector<int> label;
        std::vector<int> description;
    };
    std::vector<Hit> hits;
    const bool typed = m_filtering && !m_filter.trimmed().isEmpty();
    for (int i = 0; i < int(m_items.size()); ++i) {
        const QVariantMap item = m_items.at(i).toMap();
        if (text(item, "kind") == QLatin1String("separator"))
            continue;
        Hit hit{i, 0, item.value(QStringLiteral("rank"), std::numeric_limits<int>::max()).toInt(), {}, {}};
        if (typed) {
            if (const auto byLabel = fuzzyMatch(m_filter, text(item, "label"))) {
                hit.score = kNameFirst + byLabel->score;
                hit.label = byLabel->positions;
            } else if (const auto byDescription = m_matchOn.contains(QLatin1String("description"))
                                                      ? fuzzyMatch(m_filter, text(item, "description"))
                                                      : std::nullopt) {
                hit.score = byDescription->score;
                hit.description = byDescription->positions;
            } else if (const auto byDetail = m_matchOn.contains(QLatin1String("detail"))
                                                 ? fuzzyMatch(m_filter, text(item, "detail"))
                                                 : std::nullopt) {
                hit.score = byDetail->score;
            } else if (!item.value(QStringLiteral("alwaysShow")).toBool()) {
                continue;
            }
        }
        hits.push_back(std::move(hit));
    }
    if (typed && !m_keepOrder)
        std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.rank < b.rank;
        });
    const bool headings = !typed || m_keepOrder;
    beginResetModel();
    m_rows.clear();
    QString group;
    for (Hit &hit : hits) {
        const QString &itemGroup = m_groups[std::size_t(hit.item)];
        if (headings && itemGroup != group && !itemGroup.isEmpty())
            m_rows.push_back(Row{-1, itemGroup, m_rows.empty(), {}, {}});
        group = itemGroup;
        m_rows.push_back(Row{hit.item, {}, false, std::move(hit.label), std::move(hit.description)});
    }
    endResetModel();
    const int shown = int(std::count_if(m_rows.begin(), m_rows.end(), [](const Row &r) { return r.item >= 0; }));
    if (shown != m_shown) {
        m_shown = shown;
        emit countChanged();
    }
    m_current = -1;
    if (typed || !setCurrentById(previous))
        first();
    emit currentChanged();
}

bool QuickPickModel::isItem(int row) const
{
    return row >= 0 && row < int(m_rows.size()) && m_rows[std::size_t(row)].item >= 0;
}

void QuickPickModel::setCurrent(int row)
{
    if (!isItem(row))
        row = -1;
    if (row == m_current)
        return;
    m_current = row;
    emit currentChanged();
}

QString QuickPickModel::currentId() const
{
    if (!isItem(m_current))
        return {};
    return text(m_items.at(m_rows[std::size_t(m_current)].item).toMap(), "id");
}

void QuickPickModel::move(int delta)
{
    const int rows = int(m_rows.size());
    if (m_shown == 0 || delta == 0)
        return;
    int row = m_current < 0 ? (delta > 0 ? -1 : rows) : m_current;
    const int step = delta > 0 ? 1 : -1;
    for (int left = std::abs(delta); left > 0;) {
        row = (row + step + rows) % rows;
        if (isItem(row))
            --left;
    }
    setCurrent(row);
}

void QuickPickModel::page(int delta, int rows)
{
    if (m_shown == 0)
        return;
    int row = std::clamp(m_current + delta * std::max(1, rows), 0, int(m_rows.size()) - 1);
    const int step = delta > 0 ? 1 : -1;
    while (!isItem(row) && row + step >= 0 && row + step < int(m_rows.size()))
        row += step;
    while (!isItem(row))
        row -= step;
    setCurrent(row);
}

void QuickPickModel::first()
{
    for (int row = 0; row < int(m_rows.size()); ++row)
        if (isItem(row))
            return setCurrent(row);
    setCurrent(-1);
}

void QuickPickModel::last()
{
    for (int row = int(m_rows.size()) - 1; row >= 0; --row)
        if (isItem(row))
            return setCurrent(row);
    setCurrent(-1);
}

bool QuickPickModel::setCurrentById(const QString &id)
{
    if (id.isEmpty())
        return false;
    for (int row = 0; row < int(m_rows.size()); ++row)
        if (isItem(row) && text(m_items.at(m_rows[std::size_t(row)].item).toMap(), "id") == id) {
            setCurrent(row);
            return true;
        }
    return false;
}

QVariantMap QuickPickModel::itemAt(int row) const
{
    if (!isItem(row))
        return {};
    return m_items.at(m_rows[std::size_t(row)].item).toMap();
}

void QuickPickModel::toggle(int row)
{
    if (!m_many || !isItem(row))
        return;
    const QVariantMap item = itemAt(row);
    if (!text(item, "disabled").isEmpty())
        return;
    const QString id = text(item, "id");
    if (!m_picked.remove(id))
        m_picked.insert(id);
    emit dataChanged(index(row), index(row), {PickedRole});
    emit pickedChanged();
}

void QuickPickModel::pickAllShown()
{
    if (!m_many)
        return;
    for (const Row &row : m_rows)
        if (row.item >= 0) {
            const QVariantMap item = m_items.at(row.item).toMap();
            if (text(item, "disabled").isEmpty())
                m_picked.insert(text(item, "id"));
        }
    if (!m_rows.empty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), {PickedRole});
    emit pickedChanged();
}

QStringList QuickPickModel::pickedIds() const
{
    QStringList out;
    for (const QVariant &v : m_items) {
        const QString id = text(v.toMap(), "id");
        if (!id.isEmpty() && m_picked.contains(id))
            out.append(id);
    }
    return out;
}

}
