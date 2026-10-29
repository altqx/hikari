#pragma once

#include <QAccessible>
#include <QAccessibleObject>

#include <cstdint>
#include <list>
#include <unordered_map>

namespace hikari::ui {

class LineGrid;

void installGridAccessibility();

class LineGridAccessible : public QAccessibleObject,
                           public QAccessibleTableInterface,
                           public QAccessibleSelectionInterface {
public:
    static constexpr std::size_t kCellCacheCapacity = 256;

    explicit LineGridAccessible(LineGrid *grid);
    ~LineGridAccessible() override;

    QAccessibleInterface *parent() const override;
    // Qt's Windows UI Automation bridge hosts an element in its parent's window: without one here each cell hosted the main window.
    QWindow *window() const override;
    int childCount() const override;
    QAccessibleInterface *child(int index) const override;
    int indexOfChild(const QAccessibleInterface *child) const override;
    QAccessibleInterface *childAt(int x, int y) const override;
    QAccessibleInterface *focusChild() const override;
    QString text(QAccessible::Text type) const override;
    QRect rect() const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;
    void *interface_cast(QAccessible::InterfaceType type) override;

    QAccessibleInterface *caption() const override { return nullptr; }
    QAccessibleInterface *summary() const override { return nullptr; }
    QAccessibleInterface *cellAt(int row, int column) const override;
    int columnCount() const override;
    int rowCount() const override;
    QString columnDescription(int column) const override;
    QString rowDescription(int row) const override;
    int selectedCellCount() const override;
    int selectedColumnCount() const override { return 0; }
    int selectedRowCount() const override;
    QList<QAccessibleInterface *> selectedCells() const override;
    QList<int> selectedColumns() const override { return {}; }
    QList<int> selectedRows() const override;
    bool isColumnSelected(int) const override { return false; }
    bool isRowSelected(int row) const override;
    bool selectRow(int) override { return false; }
    bool selectColumn(int) override { return false; }
    bool unselectRow(int) override { return false; }
    bool unselectColumn(int) override { return false; }
    void modelChange(QAccessibleTableModelChangeEvent *) override {}

    int selectedItemCount() const override;
    QList<QAccessibleInterface *> selectedItems() const override;
    bool isSelected(QAccessibleInterface *child) const override;
    bool select(QAccessibleInterface *) override { return false; }
    bool unselect(QAccessibleInterface *) override { return false; }
    bool selectAll() override { return false; }
    bool clear() override { return false; }

    std::size_t cachedCellCount() const { return m_cache.size(); }
    std::size_t cellsCreated() const { return m_created; }

    LineGrid *grid() const;

private:
    struct Key {
        std::uint64_t line;
        int column;
        bool operator==(const Key &) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key &k) const { return std::hash<std::uint64_t>()(k.line) * 31 + k.column; }
    };

    mutable std::list<Key> m_lru;
    mutable std::unordered_map<Key, std::pair<QAccessible::Id, std::list<Key>::iterator>, KeyHash> m_cache;
    mutable std::size_t m_created = 0;
};

}
