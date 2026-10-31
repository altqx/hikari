#include "line_grid.h"

#include "line_grid_accessible.h"
#include "line_table_model.h"
#include "theme.h"
#include "wheel_scroll.h"
#include "hikari/application/subtitle_comparison.h"

#include <QCoreApplication>
#include <QCursor>
#include <QAbstractProxyModel>
#include <QAccessible>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

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
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, false);
    installGridAccessibility();
    updateRowHeight();
    theme::onChanged(this, [this] { update(); });
}

void LineGrid::updateRowHeight()
{
    const QFontMetricsF metrics(QFont{});
    m_geometry.rowHeight = std::ceil(metrics.height() + 6);
    m_geometry.headerHeight = m_geometry.rowHeight;
    emit rowHeightChanged();
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
        auto relayout = [this] {
            updateColumns();
            modelLayoutChanged();
        };
        auto repaint = [this] { update(); };
        m_connections = {
            connect(m_model, &QAbstractItemModel::modelReset, this, relayout),
            connect(m_model, &QAbstractItemModel::rowsInserted, this, relayout),
            connect(m_model, &QAbstractItemModel::rowsRemoved, this, relayout),
            connect(m_model, &QAbstractItemModel::layoutChanged, this, relayout),
            connect(m_model, &QAbstractItemModel::headerDataChanged, this, [this] {
                updateColumns();
                updateViewport();
                setHeaderToolTip(-1);
                update();
            }),
            connect(m_model, &QAbstractItemModel::dataChanged, this,
                    [this, repaint](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                        repaint();
                        if (roles.contains(LineTableModel::ActiveRole) || roles.contains(LineTableModel::SelectedRole))
                            stateChanged();
                    }),
        };
        if (auto *lines = sourceLines(m_model))
            m_connections.push_back(connect(lines, &LineTableModel::runFoldChanged, this, &LineGrid::announceFold));
    }
    emit modelChanged();
    updateColumns();
    modelLayoutChanged();
}

void LineGrid::modelLayoutChanged()
{
    emit contentHeightChanged();
    updateViewport();
    update();
    stateChanged();
}

namespace {

constexpr double kFoldIndent = 14;
constexpr double kMinColumnWidth = 24;
constexpr double kMinFlexibleWidth = 120;
constexpr double kDividerGrip = 4;

constexpr std::array<const char *, LineTableModel::ColumnCount> kColumnKeys{
    "number", "layer", "start", "end", "style", "actor", "marginLeft", "marginRight", "marginVertical",
    "effect", "cps", "wraps", "text", "translation"};
static_assert(kColumnKeys.back() != nullptr);

double minimumWidth(int column)
{
    return column == LineTableModel::TextColumn || column == LineTableModel::TranslationColumn ? kMinFlexibleWidth
                                                                                               : kMinColumnWidth;
}


}

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

bool LineGrid::foldsWith(int key, int modifiers) const
{
    const int row = currentRow();
    if (!m_model || row < 0 || modifiers != Qt::NoModifier || (key != Qt::Key_Left && key != Qt::Key_Right))
        return false;
    return runFold(row) == (key == Qt::Key_Right ? RunFold::Folded : RunFold::Unfolded);
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

void LineGrid::updateColumns()
{
    const int previous = m_currentColumn >= 0 ? modelColumn(m_currentColumn) : LineTableModel::TextColumn;
    m_columns.clear();
    if (m_model)
        for (int c = 0; c < m_model->columnCount(); ++c) {
            const QVariant shown = m_model->headerData(c, Qt::Horizontal, LineTableModel::ColumnShownRole);
            if (!shown.isValid() || shown.toBool())
                m_columns.push_back(c);
        }
    m_markWidth = m_model && m_model->headerData(0, Qt::Horizontal, LineTableModel::FilteredRole).toBool() ? 11 : 0;
    const auto it = std::find(m_columns.begin(), m_columns.end(), previous);
    m_currentColumn = it != m_columns.end() ? static_cast<int>(it - m_columns.begin())
                                            : static_cast<int>(m_columns.size()) - 1;
}

int LineGrid::textColumn() const
{
    const auto it = std::find(m_columns.begin(), m_columns.end(), LineTableModel::TextColumn);
    return it != m_columns.end() ? static_cast<int>(it - m_columns.begin()) : -1;
}

int LineGrid::modelColumn(int column) const
{
    return column >= 0 && column < static_cast<int>(m_columns.size()) ? m_columns[static_cast<std::size_t>(column)] : -1;
}

QString LineGrid::cellText(int row, int column) const
{
    return m_model ? m_model->index(row, modelColumn(column)).data().toString() : QString();
}

QString LineGrid::rowStateText(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return {};
    const int state = m_model->index(row, 0).data(LineTableModel::LineStateRole).toInt();
    QStringList words;
    if ((state & 3) == 1)
        words << QCoreApplication::translate("HikariSub.Grid", "changed");
    else if ((state & 3) == 2)
        words << QCoreApplication::translate("HikariSub.Grid", "changed, saved");
    if (state & 4)
        words << QCoreApplication::translate("HikariSub.Grid", "unconfirmed");
    if (state & 8)
        words << QCoreApplication::translate("HikariSub.Grid", "bookmarked");
    if (m_model->index(row, 0).data(LineTableModel::DraftRole).toBool())
        words << QCoreApplication::translate("HikariSub.Grid", "uncommitted");
    if (m_model->index(row, 0).data(LineTableModel::UnplacedRunsRole).toBool())
        words << QCoreApplication::translate("HikariSub.Grid", "runs not placed");
    if (const RunFold fold = runFold(row); fold == RunFold::Folded)
        words << QCoreApplication::translate("HikariSub.Grid", "folded");
    else if (fold == RunFold::Unfolded)
        words << QCoreApplication::translate("HikariSub.Grid", "unfolded");
    return words.join(QStringLiteral(", "));
}

QString LineGrid::columnTitle(int column) const
{
    return m_model ? m_model->headerData(modelColumn(column), Qt::Horizontal).toString() : QString();
}

RunFold LineGrid::runFold(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return RunFold::None;
    switch (m_model->index(row, 0).data(LineTableModel::FoldRole).toInt()) {
    case 1: return RunFold::Folded;
    case 2: return RunFold::Unfolded;
    default: return RunFold::None;
    }
}

int LineGrid::columnCount() const
{
    return m_model ? static_cast<int>(m_columns.size()) : 0;
}

QRectF LineGrid::cellRect(int row, int column) const
{
    const auto widths = layoutWidths();
    const double w = column >= 0 && column < static_cast<int>(widths.size()) ? widths[static_cast<std::size_t>(column)] : 0;
    return QRectF(columnLeft(widths, column), m_geometry.headerHeight + row * m_geometry.rowHeight - m_contentY, w,
                  m_geometry.rowHeight);
}

double LineGrid::columnLeft(const std::vector<double> &widths, int column) const
{
    const auto end = widths.begin() + std::clamp<std::ptrdiff_t>(column, 0, static_cast<std::ptrdiff_t>(widths.size()));
    return std::accumulate(widths.begin(), end, m_markWidth - m_contentX);
}

int LineGrid::dividerAt(QPointF at) const
{
    if (at.y() < 0 || at.y() >= m_geometry.headerHeight || at.x() < m_markWidth || at.x() >= m_viewport.width())
        return -1;
    const auto widths = layoutWidths();
    for (int c = 0; c + 1 < static_cast<int>(widths.size()); ++c)
        if (std::abs(at.x() - columnLeft(widths, c + 1)) <= kDividerGrip)
            return c;
    return -1;
}

void LineGrid::resizeColumn(int column, double width)
{
    const int mc = modelColumn(column);
    if (mc < 0 || column + 1 >= columnCount())
        return;
    width = std::round(width);
    if (flexible(mc)) {
        const auto widths = layoutWidths();
        const int next = modelColumn(column + 1);
        const double pair = widths[std::size_t(column)] + widths[std::size_t(column) + 1];
        m_userWidths[next] = std::max(minimumWidth(next), pair - std::max(minimumWidth(mc), width));
    } else {
        m_userWidths[mc] = std::max(minimumWidth(mc), width);
    }
    emit columnWidthsChanged();
    updateViewport();
    update();
}

void LineGrid::fitColumn(int column)
{
    const int mc = modelColumn(column);
    if (mc < 0 || !m_model)
        return;
    const QFontMetricsF metrics(QFont{});
    QFont bold;
    bold.setBold(true);
    const QFontMetricsF boldMetrics(bold);
    double width = metrics.horizontalAdvance(columnTitle(column)) + 8;
    for (int row = 0, rows = m_model->rowCount(); row < rows; ++row) {
        const QString text = m_model->index(row, mc).data().toString();
        const int fold = mc == LineTableModel::TextColumn ? m_model->index(row, 0).data(LineTableModel::FoldRole).toInt() : 0;
        const double advance = (fold == 1 ? boldMetrics : metrics).horizontalAdvance(text);
        width = std::max(width, std::ceil(advance) + 8 + (fold != 0 ? kFoldIndent : 0));
    }
    resizeColumn(column, width);
}

QVariantMap LineGrid::columnWidths() const
{
    QVariantMap widths;
    for (const auto &[column, width] : m_userWidths)
        widths.insert(QLatin1String(kColumnKeys[std::size_t(column)]), qRound(width));
    return widths;
}

void LineGrid::setColumnWidths(const QVariantMap &widths)
{
    std::map<int, double> parsed;
    for (auto it = widths.begin(); it != widths.end(); ++it) {
        const auto key = std::find_if(kColumnKeys.begin(), kColumnKeys.end(),
                                      [&](const char *k) { return it.key() == QLatin1String(k); });
        const int column = static_cast<int>(key - kColumnKeys.begin());
        if (const double width = it.value().toDouble(); column != LineTableModel::TextColumn && key != kColumnKeys.end() && width > 0)
            parsed[column] = width;
    }
    if (parsed == m_userWidths)
        return;
    m_userWidths = std::move(parsed);
    emit columnWidthsChanged();
    updateViewport();
    update();
}

bool LineGrid::flexible(int column) const
{
    return column == LineTableModel::TextColumn ||
           (column == LineTableModel::TranslationColumn && !m_userWidths.contains(column));
}

void LineGrid::updateViewport()
{
    const auto overflows = [this](double available) {
        const auto widths = widthsFor(available - m_markWidth);
        return std::accumulate(widths.begin(), widths.end(), m_markWidth) > available + 0.5;
    };
    const bool rows = m_model && m_model->rowCount() > 0;
    bool vertical = rows && contentHeight() > height();
    const bool horizontal = overflows(width() - (vertical ? scrollBarRoom() : 0));
    vertical = vertical || (rows && horizontal && contentHeight() > height() - scrollBarRoom());
    const QSizeF viewport(std::max<qreal>(0, width() - (vertical ? scrollBarRoom() : 0)),
                          std::max<qreal>(0, height() - (horizontal ? scrollBarRoom() : 0)));
    const auto widths = widthsFor(viewport.width() - m_markWidth);
    const double content = std::accumulate(widths.begin(), widths.end(), m_markWidth);
    if (viewport != m_viewport || vertical != m_verticalBar || horizontal != m_horizontalBar || content != m_contentWidth) {
        m_viewport = viewport;
        m_verticalBar = vertical;
        m_horizontalBar = horizontal;
        m_contentWidth = content;
        emit viewportChanged();
        update();
    }
    setContentX(m_contentX);
    setContentY(m_contentY);
}

void LineGrid::scrollToRow(int row)
{
    const double top = row * m_geometry.rowHeight;
    const double body = m_viewport.height() - m_geometry.headerHeight;
    if (top < m_contentY)
        setContentY(top);
    else if (top + m_geometry.rowHeight > m_contentY + body)
        setContentY(top + m_geometry.rowHeight - body);
}

void LineGrid::scrollToColumn(int column)
{
    const QRectF cell = cellRect(0, column);
    if (cell.left() < m_markWidth)
        setContentX(m_contentX + cell.left() - m_markWidth);
    else if (cell.right() > m_viewport.width())
        setContentX(m_contentX + std::min(cell.right() - m_viewport.width(), cell.left() - m_markWidth));
}

void LineGrid::makeLineVisible(qulonglong id)
{
    const int row = rowOfLine(core::LineId{id});
    if (row < 0 || m_geometry.rowHeight <= 0)
        return;
    const int rows = static_cast<int>(m_viewport.height() / m_geometry.rowHeight);
    const int top = static_cast<int>(m_contentY / m_geometry.rowHeight);
    if (top > row || top + rows < row + 2)
        setContentY(std::max(0, row - rows / 2 + 1) * m_geometry.rowHeight);
}

void LineGrid::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    const int row = currentRow();
    const double top = row * m_geometry.rowHeight;
    const double oldHeight = m_viewport.height();
    const bool shown = row >= 0 && top >= m_contentY &&
                       top + m_geometry.rowHeight <= m_contentY + oldHeight - m_geometry.headerHeight;
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    updateViewport();
    if (shown && m_viewport.height() < oldHeight)
        scrollToRow(row);
}

void LineGrid::keyPressEvent(QKeyEvent *event)
{
    const int rows = m_model ? m_model->rowCount() : 0;
    if (rows == 0) {
        event->ignore();
        return;
    }
    const int page = std::max(1, m_geometry.visibleRowCount(m_contentY, m_viewport.height(), rows) - 1);
    const auto mods = event->modifiers();
    if (event->matches(QKeySequence::SelectAll)) {
        emit selectAllRequested();
        event->accept();
        return;
    }
    if ((mods & Qt::ShiftModifier) && !(mods & (Qt::ControlModifier | Qt::AltModifier))) {
        int step = 0;
        switch (event->key()) {
        case Qt::Key_Up: step = -1; break;
        case Qt::Key_Down: step = 1; break;
        case Qt::Key_PageUp: step = -page; break;
        case Qt::Key_PageDown: step = page; break;
        case Qt::Key_Home: step = -rows; break;
        case Qt::Key_End: step = rows; break;
        default: break;
        }
        if (step != 0) {
            emit extendRequested(step);
            event->accept();
            return;
        }
    }
    const int current = std::max(0, currentRow());
    int target = current;
    switch (event->key()) {
    case Qt::Key_Up: target = current - 1; break;
    case Qt::Key_Down: target = currentRow() < 0 ? 0 : current + 1; break;
    case Qt::Key_PageUp: target = current - page; break;
    case Qt::Key_PageDown: target = current + page; break;
    case Qt::Key_Home: target = 0; break;
    case Qt::Key_End: target = rows - 1; break;
    case Qt::Key_Left:
    case Qt::Key_Right: {
        if (foldsWith(event->key(), mods.toInt())) {
            emit foldToggleRequested(lineAtRow(current)->value);
        } else {
            m_currentColumn = event->key() == Qt::Key_Left ? std::max(0, m_currentColumn - 1)
                                                           : std::min(columnCount() - 1, m_currentColumn + 1);
            scrollToColumn(m_currentColumn);
            announceState(true);
        }
        event->accept();
        return;
    }
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

void LineGrid::wheelEvent(QWheelEvent *event)
{
    const qreal dy = wheelScrollPixels(*event, m_geometry.rowHeight, Qt::Vertical).y();
    if (dy == 0) {
        event->ignore();
        return;
    }
    setContentY(m_contentY - dy);
    event->accept();
}

void LineGrid::mousePressEvent(QMouseEvent *event)
{
    if (!QRectF(QPointF(), m_viewport).contains(event->position())) {
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        forceActiveFocus(Qt::MouseFocusReason);
        const int row = rowAt(event->position().y());
        if (row >= 0 && m_model->index(row, 0).data(LineTableModel::GroupRole).toInt() == 1) {
            emit groupMenuRequested(lineAtRow(row)->value, event->position().x(), event->position().y());
            event->accept();
            return;
        }
        if (const auto id = row >= 0 ? lineAtRow(row) : std::nullopt; id && !isRowSelected(row))
            emit activeLineRequested(id->value);
        emit contextMenuRequested(event->position().x(), event->position().y());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    if (const int divider = dividerAt(event->position()); divider >= 0) {
        m_resize = ColumnResize{divider, event->position().x(), layoutWidths()[std::size_t(divider)]};
        setHeaderToolTip(-1);
        event->accept();
        return;
    }
    if (m_markWidth > 0 && event->position().x() < m_markWidth) {
        const int row = rowAt(event->position().y() + m_geometry.rowHeight / 2) - 1;
        int mark = 0, documentRow = -1;
        if (row < 0) {
            mark = m_model->headerData(0, Qt::Horizontal, LineTableModel::HeaderBlockRole).toInt();
        } else if (row < m_model->rowCount()) {
            const QModelIndex idx = m_model->index(row, 0);
            mark = idx.data(LineTableModel::HiddenBlockRole).toInt();
            documentRow = idx.data(LineTableModel::HiddenBlockRowRole).toInt();
        }
        if (mark && event->position().y() > m_geometry.headerHeight / 2)
            emit hiddenBlockToggleRequested(documentRow);
        event->accept();
        return;
    }
    const int row = rowAt(event->position().y());
    const auto id = row >= 0 ? lineAtRow(row) : std::nullopt;
    if (id && event->modifiers() == Qt::NoModifier &&
        m_model->index(row, 0).data(LineTableModel::GroupRole).toInt() == 1) {
        emit groupToggleRequested(id->value);
        event->accept();
        return;
    }
    if (id && event->modifiers() == Qt::NoModifier && onFoldArrow(event->position().x()) &&
        m_model->index(row, 0).data(LineTableModel::FoldRole).toInt() != 0) {
        emit foldToggleRequested(id->value);
        event->accept();
        return;
    }
    m_dragLine = id;
    if (id)
        emit lineClicked(id->value, static_cast<int>(event->modifiers()), inEndColumn(event->position().x()), false);
    event->accept();
}

void LineGrid::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || (m_markWidth > 0 && event->position().x() < m_markWidth) ||
        !QRectF(QPointF(), m_viewport).contains(event->position())) {
        event->ignore();
        return;
    }
    if (const int divider = dividerAt(event->position()); divider >= 0) {
        m_resize.reset();
        fitColumn(divider);
        emit columnWidthsEdited();
        event->accept();
        return;
    }
    const int row = rowAt(event->position().y());
    const auto id = row >= 0 ? lineAtRow(row) : std::nullopt;
    if (!id || m_model->index(row, 0).data(LineTableModel::GroupRole).toInt() == 1) {
        event->ignore();
        return;
    }
    emit lineClicked(id->value, static_cast<int>(event->modifiers()), inEndColumn(event->position().x()), true);
    event->accept();
}

bool LineGrid::inEndColumn(qreal x) const
{
    return modelColumn(columnAt(x)) == LineTableModel::EndColumn;
}

bool LineGrid::onFoldArrow(qreal x) const
{
    const int column = columnAt(x);
    if (modelColumn(column) != LineTableModel::TextColumn)
        return false;
    return x < columnLeft(layoutWidths(), column) + 4 + kFoldIndent;
}

void LineGrid::drawFoldArrow(QPainter *painter, const QRectF &cell, bool folded) const
{
    const QPointF c(cell.left() + 4 + kFoldIndent / 2 - 2, cell.center().y());
    const QPointF right[] = {{c.x() - 2, c.y() - 4}, {c.x() + 2, c.y()}, {c.x() - 2, c.y() + 4}};
    const QPointF down[] = {{c.x() - 4, c.y() - 2}, {c.x(), c.y() + 2}, {c.x() + 4, c.y() - 2}};
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(theme::current().roles.text, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPolyline(folded ? right : down, 3);
    painter->restore();
}

int LineGrid::columnAt(qreal x) const
{
    if (x < m_markWidth || x >= m_viewport.width())
        return -1;
    const auto widths = layoutWidths();
    double left = columnLeft(widths, 0);
    for (std::size_t c = 0; c < widths.size(); ++c) {
        if (x >= left && x < left + widths[c])
            return static_cast<int>(c);
        left += widths[c];
    }
    return -1;
}

void LineGrid::hoverMoveEvent(QHoverEvent *event)
{
    if (dividerAt(event->position()) >= 0)
        setCursor(Qt::SplitHCursor);
    else
        unsetCursor();
    setHeaderToolTip(event->position().y() < m_geometry.headerHeight ? columnAt(event->position().x()) : -1);
}

void LineGrid::hoverLeaveEvent(QHoverEvent *event)
{
    if (!m_resize)
        unsetCursor();
    setHeaderToolTip(-1);
    QQuickPaintedItem::hoverLeaveEvent(event);
}

void LineGrid::setHeaderToolTip(int column)
{
    const QString tip = m_model && column >= 0
                            ? m_model->headerData(modelColumn(column), Qt::Horizontal, Qt::ToolTipRole).toString()
                            : QString();
    QRectF cell;
    if (!tip.isEmpty()) {
        const QRectF first = cellRect(0, column);
        cell = QRectF(first.x(), 0, first.width(), m_geometry.headerHeight);
    }
    if (tip == m_headerToolTip && cell == m_headerToolTipCell)
        return;
    m_headerToolTip = tip;
    m_headerToolTipCell = cell;
    emit headerToolTipChanged();
}

void LineGrid::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resize && (event->buttons() & Qt::LeftButton)) {
        resizeColumn(m_resize->column, m_resize->startWidth + event->position().x() - m_resize->pressX);
        event->accept();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton) || event->modifiers() != Qt::NoModifier || !m_dragLine) {
        event->ignore();
        return;
    }
    const int row = rowAt(event->position().y());
    const auto id = row >= 0 ? lineAtRow(row) : std::nullopt;
    if (id && id != m_dragLine) {
        m_dragLine = id;
        scrollToRow(row);
        emit lineDragged(id->value);
    }
    event->accept();
}

void LineGrid::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resize) {
        m_resize.reset();
        emit columnWidthsEdited();
    }
    m_dragLine.reset();
    event->accept();
}

void LineGrid::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    update();
    announceState(true);
}

void LineGrid::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    update();
}

void LineGrid::stateChanged()
{
    if (const auto active = activeLine(); active && rowOfLine(*active) < 0) {
        if (auto *filter = qobject_cast<LineFilterModel *>(m_model.data())) {
            const int nearest = filter->nearestVisibleRow(*active);
            if (const auto id = lineAtRow(nearest)) {
                emit activeLineFallbackRequested(id->value);
                return;
            }
        }
    }
    announceState(activeLine() != m_announcedActive);
}

void LineGrid::announceState(bool activeMoved)
{
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
    const int selected = selectedCount(), hidden = hiddenSelectedCount();
    if (selected != m_announcedSelected || hidden != m_announcedHidden) {
        m_announcedSelected = selected;
        m_announcedHidden = hidden;
        if (QAccessible::isActive()) {
            const QString message =
                hidden > 0 ? QCoreApplication::translate("HikariSub.Grid", "%n selected, %1 hidden", nullptr, selected).arg(hidden)
                           : QCoreApplication::translate("HikariSub.Grid", "%n selected", nullptr, selected);
            QAccessibleAnnouncementEvent announcement(this, message);
            QAccessible::updateAccessibility(&announcement);
        }
    }
}

void LineGrid::announceFold(qulonglong first)
{
    const int row = rowOfLine(core::LineId{first});
    QAccessibleInterface *table = row >= 0 && QAccessible::isActive() ? QAccessible::queryAccessibleInterface(this) : nullptr;
    auto *ti = table ? table->tableInterface() : nullptr;
    if (!ti)
        return;
    if (QAccessibleInterface *cell = ti->cellAt(row, textColumn())) {
        QAccessible::State changed;
        changed.expanded = true;
        QAccessibleStateChangeEvent event(cell, changed);
        QAccessible::updateAccessibility(&event);
    }
}

qreal LineGrid::contentHeight() const
{
    return m_geometry.contentHeight(m_model ? m_model->rowCount() : 0);
}

void LineGrid::setContentX(qreal x)
{
    const qreal clamped = std::clamp<qreal>(x, 0, std::max<qreal>(0, m_contentWidth - m_viewport.width()));
    if (clamped == m_contentX)
        return;
    m_contentX = clamped;
    emit contentXChanged();
    update();
}

void LineGrid::setContentY(qreal y)
{
    const qreal maxY = std::max<qreal>(0, contentHeight() - m_viewport.height());
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

std::vector<double> LineGrid::widthsFor(double total) const
{
    const QFontMetricsF m(QFont{});
    auto fit = [&](const char *sample, double pad) { return m.horizontalAdvance(QLatin1String(sample)) + pad; };
    std::vector<double> w;
    double used = 0;
    for (const int c : m_columns) {
        double width = 0;
        if (const auto user = m_userWidths.find(c); user != m_userWidths.end() && !flexible(c)) {
            w.push_back(std::max(minimumWidth(c), user->second));
            used += w.back();
            continue;
        }
        switch (c) {
        case LineTableModel::NumberColumn: width = fit("00000", 8); break;
        case LineTableModel::LayerColumn: width = fit("000", 8); break;
        case LineTableModel::StartColumn:
        case LineTableModel::EndColumn: width = fit("00:00:00,000", 12); break;
        case LineTableModel::StyleColumn:
        case LineTableModel::ActorColumn: width = 90; break;
        case LineTableModel::MarginLeftColumn:
        case LineTableModel::MarginRightColumn:
        case LineTableModel::MarginVerticalColumn: width = fit("Right", 8); break;
        case LineTableModel::EffectColumn: width = 60; break;
        case LineTableModel::CpsColumn: width = fit("CPS", 10); break;
        case LineTableModel::WrapsColumn: width = fit("00/00", 10); break;
        default: width = -1; break;
        }
        if (width > 0 && m_model)
            width = std::max(width, m.horizontalAdvance(m_model->headerData(c, Qt::Horizontal).toString()) + 8);
        w.push_back(width);
        used += std::max(0.0, width);
    }
    const auto rest = static_cast<double>(std::count_if(w.begin(), w.end(), [](double v) { return v < 0; }));
    for (double &v : w)
        if (v < 0)
            v = std::max(kMinFlexibleWidth, (total - used) / rest);
    return w;
}

void LineGrid::drawBlockMark(QPainter *painter, double borderY, int mark, double width) const
{
    if (!mark)
        return;
    painter->save();
    painter->setPen(theme::current().roles.muted);
    painter->setBrush(Qt::NoBrush);
    const QRectF box(1, borderY - 5, 9, 9);
    painter->drawRect(box);
    painter->drawLine(QPointF(3, borderY - 0.5), QPointF(8, borderY - 0.5));
    if (mark == 1)
        painter->drawLine(QPointF(5.5, box.top() + 2), QPointF(5.5, box.bottom() - 2));
    painter->drawLine(QPointF(10, borderY - 0.5), QPointF(width, borderY - 0.5));
    painter->restore();
}

std::optional<QColor> comparisonBackground(int state, bool comment, bool selected, const QVariantList &colours,
                                           const QColor &selection)
{
    if (state != 1 && state != 2)
        return std::nullopt;
    const int slot = comment ? (state == 2 ? 3 : 4) : (state == 2 ? 1 : 2);
    const QColor colour = colours.value(slot).value<QColor>();
    return selected ? selectionOver(colour, selection) : colour;
}

QColor commentInk(const theme::Roles &roles, const QColor &background)
{
    return theme::contrastRatio(roles.muted, background) >= 4.5 ? roles.muted : roles.text;
}

QColor selectionOver(const QColor &colour, const QColor &selection)
{
    const int r = selection.red(), g = selection.green(), b = selection.blue(), invA = 0xFF - selection.alpha();
    return QColor(colour.red() * invA / 0xFF + (r - invA * r / 0xFF), colour.green() * invA / 0xFF + (g - invA * g / 0xFF),
                  colour.blue() * invA / 0xFF + (b - invA * b / 0xFF));
}

static QColor labelInk(const QColor &label)
{
    const auto &roles = theme::current().roles;
    if (label.isValid() && theme::contrastRatio(roles.field, label) > theme::contrastRatio(roles.text, label))
        return roles.field;
    return roles.text;
}

void LineGrid::drawLabel(QPainter *painter, const QRectF &cell, int state, bool draft, const QVariantList &colours) const
{
    const QColor label = colours.value(LineTableModel::labelSlot(state)).value<QColor>();
    if (colours.size() == 4)
        painter->fillRect(cell, label);
    const int changed = state & 3;
    if (!changed && !draft)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor mark = labelInk(label);
    // Centred on a device pixel corner so the ring's 1-wide pen covers whole pixels.
    const QTransform &device = painter->deviceTransform();
    const QPointF centre = device.map(QPointF(cell.right() - 6.5, cell.center().y()));
    const QPointF snapped = device.inverted().map(QPointF(std::round(centre.x()), std::round(centre.y())));
    const QRectF dot(snapped.x() - 2.5, snapped.y() - 2.5, 5, 5);
    if (draft) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(mark);
        const QPointF triangle[] = {QPointF(dot.left(), snapped.y() - 3.5), QPointF(dot.right() + 0.5, snapped.y()),
                                    QPointF(dot.left(), snapped.y() + 3.5)};
        painter->drawPolygon(triangle, 3);
        painter->restore();
        return;
    }
    if (changed == 1) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(mark);
    } else {
        painter->setPen(QPen(mark, 1));
        painter->setBrush(Qt::NoBrush);
    }
    painter->drawEllipse(dot);
    painter->restore();
}

void LineGrid::drawComparisonMarks(QPainter *painter, const QRectF &cell, const QString &text, const QVariantList &marks,
                                   const QColor &outline) const
{
    if (marks.size() < 2)
        return;
    const QFontMetricsF metrics(painter->font());
    painter->save();
    painter->setClipRect(cell, Qt::IntersectClip);
    painter->setPen(outline);
    for (qsizetype m = 0; m + 1 < marks.size(); m += 2) {
        const auto marked = application::markedRun(
            std::u16string_view(reinterpret_cast<const char16_t *>(text.utf16()), static_cast<std::size_t>(text.size())),
            marks[m].toInt(), marks[m + 1].toInt());
        if (!marked)
            continue;
        const auto start = static_cast<qsizetype>(marked->start);
        QString run = text.mid(start, static_cast<qsizetype>(marked->length));
        if (run == QLatin1String(" "))
            run = QStringLiteral("_");
        const double before = start > 0 ? metrics.horizontalAdvance(text.left(start)) : 0;
        for (const QPointF offset : {QPointF(-1, -1), QPointF(1, -1), QPointF(-1, 1), QPointF(1, 1)})
            painter->drawText(cell.translated(before + offset.x(), offset.y()), Qt::AlignVCenter | Qt::TextSingleLine,
                              run);
    }
    painter->restore();
}

void LineGrid::drawSpellMarks(QPainter *painter, const QRectF &cell, QString text, const QVariantList &marks) const
{
    if (marks.size() < 2)
        return;
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    const QFontMetricsF metrics(painter->font());
    const double baseline = cell.top() + (cell.height() - metrics.height()) / 2 + metrics.ascent();
    const double y = std::min(baseline + metrics.underlinePos() + 2, cell.bottom() - 1);
    painter->save();
    painter->setClipRect(cell, Qt::IntersectClip);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(theme::current().content.spellcheck, 1));
    painter->setBrush(Qt::NoBrush);
    for (qsizetype s = 0; s + 1 < marks.size(); s += 2) {
        const int start = marks[s].toInt(), end = marks[s + 1].toInt();
        QString error = text.mid(start, end - start + 1);
        while (!error.isEmpty() && error.back().isSpace() && error.back().unicode() < 127)
            error.chop(1);
        const double left = cell.x() + (start > 0 ? metrics.horizontalAdvance(text.left(start)) : 0);
        const double right = std::min(left + metrics.horizontalAdvance(error), cell.right());
        if (left >= right)
            continue;
        QPainterPath wave(QPointF(left, y));
        bool up = true;
        for (double x = left; x < right; x += 3, up = !up)
            wave.lineTo(std::min(x + 3, right), up ? y - 2.5 : y);
        painter->drawPath(wave);
    }
    painter->restore();
}

void LineGrid::paint(QPainter *painter)
{
    const QRectF bounds = boundingRect();
    const auto &roles = theme::current().roles;
    const auto &content = theme::current().content;
    painter->fillRect(bounds, roles.panel);
    m_lastPainted = 0;
    const int rows = m_model ? m_model->rowCount() : 0;
    const int columns = columnCount();
    const auto widths = layoutWidths();
    const double rh = m_geometry.rowHeight;
    const double viewWidth = m_viewport.width();
    const QRectF columnsArea(m_markWidth, 0, viewWidth - m_markWidth, m_viewport.height());

    painter->fillRect(QRectF(0, 0, bounds.width(), m_geometry.headerHeight), roles.raised);
    painter->save();
    painter->setClipRect(columnsArea.intersected(QRectF(0, 0, viewWidth, m_geometry.headerHeight)));
    double x = columnLeft(widths, 0);
    for (int c = 0; c < columns; ++c) {
        painter->setPen(roles.muted);
        painter->drawText(QRectF(x + 4, 0, widths[c] - 8, m_geometry.headerHeight), Qt::AlignVCenter | Qt::TextSingleLine,
                          QFontMetricsF(painter->font()).elidedText(columnTitle(c), Qt::ElideRight, widths[c] - 8));
        x += widths[c];
        if (c + 1 < columns)
            painter->fillRect(QRectF(std::round(x) - 1, 4, 1, m_geometry.headerHeight - 8), roles.line);
    }
    painter->restore();

    const int first = m_geometry.firstVisibleRow(m_contentY);
    const int count = m_geometry.visibleRowCount(m_contentY, m_viewport.height(), rows);
    const QVariantList comparisonColours =
        m_model ? m_model->headerData(0, Qt::Horizontal, LineTableModel::ComparisonColoursRole).toList() : QVariantList();
    const QVariantList labelColours =
        m_model ? m_model->headerData(0, Qt::Horizontal, LineTableModel::LabelColoursRole).toList() : QVariantList();
    const bool numberShown = columns > 0 && modelColumn(0) == LineTableModel::NumberColumn;
    painter->save();
    painter->setClipRect(QRectF(0, m_geometry.headerHeight, viewWidth, m_viewport.height() - m_geometry.headerHeight));
    for (int row = first; row < first + count; ++row) {
        const double top = m_geometry.headerHeight + row * rh - m_contentY;
        const QModelIndex idx = m_model->index(row, 0);
        const bool selected = idx.data(LineTableModel::SelectedRole).toBool();
        const bool active = idx.data(LineTableModel::ActiveRole).toBool();
        const bool comment = idx.data(LineTableModel::CommentRole).toBool();
        // MUST KILL: FoldRole is a magic int (0 none, 1 folded run, 2 unfolded run's first row)
        const int fold = idx.data(LineTableModel::FoldRole).toInt();
        QColor background = row % 2 ? content.gridAlternate : roles.panel;
        if (comment)
            background = selected ? selectionOver(content.gridComment, content.comparisonSelection) : content.gridComment;
        else if (selected)
            background = roles.select;
        const int comparison = idx.data(LineTableModel::ComparisonRole).toInt();
        painter->fillRect(QRectF(0, top, viewWidth, rh), background);
        const auto compared =
            comparisonBackground(comparison, comment, selected, comparisonColours, content.comparisonSelection);
        if (compared) {
            const double from = std::max(m_markWidth, columnLeft(widths, numberShown ? 1 : 0));
            painter->fillRect(QRectF(from, top, viewWidth - from, rh), *compared);
        }
        painter->save();
        painter->setClipRect(columnsArea, Qt::IntersectClip);
        if (numberShown)
            drawLabel(painter, QRectF(columnLeft(widths, 0), top, widths[0], rh),
                      idx.data(LineTableModel::LineStateRole).toInt(), idx.data(LineTableModel::DraftRole).toBool(),
                      labelColours);
        painter->setPen(comment ? commentInk(roles, compared.value_or(background)) : roles.text);
        if (fold == 1) {
            painter->save();
            painter->setPen(QPen(roles.control, 1, Qt::DashLine));
            painter->drawLine(QPointF(0, top + 0.5), QPointF(viewWidth, top + 0.5));
            painter->drawLine(QPointF(0, top + rh - 0.5), QPointF(viewWidth, top + rh - 0.5));
            painter->restore();
            painter->setPen(selected ? roles.text : roles.muted);
        }
        x = columnLeft(widths, 0);
        for (int c = 0; c < columns; ++c) {
            const int mc = modelColumn(c);
            if ((mc == LineTableModel::CpsColumn && idx.data(LineTableModel::CpsTooHighRole).toBool()) ||
                (mc == LineTableModel::WrapsColumn && idx.data(LineTableModel::BadWrapsRole).toBool()))
                painter->fillRect(QRectF(x, top, widths[c], rh), content.gridWarning);
            QString text = m_model->index(row, mc).data().toString();
            if (mc == LineTableModel::NumberColumn && idx.data(LineTableModel::GroupRole).toInt() == 1) {
                const QRectF box(x + 4, top + (rh - 9) / 2, 9, 9);
                painter->drawRect(box);
                painter->drawLine(QPointF(box.left() + 2, box.center().y()), QPointF(box.right() - 2, box.center().y()));
                if (idx.data(LineTableModel::GroupClosedRole).toBool())
                    painter->drawLine(QPointF(box.center().x(), box.top() + 2), QPointF(box.center().x(), box.bottom() - 2));
                text.clear();
            }
            const double indent = mc == LineTableModel::TextColumn && fold != 0 ? kFoldIndent : 0;
            if (indent > 0)
                drawFoldArrow(painter, QRectF(x, top, widths[c], rh), fold == 1);
            if (mc == LineTableModel::TextColumn) {
                drawSpellMarks(painter, QRectF(x + 4 + indent, top, widths[c] - 8 - indent, rh), text,
                               idx.data(LineTableModel::SpellMarksRole).toList());
                if (comparison == 2 && !comparisonColours.isEmpty())
                    drawComparisonMarks(painter, QRectF(x + 4 + indent, top, widths[c] - 8 - indent, rh), text,
                                        idx.data(LineTableModel::ComparisonMarksRole).toList(),
                                        comparisonColours.value(0).value<QColor>());
            }
            const bool onLabel = c == 0 && numberShown && labelColours.size() == 4;
            if (onLabel) {
                painter->save();
                painter->setPen(labelInk(labelColours.value(LineTableModel::labelSlot(
                                             idx.data(LineTableModel::LineStateRole).toInt())).value<QColor>()));
            }
            const bool count = fold == 1 && mc == LineTableModel::TextColumn;
            if (count) {
                painter->save();
                QFont bold = painter->font();
                bold.setBold(true);
                painter->setFont(bold);
                painter->setPen(roles.text);
            }
            painter->drawText(QRectF(x + 4 + indent, top, widths[c] - 8 - indent, rh), Qt::AlignVCenter | Qt::TextSingleLine,
                              QFontMetricsF(painter->font()).elidedText(text, Qt::ElideRight, widths[c] - 8 - indent));
            if (onLabel || count)
                painter->restore();
            if (mc == LineTableModel::TextColumn && idx.data(LineTableModel::UnplacedRunsRole).toBool()) {
                const double right = x + widths[c] - 1;
                const QPointF corner[] = {{right - 7, top + 1}, {right, top + 1}, {right, top + 8}};
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(Qt::NoPen);
                painter->setBrush(roles.accent);
                painter->drawPolygon(corner, 3);
                painter->restore();
            }
            x += widths[c];
        }
        painter->restore();
        if (active && hasActiveFocus()) {
            const double w = viewWidth;
            painter->fillRect(QRectF(1, top + 1, w - 2, 1), roles.focus);
            painter->fillRect(QRectF(1, top + rh - 2, w - 2, 1), roles.focus);
            painter->fillRect(QRectF(1, top + 2, 1, rh - 4), roles.focus);
            painter->fillRect(QRectF(w - 2, top + 2, 1, rh - 4), roles.focus);
        }
        if (selected)
            painter->fillRect(QRectF(0, top, 3, rh), roles.accent);
        if (active) {
            painter->setPen(roles.accent);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(0.5, top + 0.5, viewWidth - 1, rh - 1));
        }
        ++m_lastPainted;
        if (m_markWidth > 0)
            drawBlockMark(painter, top + rh, idx.data(LineTableModel::HiddenBlockRole).toInt(), viewWidth);
    }
    if (m_markWidth > 0)
        drawBlockMark(painter, m_geometry.headerHeight,
                      m_model->headerData(0, Qt::Horizontal, LineTableModel::HeaderBlockRole).toInt(), viewWidth);
    painter->restore();
    emit painted();
}

}
