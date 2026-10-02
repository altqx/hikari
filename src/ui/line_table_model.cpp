#include "line_table_model.h"

#include <QString>

namespace hikari::ui {

namespace {

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

} // namespace

LineTableModel::LineTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void LineTableModel::setDocument(const core::Document &document)
{
    beginResetModel();
    m_rows.clear();
    m_rowById.clear();
    for (const core::LineRecord *line : document.lines()) {
        m_rowById.emplace(line->id.value, static_cast<int>(m_rows.size()));
        m_rows.push_back(Row{*line});
    }
    // Keep only selection that still names existing Lines.
    std::erase_if(m_selection.selected, [&](core::LineId id) { return !m_rowById.contains(id.value); });
    if (m_selection.active && !m_rowById.contains(m_selection.active->value))
        m_selection.active.reset();
    if (m_anchor && !m_rowById.contains(m_anchor->value))
        m_anchor.reset();
    endResetModel();
}

void LineTableModel::setSelection(const application::Selection &selection, std::optional<core::LineId> anchor)
{
    std::vector<core::LineId> touched(m_selection.selected.begin(), m_selection.selected.end());
    touched.insert(touched.end(), selection.selected.begin(), selection.selected.end());
    for (const auto &id : {m_selection.active, selection.active, m_anchor, anchor})
        if (id)
            touched.push_back(*id);
    m_selection = selection;
    m_anchor = anchor;
    emitStateChanged(touched);
}

void LineTableModel::emitStateChanged(const std::vector<core::LineId> &ids)
{
    for (const auto &id : ids)
        if (const auto row = rowOf(id))
            emit dataChanged(index(*row, 0), index(*row, ColumnCount - 1), {ActiveRole, SelectedRole, AnchorRole});
}

std::optional<int> LineTableModel::rowOf(core::LineId id) const
{
    const auto it = m_rowById.find(id.value);
    if (it == m_rowById.end())
        return std::nullopt;
    return it->second;
}

std::optional<core::LineId> LineTableModel::lineAt(int row) const
{
    const core::LineRecord *record = recordAt(row);
    return record ? std::optional(record->id) : std::nullopt;
}

const core::LineRecord *LineTableModel::recordAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return nullptr;
    return &m_rows[static_cast<std::size_t>(row)].line;
}

int LineTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int LineTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LineTableModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::LineRecord &line = m_rows[static_cast<std::size_t>(index.row())].line;
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NumberColumn:
            return index.row() + 1;
        case StartColumn:
            return qs(line.start.lexeme);
        case EndColumn:
            return qs(line.end.lexeme);
        case StyleColumn:
            return qs(line.style);
        case ActorColumn:
            return qs(line.actor);
        case TextColumn:
            return qs(line.text);
        default:
            return {};
        }
    case LineIdRole:
        return QVariant::fromValue<qulonglong>(line.id.value);
    case CommentRole:
        return line.comment;
    case ActiveRole:
        return m_selection.active == line.id;
    case SelectedRole:
        return m_selection.selected.contains(line.id);
    case AnchorRole:
        return m_anchor == line.id;
    case StartMicrosecondsRole:
        return QVariant::fromValue<qlonglong>(line.start.value.microseconds());
    case EndMicrosecondsRole:
        return QVariant::fromValue<qlonglong>(line.end.value.microseconds());
    default:
        return {};
    }
}

QVariant LineTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const char *names[] = {"#", "Start", "End", "Style", "Actor", "Text"};
    if (section < 0 || section >= ColumnCount)
        return {};
    return tr(names[section]);
}

QHash<int, QByteArray> LineTableModel::roleNames() const
{
    auto roles = QAbstractTableModel::roleNames();
    roles.insert({{LineIdRole, "lineId"},
                  {CommentRole, "comment"},
                  {ActiveRole, "active"},
                  {SelectedRole, "selected"},
                  {AnchorRole, "anchor"},
                  {StartMicrosecondsRole, "startMicroseconds"},
                  {EndMicrosecondsRole, "endMicroseconds"}});
    return roles;
}

LineFilterModel::LineFilterModel(QObject *parent) : QSortFilterProxyModel(parent) {}

void LineFilterModel::setLineModel(LineTableModel *model)
{
    m_lines = model;
    setSourceModel(model);
}

void LineFilterModel::setPredicate(Predicate predicate)
{
    beginFilterChange();
    m_predicate = std::move(predicate);
    endFilterChange(Direction::Rows);
}

bool LineFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (!m_predicate || !m_lines)
        return true;
    const core::LineRecord *record = m_lines->recordAt(sourceRow);
    return record && m_predicate(*record);
}

int LineFilterModel::hiddenSelectedCount() const
{
    if (!m_lines)
        return 0;
    int hidden = 0;
    for (const auto &id : m_lines->selection().selected) {
        const auto row = m_lines->rowOf(id);
        if (row && !filterAcceptsRow(*row, {}))
            ++hidden;
    }
    return hidden;
}

} // namespace hikari::ui
