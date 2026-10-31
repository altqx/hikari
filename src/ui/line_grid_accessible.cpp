#include "line_grid_accessible.h"

#include "line_grid.h"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QWindow>

#include <algorithm>

namespace hikari::ui {

namespace {

class LineGridCell : public QAccessibleInterface,
                     public QAccessibleTableCellInterface,
                     public QAccessibleActionInterface {
public:
    LineGridCell(LineGrid *grid, core::LineId line, int column) : m_grid(grid), m_line(line), m_column(column) {}

    bool isValid() const override { return m_grid && m_grid->rowOfLine(m_line) >= 0; }
    QObject *object() const override { return nullptr; }
    QWindow *window() const override { return m_grid ? m_grid->window() : nullptr; }
    QAccessibleInterface *parent() const override
    {
        return m_grid ? QAccessible::queryAccessibleInterface(m_grid.data()) : nullptr;
    }
    QAccessibleInterface *child(int) const override { return nullptr; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }

    QString text(QAccessible::Text type) const override
    {
        const int row = rowIndex();
        if (!m_grid || row < 0)
            return {};
        switch (type) {
        case QAccessible::Name:
            return m_grid->cellText(row, m_column);
        case QAccessible::Description:
        {
            const QString state = m_grid->rowStateText(row);
            const QString base = QStringLiteral("%1, %2").arg(m_grid->cellText(row, 0), m_grid->columnTitle(m_column));
            return state.isEmpty() ? base : base + QStringLiteral(", ") + state;
        }
        default:
            return {};
        }
    }
    void setText(QAccessible::Text, const QString &) override {}

    QRect rect() const override
    {
        if (!m_grid || !m_grid->window() || rowIndex() < 0)
            return {};
        const QRectF local = m_grid->cellRect(rowIndex(), m_column);
        const QPointF scene = m_grid->mapToScene(local.topLeft());
        return QRect(m_grid->window()->mapToGlobal(scene.toPoint()), local.size().toSize());
    }
    QAccessible::Role role() const override { return QAccessible::Cell; }
    QAccessible::State state() const override
    {
        QAccessible::State s;
        const int row = rowIndex();
        s.selectable = true;
        s.focusable = true;
        s.readOnly = true;
        s.invisible = row < 0;
        s.selected = row >= 0 && m_grid->isRowSelected(row);
        const RunFold fold = row >= 0 && m_column == m_grid->textColumn() ? m_grid->runFold(row) : RunFold::None;
        s.expandable = fold != RunFold::None;
        s.expanded = fold == RunFold::Unfolded;
        s.focused = row >= 0 && m_grid->hasActiveFocus() && row == m_grid->currentRow() &&
                    m_column == m_grid->currentColumn();
        return s;
    }
    void *interface_cast(QAccessible::InterfaceType type) override
    {
        if (type == QAccessible::TableCellInterface)
            return static_cast<QAccessibleTableCellInterface *>(this);
        if (type == QAccessible::ActionInterface)
            return static_cast<QAccessibleActionInterface *>(this);
        return nullptr;
    }

    bool isSelected() const override { return rowIndex() >= 0 && m_grid->isRowSelected(rowIndex()); }
    QList<QAccessibleInterface *> columnHeaderCells() const override { return {}; }
    QList<QAccessibleInterface *> rowHeaderCells() const override { return {}; }
    int columnIndex() const override { return m_column; }
    int rowIndex() const override { return m_grid ? m_grid->rowOfLine(m_line) : -1; }
    int columnExtent() const override { return 1; }
    int rowExtent() const override { return 1; }
    QAccessibleInterface *table() const override { return parent(); }

    QStringList actionNames() const override { return {setFocusAction()}; }
    void doAction(const QString &name) override
    {
        if (name == setFocusAction() && m_grid)
            emit m_grid->activeLineRequested(m_line.value);
    }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }

private:
    QPointer<LineGrid> m_grid;
    core::LineId m_line;
    int m_column;
};

QAccessibleInterface *gridFactory(const QString &className, QObject *object)
{
    if (className == QLatin1String("hikari::ui::LineGrid"))
        if (auto *grid = qobject_cast<LineGrid *>(object))
            return new LineGridAccessible(grid);
    return nullptr;
}

}

void installGridAccessibility()
{
    static const bool installed = [] {
        QAccessible::installFactory(gridFactory);
        return true;
    }();
    (void)installed;
}

LineGridAccessible::LineGridAccessible(LineGrid *grid) : QAccessibleObject(grid) {}

LineGridAccessible::~LineGridAccessible()
{
    for (auto &[key, entry] : m_cache)
        QAccessible::deleteAccessibleInterface(entry.first);
}

LineGrid *LineGridAccessible::grid() const
{
    return qobject_cast<LineGrid *>(object());
}

QAccessibleInterface *LineGridAccessible::cellAt(int row, int column) const
{
    LineGrid *g = grid();
    if (!g || column < 0 || column >= columnCount())
        return nullptr;
    const auto line = g->lineAtRow(row);
    if (!line)
        return nullptr;
    const Key key{line->value, column};
    if (auto it = m_cache.find(key); it != m_cache.end()) {
        m_lru.splice(m_lru.begin(), m_lru, it->second.second);
        return QAccessible::accessibleInterface(it->second.first);
    }
    if (m_cache.size() >= kCellCacheCapacity) {
        auto oldest = std::prev(m_lru.end());
        if (const auto active = g->activeLine(); active && *oldest == Key{active->value, g->currentColumn()})
            oldest = std::prev(oldest);
        const Key evicted = *oldest;
        m_lru.erase(oldest);
        QAccessible::deleteAccessibleInterface(m_cache.at(evicted).first);
        m_cache.erase(evicted);
    }
    auto *cell = new LineGridCell(g, *line, column);
    const QAccessible::Id id = QAccessible::registerAccessibleInterface(cell);
    m_lru.push_front(key);
    m_cache.emplace(key, std::make_pair(id, m_lru.begin()));
    ++m_created;
    return cell;
}

QAccessibleInterface *LineGridAccessible::parent() const
{
    LineGrid *g = grid();
    if (!g)
        return nullptr;
    // Qt Quick leaves items without an Accessible attachment out of the accessible tree.
    auto *self = const_cast<LineGridAccessible *>(this);
    for (QQuickItem *p = g->parentItem(); p; p = p->parentItem())
        if (QAccessibleInterface *candidate = QAccessible::queryAccessibleInterface(p);
            candidate && candidate->indexOfChild(self) >= 0)
            return candidate;
    // UI Automation takes an element without a parent for a top-level window.
    for (QQuickItem *p = g->parentItem(); p; p = p->parentItem())
        if (QAccessibleInterface *candidate = QAccessible::queryAccessibleInterface(p))
            return candidate;
    return g->window() ? QAccessible::queryAccessibleInterface(g->window()) : nullptr;
}

QWindow *LineGridAccessible::window() const
{
    LineGrid *g = grid();
    return g ? g->window() : nullptr;
}

int LineGridAccessible::childCount() const
{
    return rowCount() * columnCount();
}

QAccessibleInterface *LineGridAccessible::child(int index) const
{
    const int columns = columnCount();
    return index < 0 || columns == 0 ? nullptr : cellAt(index / columns, index % columns);
}

int LineGridAccessible::indexOfChild(const QAccessibleInterface *child) const
{
    if (!child || child->role() != QAccessible::Cell)
        return -1;
    auto *cell = const_cast<QAccessibleInterface *>(child)->tableCellInterface();
    return cell && cell->rowIndex() >= 0 ? cell->rowIndex() * columnCount() + cell->columnIndex() : -1;
}

QAccessibleInterface *LineGridAccessible::childAt(int x, int y) const
{
    LineGrid *g = grid();
    if (!g || !g->window())
        return nullptr;
    const QPointF local = g->mapFromScene(g->window()->mapFromGlobal(QPoint(x, y)));
    const int row = g->rowAt(local.y());
    if (row < 0)
        return nullptr;
    for (int c = 0; c < columnCount(); ++c)
        if (g->cellRect(row, c).contains(local))
            return cellAt(row, c);
    return nullptr;
}

QAccessibleInterface *LineGridAccessible::focusChild() const
{
    LineGrid *g = grid();
    if (!g || !g->hasActiveFocus())
        return nullptr;
    const int row = g->currentRow();
    return row >= 0 ? cellAt(row, g->currentColumn()) : nullptr;
}

QString LineGridAccessible::text(QAccessible::Text type) const
{
    if (type == QAccessible::Name) {
        if (const LineGrid *g = grid(); g && !g->accessibleName().isEmpty())
            return g->accessibleName();
        return QCoreApplication::translate("HikariSub.Grid", "Subtitle lines");
    }
    return {};
}

QRect LineGridAccessible::rect() const
{
    LineGrid *g = grid();
    if (!g || !g->window())
        return {};
    const QPointF scene = g->mapToScene(QPointF(0, 0));
    return QRect(g->window()->mapToGlobal(scene.toPoint()), g->size().toSize());
}

QAccessible::Role LineGridAccessible::role() const
{
    return QAccessible::Table;
}

QAccessible::State LineGridAccessible::state() const
{
    QAccessible::State s;
    LineGrid *g = grid();
    s.focusable = true;
    s.focused = g && g->hasActiveFocus();
    s.multiSelectable = true;
    s.extSelectable = true;
    s.readOnly = true;
    return s;
}

void *LineGridAccessible::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::TableInterface)
        return static_cast<QAccessibleTableInterface *>(this);
    if (type == QAccessible::SelectionInterface)
        return static_cast<QAccessibleSelectionInterface *>(this);
    if (type == QAccessible::ViewportInterface)
        return static_cast<QAccessibleViewportInterface *>(this);
    return QAccessibleObject::interface_cast(type);
}

int LineGridAccessible::columnCount() const
{
    LineGrid *g = grid();
    return g ? g->columnCount() : 0;
}

int LineGridAccessible::rowCount() const
{
    LineGrid *g = grid();
    return g && g->model() ? g->model()->rowCount() : 0;
}

QString LineGridAccessible::columnDescription(int column) const
{
    LineGrid *g = grid();
    return g ? g->columnTitle(column) : QString();
}

QString LineGridAccessible::rowDescription(int row) const
{
    LineGrid *g = grid();
    return g && row >= 0 && row < rowCount()
               ? QCoreApplication::translate("HikariSub.Grid", "Line %1").arg(g->cellText(row, 0))
               : QString();
}

QList<int> LineGridAccessible::selectedRows() const
{
    LineGrid *g = grid();
    return g ? g->shownSelectedRows() : QList<int>{};
}

int LineGridAccessible::selectedRowCount() const
{
    return static_cast<int>(selectedRows().size());
}

int LineGridAccessible::selectedCellCount() const
{
    return selectedRowCount() * columnCount();
}

QList<QAccessibleInterface *> LineGridAccessible::selectedCells() const
{
    QList<QAccessibleInterface *> cells;
    for (int row : selectedRows())
        for (int c = 0; c < columnCount(); ++c)
            cells.append(cellAt(row, c));
    return cells;
}

bool LineGridAccessible::isRowSelected(int row) const
{
    LineGrid *g = grid();
    return g && g->isRowSelected(row);
}

int LineGridAccessible::selectedItemCount() const
{
    return selectedCellCount();
}

QList<QAccessibleInterface *> LineGridAccessible::selectedItems() const
{
    return selectedCells();
}

bool LineGridAccessible::isSelected(QAccessibleInterface *child) const
{
    auto *cell = child ? child->tableCellInterface() : nullptr;
    return cell && cell->isSelected();
}

QSizeF LineGridAccessible::contentSize() const
{
    const int rows = rowCount();
    return rows > 0 ? QSizeF(columnCount(), rows) : QSizeF(0, 0);
}

QPointF LineGridAccessible::position() const
{
    LineGrid *g = grid();
    const int rows = rowCount();
    if (!g || rows == 0)
        return {0, 0};
    return {0, qreal(std::min(g->geometry().firstVisibleRow(g->contentY()), rows - 1)) / rows};
}

QSizeF LineGridAccessible::viewportSize() const
{
    LineGrid *g = grid();
    const int rows = rowCount();
    if (!g || rows == 0)
        return {1, 1};
    return {1, qreal(g->geometry().visibleRowCount(g->contentY(), g->height(), rows)) / rows};
}

void LineGridAccessible::setPosition(QPointF position)
{
    if (LineGrid *g = grid())
        g->scrollToTopRow(static_cast<int>(std::clamp<qreal>(position.y(), 0, 1) * rowCount()));
}

}
