#pragma once

// Painted subtitle Grid viewport (V1-G; ADR 0003). Paints only the rows inside
// the viewport, straight from a Line model, with no per-row QML items. Row
// identity comes from the model's LineIdRole; painting never stores content.
// Row commands, shell integration and accessibility are separate cards.

#include "hikari/core/document.h"

#include <QAbstractItemModel>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace hikari::ui {

// Pure viewport geometry: which rows a vertical window shows.
struct GridGeometry {
    double rowHeight = 20;
    double headerHeight = 0;

    int firstVisibleRow(double contentY) const;
    // Rows intersecting [contentY, contentY + viewportHeight) below the header.
    int visibleRowCount(double contentY, double viewportHeight, int rowCount) const;
    // Row under a viewport y coordinate, or -1 for the header or past the last row.
    int rowAt(double viewportY, double contentY, int rowCount) const;
    double contentHeight(int rowCount) const { return headerHeight + rowCount * rowHeight; }
};

class LineGrid : public QQuickPaintedItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(HikariGrid)
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(qreal rowHeight READ rowHeight NOTIFY rowHeightChanged)
    Q_PROPERTY(int lastPaintedRowCount READ lastPaintedRowCount NOTIFY painted)

public:
    explicit LineGrid(QQuickItem *parent = nullptr);

    QAbstractItemModel *model() const { return m_model; }
    void setModel(QAbstractItemModel *model);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal y); // clamped to [0, contentHeight - height]
    qreal contentHeight() const;
    qreal rowHeight() const { return m_geometry.rowHeight; }
    const GridGeometry &geometry() const { return m_geometry; }

    Q_INVOKABLE int rowAt(qreal y) const;

    // Identity and state, resolved through a filter proxy when there is one.
    std::optional<core::LineId> lineAtRow(int row) const;
    int rowOfLine(core::LineId id) const;             // -1 when not shown
    std::optional<core::LineId> activeLine() const;    // application state
    int currentRow() const;                            // row of the active Line, or -1
    int currentColumn() const { return m_currentColumn; }
    bool isRowSelected(int row) const;
    QList<int> shownSelectedRows() const;              // sorted rows of shown selected Lines
    int selectedCount() const;                         // all selected Lines, shown or hidden
    int hiddenSelectedCount() const;
    QString cellText(int row, int column) const;
    QString columnTitle(int column) const;
    int columnCount() const;
    QRectF cellRect(int row, int column) const;        // item coordinates
    void scrollToRow(int row);
    // Rows painted by the last paint() call: evidence that only the viewport is drawn.
    int lastPaintedRowCount() const { return m_lastPainted; }

    void paint(QPainter *painter) override;

signals:
    // Keyboard navigation asks the application to move the active Line; the
    // Grid never changes selection itself.
    void activeLineRequested(qulonglong lineId);
    void modelChanged();
    void contentYChanged();
    void contentHeightChanged();
    void rowHeightChanged();
    void painted();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    void modelLayoutChanged();
    void stateChanged();
    void announceState(bool activeMoved);
    void updateRowHeight();
    std::vector<double> columnWidths(double total) const;

    QPointer<QAbstractItemModel> m_model;
    std::vector<QMetaObject::Connection> m_connections;
    GridGeometry m_geometry;
    qreal m_contentY = 0;
    int m_lastPainted = 0;
    int m_currentColumn = 5; // Text
    std::optional<core::LineId> m_announcedActive;
    int m_announcedSelected = 0;
    int m_announcedHidden = 0;
};

} // namespace hikari::ui
