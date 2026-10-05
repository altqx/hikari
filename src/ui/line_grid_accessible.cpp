#include "line_grid_accessible.h"

#include "line_grid.h"

#include <QQuickWindow>
#include <QWindow>

namespace hikari::ui {

namespace {

// A virtual table cell. It holds the Line's identity, not a row: its row index
// is looked up on every query, so filtering or reordering never retargets it.
class LineGridCell : public QAccessibleInterface,
                     public QAccessibleTableCellInterface,
                     public QAccessibleActionInterface {
public:
    LineGridCell(LineGrid *grid, core::LineId line, int column) : m_grid(grid), m_line(line), m_column(column) {}

    bool isValid() const override { return m_grid && m_grid->rowOfLine(m_line) >= 0; }
    QObject *object() const override { return nullptr; } // not a paint object or delegate
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
            return m_grid->cellText(row, m_column); // the cell first: its displayed text
        case QAccessible::Description:
            return QStringLiteral("%1, %2").arg(m_grid->cellText(row, 0), m_grid->columnTitle(m_column));
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

    // QAccessibleTableCellInterface
    bool isSelected() const override { return rowIndex() >= 0 && m_grid->isRowSelected(rowIndex()); }
    QList<QAccessibleInterface *> columnHeaderCells() const override { return {}; }
    QList<QAccessibleInterface *> rowHeaderCells() const override { return {}; }
    int columnIndex() const override { return m_column; }
    int rowIndex() const override { return m_grid ? m_grid->rowOfLine(m_line) : -1; }
    int columnExtent() const override { return 1; }
    int rowExtent() const override { return 1; }
    QAccessibleInterface *table() const override { return parent(); }

    // QAccessibleActionInterface: focusing asks the application to make this
    // Line active, exactly as keyboard navigation does.
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

} // namespace

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
        m_lru.splice(m_lru.begin(), m_lru, it->second.second); // most recently used
        return QAccessible::accessibleInterface(it->second.first);
    }
    // Bounded cache: evict the least recently used cell.
    if (m_cache.size() >= kCellCacheCapacity) {
        const Key oldest = m_lru.back();
        m_lru.pop_back();
        QAccessible::deleteAccessibleInterface(m_cache.at(oldest).first);
        m_cache.erase(oldest);
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
    // The nearest ancestor that lists the Grid among its accessible
    // children (Qt Quick skips items without an Accessible attachment).
    auto *self = const_cast<LineGridAccessible *>(this);
    for (QQuickItem *p = g->parentItem(); p; p = p->parentItem())
        if (QAccessibleInterface *candidate = QAccessible::queryAccessibleInterface(p);
            candidate && candidate->indexOfChild(self) >= 0)
            return candidate;
    // Otherwise the nearest accessible ancestor, and at last the window: an
    // element without a parent counts as a top-level window to UI Automation.
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
        // R2: a Grid may name itself (the reference tray's).
        if (const LineGrid *g = grid(); g && !g->accessibleName().isEmpty())
            return g->accessibleName();
        return QObject::tr("Subtitle lines");
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
    return g && row >= 0 && row < rowCount() ? QObject::tr("Line %1").arg(g->cellText(row, 0)) : QString();
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

} // namespace hikari::ui
