#include "line_grid.h"

#include "line_table_model.h"

#include <QFontMetricsF>
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
            connect(m_model, &QAbstractItemModel::dataChanged, this, repaint),
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
