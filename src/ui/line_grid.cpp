#include "line_grid.h"

#include "line_grid_accessible.h"
#include "line_table_model.h"

#include <QAbstractProxyModel>
#include <QAccessible>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace hikari::ui {

int GridGeometry::firstVisibleRow(double contentY) const
{
    return std::max(0, static_cast<int>(std::floor(std::max(0.0, contentY) / rowHeight)));
}

int GridGeometry::visibleRowCount(double contentY, double viewportHeight, int rowCount) const
{
    const double body = viewportHeight - headerHeight;
    if (body <= 0 || rowCount <= 0)
        return 0;
    const int first = firstVisibleRow(contentY);
    if (first >= rowCount)
        return 0;
    // Last row whose top is above the bottom edge of the body.
    const int last = static_cast<int>(std::ceil((std::max(0.0, contentY) + body) / rowHeight)) - 1;
    return std::min(last, rowCount - 1) - first + 1;
}

int GridGeometry::rowAt(double viewportY, double contentY, int rowCount) const
{
    if (viewportY < headerHeight)
        return -1;
    const int row = static_cast<int>(std::floor((viewportY - headerHeight + std::max(0.0, contentY)) / rowHeight));
    return row >= 0 && row < rowCount ? row : -1;
}

LineGrid::LineGrid(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setActiveFocusOnTab(true);
    setFlag(ItemIsFocusScope, false);
    installGridAccessibility();
    updateRowHeight();
    connect(this, &QQuickItem::heightChanged, this, [this] { setContentY(m_contentY); });
}

void LineGrid::updateRowHeight()
{
    const QFontMetricsF metrics(QFont{});
    m_geometry.rowHeight = std::ceil(metrics.height() + 6);
    m_geometry.headerHeight = m_geometry.rowHeight;
    emit rowHeightChanged();
}

void LineGrid::setModel(QAbstractItemModel *model)
{
    if (m_model == model)
        return;
    for (const auto &c : m_connections)
        disconnect(c);
    m_connections.clear();
    m_model = model;
    if (m_model) {
        auto relayout = [this] { modelLayoutChanged(); };
        auto repaint = [this] { update(); };
        m_connections = {
            connect(m_model, &QAbstractItemModel::modelReset, this, relayout),
            connect(m_model, &QAbstractItemModel::rowsInserted, this, relayout),
            connect(m_model, &QAbstractItemModel::rowsRemoved, this, relayout),
            connect(m_model, &QAbstractItemModel::layoutChanged, this, relayout),
            connect(m_model, &QAbstractItemModel::dataChanged, this,
                    [this, repaint](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                        repaint();
                        if (roles.contains(LineTableModel::ActiveRole) || roles.contains(LineTableModel::SelectedRole))
                            stateChanged();
                    }),
        };
    }
    emit modelChanged();
    modelLayoutChanged();
}

void LineGrid::modelLayoutChanged()
{
    emit contentHeightChanged();
    setContentY(m_contentY);
    update();
    stateChanged();
}

namespace {

LineTableModel *sourceLines(QAbstractItemModel *model)
{
    if (auto *lines = qobject_cast<LineTableModel *>(model))
        return lines;
    if (auto *proxy = qobject_cast<QAbstractProxyModel *>(model))
        return sourceLines(proxy->sourceModel());
    return nullptr;
}

} // namespace

std::optional<core::LineId> LineGrid::lineAtRow(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return std::nullopt;
    return core::LineId{m_model->index(row, 0).data(LineTableModel::LineIdRole).toULongLong()};
}

int LineGrid::rowOfLine(core::LineId id) const
{
    if (!m_model)
        return -1;
    if (auto *lines = qobject_cast<LineTableModel *>(m_model.data()))
        return lines->rowOf(id).value_or(-1);
    if (auto *filter = qobject_cast<LineFilterModel *>(m_model.data())) {
        auto *lines = sourceLines(filter);
        const auto source = lines ? lines->rowOf(id) : std::nullopt;
        if (!source)
            return -1;
        return filter->mapFromSource(lines->index(*source, 0)).row();
    }
    for (int r = 0; r < m_model->rowCount(); ++r)
        if (lineAtRow(r) == id)
            return r;
    return -1;
}

std::optional<core::LineId> LineGrid::activeLine() const
{
    auto *lines = sourceLines(m_model);
    return lines ? lines->selection().active : std::nullopt;
}

int LineGrid::currentRow() const
{
    const auto active = activeLine();
    return active ? rowOfLine(*active) : -1;
}

bool LineGrid::isRowSelected(int row) const
{
    return m_model && row >= 0 && row < m_model->rowCount() &&
           m_model->index(row, 0).data(LineTableModel::SelectedRole).toBool();
}

QList<int> LineGrid::shownSelectedRows() const
{
    QList<int> rows;
    if (auto *lines = sourceLines(m_model))
        for (const auto &id : lines->selection().selected)
            if (const int row = rowOfLine(id); row >= 0)
                rows.append(row);
    std::sort(rows.begin(), rows.end());
    return rows;
}

int LineGrid::selectedCount() const
{
    auto *lines = sourceLines(m_model);
    return lines ? static_cast<int>(lines->selection().selected.size()) : 0;
}

int LineGrid::hiddenSelectedCount() const
{
    if (auto *filter = qobject_cast<LineFilterModel *>(m_model.data()))
        return filter->hiddenSelectedCount();
    return 0;
}

QString LineGrid::cellText(int row, int column) const
{
    return m_model ? m_model->index(row, column).data().toString() : QString();
}

QString LineGrid::columnTitle(int column) const
{
    return m_model ? m_model->headerData(column, Qt::Horizontal).toString() : QString();
}

int LineGrid::columnCount() const
{
    return m_model ? std::min(m_model->columnCount(), 6) : 0;
}

QRectF LineGrid::cellRect(int row, int column) const
{
    const auto widths = columnWidths(width());
    double x = 0;
    for (int c = 0; c < column && c < static_cast<int>(widths.size()); ++c)
        x += widths[static_cast<std::size_t>(c)];
    const double w = column < static_cast<int>(widths.size()) ? widths[static_cast<std::size_t>(column)] : 0;
    return QRectF(x, m_geometry.headerHeight + row * m_geometry.rowHeight - m_contentY, w, m_geometry.rowHeight);
}

void LineGrid::scrollToRow(int row)
{
    const double top = row * m_geometry.rowHeight;
    const double body = height() - m_geometry.headerHeight;
    if (top < m_contentY)
        setContentY(top);
    else if (top + m_geometry.rowHeight > m_contentY + body)
        setContentY(top + m_geometry.rowHeight - body);
}

void LineGrid::keyPressEvent(QKeyEvent *event)
{
    const int rows = m_model ? m_model->rowCount() : 0;
    if (rows == 0) {
        event->ignore();
        return;
    }
    const int page = std::max(1, m_geometry.visibleRowCount(m_contentY, height(), rows) - 1);
    const int current = std::max(0, currentRow());
    int target = current;
    switch (event->key()) {
    case Qt::Key_Up: target = current - 1; break;
    case Qt::Key_Down: target = currentRow() < 0 ? 0 : current + 1; break;
    case Qt::Key_PageUp: target = current - page; break;
    case Qt::Key_PageDown: target = current + page; break;
    case Qt::Key_Home: target = 0; break;
    case Qt::Key_End: target = rows - 1; break;
    case Qt::Key_Left: m_currentColumn = std::max(0, m_currentColumn - 1); announceState(true); event->accept(); return;
    case Qt::Key_Right: m_currentColumn = std::min(columnCount() - 1, m_currentColumn + 1); announceState(true); event->accept(); return;
    default:
        event->ignore();
        return;
    }
    target = std::clamp(target, 0, rows - 1);
    scrollToRow(target);
    if (const auto id = lineAtRow(target))
        emit activeLineRequested(id->value);
    event->accept();
}

void LineGrid::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    announceState(true);
}

void LineGrid::stateChanged()
{
    // A filtered-out current Line moves to the nearest visible row; hidden
    // selection itself is kept and reported (accepted announcement policy).
    if (const auto active = activeLine(); active && rowOfLine(*active) < 0) {
        if (auto *filter = qobject_cast<LineFilterModel *>(m_model.data())) {
            const int nearest = filter->nearestVisibleRow(*active);
            if (const auto id = lineAtRow(nearest)) {
                emit activeLineRequested(id->value);
                return;
            }
        }
    }
    announceState(activeLine() != m_announcedActive);
}

void LineGrid::announceState(bool activeMoved)
{
    // Current cell first: a Focus event for the active cell when it moved.
    if (activeMoved && hasActiveFocus() && QAccessible::isActive()) {
        const int row = currentRow();
        if (row >= 0) {
            QAccessibleInterface *table = QAccessible::queryAccessibleInterface(this);
            if (auto *ti = table ? table->tableInterface() : nullptr)
                if (QAccessibleInterface *cell = ti->cellAt(row, m_currentColumn)) {
                    QAccessibleEvent focus(cell, QAccessible::Focus);
                    QAccessible::updateAccessibility(&focus);
                }
        }
    }
    m_announcedActive = activeLine();
    // Counts separately, and only when they change.
    const int selected = selectedCount(), hidden = hiddenSelectedCount();
    if (selected != m_announcedSelected || hidden != m_announcedHidden) {
        m_announcedSelected = selected;
        m_announcedHidden = hidden;
        if (QAccessible::isActive()) {
            const QString message = hidden > 0 ? tr("%n selected, %1 hidden", nullptr, selected).arg(hidden)
                                               : tr("%n selected", nullptr, selected);
            QAccessibleAnnouncementEvent announcement(this, message);
            QAccessible::updateAccessibility(&announcement);
        }
    }
}

qreal LineGrid::contentHeight() const
{
    return m_geometry.contentHeight(m_model ? m_model->rowCount() : 0);
}

void LineGrid::setContentY(qreal y)
{
    const qreal maxY = std::max<qreal>(0, contentHeight() - height());
    const qreal clamped = std::clamp<qreal>(y, 0, maxY);
    if (clamped == m_contentY)
        return;
    m_contentY = clamped;
    emit contentYChanged();
    update();
}

int LineGrid::rowAt(qreal y) const
{
    return m_geometry.rowAt(y, m_contentY, m_model ? m_model->rowCount() : 0);
}

std::vector<double> LineGrid::columnWidths(double total) const
{
    // #, Start, End, Style, Actor; Text takes the rest.
    const QFontMetricsF m(QFont{});
    std::vector<double> w{m.horizontalAdvance(QStringLiteral("00000")) + 8,
                          m.horizontalAdvance(QStringLiteral("0:00:00.00")) + 12,
                          m.horizontalAdvance(QStringLiteral("0:00:00.00")) + 12, 90, 90};
    double used = 0;
    for (double v : w)
        used += v;
    w.push_back(std::max(40.0, total - used));
    return w;
}

void LineGrid::paint(QPainter *painter)
{
    const QRectF bounds = boundingRect();
    painter->fillRect(bounds, QColor(0x20, 0x24, 0x2b));
    m_lastPainted = 0;
    const int rows = m_model ? m_model->rowCount() : 0;
    const int columns = m_model ? std::min(m_model->columnCount(), 6) : 0;
    const auto widths = columnWidths(bounds.width());
    const double rh = m_geometry.rowHeight;

    // Header.
    painter->fillRect(QRectF(0, 0, bounds.width(), m_geometry.headerHeight), QColor(0x2c, 0x31, 0x3a));
    painter->setPen(QColor(0xc8, 0xcc, 0xd4));
    double x = 0;
    for (int c = 0; c < columns; ++c) {
        painter->drawText(QRectF(x + 4, 0, widths[c] - 8, m_geometry.headerHeight), Qt::AlignVCenter,
                          m_model->headerData(c, Qt::Horizontal).toString());
        x += widths[c];
    }

    const int first = m_geometry.firstVisibleRow(m_contentY);
    const int count = m_geometry.visibleRowCount(m_contentY, bounds.height(), rows);
    painter->save();
    painter->setClipRect(QRectF(0, m_geometry.headerHeight, bounds.width(), bounds.height() - m_geometry.headerHeight));
    for (int row = first; row < first + count; ++row) {
        const double top = m_geometry.headerHeight + row * rh - m_contentY;
        const QModelIndex idx = m_model->index(row, 0);
        const bool selected = idx.data(LineTableModel::SelectedRole).toBool();
        const bool active = idx.data(LineTableModel::ActiveRole).toBool();
        const bool comment = idx.data(LineTableModel::CommentRole).toBool();
        QColor background = row % 2 ? QColor(0x24, 0x29, 0x31) : QColor(0x20, 0x24, 0x2b);
        if (selected)
            background = QColor(0x2f, 0x4b, 0x6e);
        painter->fillRect(QRectF(0, top, bounds.width(), rh), background);
        if (active) {
            painter->setPen(QColor(0x6c, 0xa8, 0xff));
            painter->drawRect(QRectF(0.5, top + 0.5, bounds.width() - 1, rh - 1));
        }
        painter->setPen(comment ? QColor(0x80, 0x86, 0x90) : QColor(0xe6, 0xe8, 0xec));
        x = 0;
        for (int c = 0; c < columns; ++c) {
            const QString text = m_model->index(row, c).data().toString();
            painter->drawText(QRectF(x + 4, top, widths[c] - 8, rh), Qt::AlignVCenter | Qt::TextSingleLine,
                              QFontMetricsF(painter->font()).elidedText(text, Qt::ElideRight, widths[c] - 8));
            x += widths[c];
        }
        ++m_lastPainted;
    }
    painter->restore();
    emit painted();
}

} // namespace hikari::ui
