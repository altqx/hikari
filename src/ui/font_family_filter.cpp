#include "font_family_filter.h"

namespace hikari::ui {

FontFamilyFilter::FontFamilyFilter(QObject *parent) : QAbstractListModel(parent)
{
}

void FontFamilyFilter::setFamilies(const QStringList &families)
{
    if (families == m_families)
        return;
    m_families = families;
    m_folded.clear();
    m_folded.reserve(families.size());
    for (const QString &family : families)
        m_folded << family.toCaseFolded();
    emit familiesChanged();
    refilter();
}

void FontFamilyFilter::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    emit textChanged();
    refilter();
}

void FontFamilyFilter::refilter()
{
    const QString term = m_text.trimmed().toCaseFolded();
    QStringList exact;
    QStringList prefix;
    QStringList inside;
    for (qsizetype i = 0; i < m_families.size(); ++i) {
        const QString &folded = m_folded[i];
        if (term.isEmpty())
            inside << m_families[i];
        else if (folded == term)
            exact << m_families[i];
        else if (folded.startsWith(term))
            prefix << m_families[i];
        else if (folded.contains(term))
            inside << m_families[i];
    }
    QStringList matches = exact + prefix + inside;
    if (matches == m_matches)
        return;
    beginResetModel();
    m_matches = std::move(matches);
    endResetModel();
    emit matchesChanged();
}

int FontFamilyFilter::row(const QString &family) const
{
    const QString folded = family.trimmed().toCaseFolded();
    for (qsizetype i = 0; i < m_matches.size(); ++i)
        if (m_matches[i].toCaseFolded() == folded)
            return int(i);
    return -1;
}

int FontFamilyFilter::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_matches.size());
}

QVariant FontFamilyFilter::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_matches.size() || role != Qt::DisplayRole)
        return {};
    return m_matches[index.row()];
}

QHash<int, QByteArray> FontFamilyFilter::roleNames() const
{
    return {{Qt::DisplayRole, "family"}};
}

}
