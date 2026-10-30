#pragma once

#include "hikari/core/document.h"
#include "theme.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QVariantList>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace hikari::ui {

struct GridGeometry {
    double rowHeight = 20;
    double headerHeight = 0;

    int firstVisibleRow(double contentY) const;
    int visibleRowCount(double contentY, double viewportHeight, int rowCount) const;
    int rowAt(double viewportY, double contentY, int rowCount) const;
    double contentHeight(int rowCount) const { return headerHeight + rowCount * rowHeight; }
};

// MUST KILL: state is a raw ComparisonRole int (0 none, 1 match, 2 differs) and colours a list indexed by magic slots
std::optional<QColor> comparisonBackground(int state, bool comment, bool selected, const QVariantList &colours,
                                           const QColor &selection);
QColor selectionOver(const QColor &colour, const QColor &selection);
QColor commentInk(const theme::Roles &roles, const QColor &background);

enum class RunFold { None, Folded, Unfolded };

class LineGrid : public QQuickPaintedItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(HikariGrid)
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(qreal rowHeight READ rowHeight NOTIFY rowHeightChanged)
    Q_PROPERTY(int lastPaintedRowCount READ lastPaintedRowCount NOTIFY painted)
    Q_PROPERTY(QString accessibleName MEMBER m_accessibleName NOTIFY accessibleNameChanged)
    Q_PROPERTY(QString headerToolTip READ headerToolTip NOTIFY headerToolTipChanged)
    Q_PROPERTY(QRectF headerToolTipCell READ headerToolTipCell NOTIFY headerToolTipChanged)

public:
    explicit LineGrid(QQuickItem *parent = nullptr);

    QAbstractItemModel *model() const { return m_model; }
    void setModel(QAbstractItemModel *model);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal y);
    qreal contentHeight() const;
    qreal rowHeight() const { return m_geometry.rowHeight; }
    const GridGeometry &geometry() const { return m_geometry; }

    Q_INVOKABLE int rowAt(qreal y) const;
    Q_INVOKABLE void makeLineVisible(qulonglong id);
    Q_INVOKABLE bool foldsWith(int key, int modifiers) const;
    Q_INVOKABLE void scrollToTopRow(int row) { setContentY(row * m_geometry.rowHeight); }

    std::optional<core::LineId> lineAtRow(int row) const;
    int rowOfLine(core::LineId id) const;
    std::optional<core::LineId> activeLine() const;
    int currentRow() const;
    int currentColumn() const { return m_currentColumn; }
    bool isRowSelected(int row) const;
    QList<int> shownSelectedRows() const;
    int selectedCount() const;
    int hiddenSelectedCount() const;
    QString cellText(int row, int column) const;
    // MUST KILL: reads LineStateRole as magic bits (&3: 1 changed, 2 saved; 4 unconfirmed; 8 bookmarked)
    QString rowStateText(int row) const;
    QString columnTitle(int column) const;
    RunFold runFold(int row) const;
    int columnCount() const;
    QRectF cellRect(int row, int column) const;
    void scrollToRow(int row);
    int lastPaintedRowCount() const { return m_lastPainted; }

    void paint(QPainter *painter) override;

    double markWidth() const { return m_markWidth; }
    QString accessibleName() const { return m_accessibleName; }
    QString headerToolTip() const { return m_headerToolTip; }
    QRectF headerToolTipCell() const { return m_headerToolTipCell; }
    int columnAt(qreal x) const;

signals:
    void hiddenBlockToggleRequested(int documentRow);
    void activeLineFallbackRequested(qulonglong id);
    void groupToggleRequested(qulonglong description);
    void foldToggleRequested(qulonglong first);
    void groupMenuRequested(qulonglong description, qreal x, qreal y);
    void activeLineRequested(qulonglong lineId);
    void extendRequested(int rows);
    void lineClicked(qulonglong lineId, int modifiers, bool endColumn, bool doubleClick);
    void lineDragged(qulonglong lineId);
    void selectAllRequested();
    void contextMenuRequested(qreal x, qreal y);
    void modelChanged();
    void contentYChanged();
    void contentHeightChanged();
    void rowHeightChanged();
    void painted();
    void accessibleNameChanged();
    void headerToolTipChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void modelLayoutChanged();
    void stateChanged();
    void announceState(bool activeMoved);
    void announceFold(qulonglong first);
    void updateRowHeight();
    std::vector<double> columnWidths(double total) const;
    // MUST KILL: mark is a magic int (0 none, 1 hidden block '+', else revealed '-')
    void drawBlockMark(QPainter *painter, double borderY, int mark, double width) const;
    void drawLabel(QPainter *painter, const QRectF &cell, int state, bool draft, const QVariantList &colours) const;
    void drawSpellMarks(QPainter *painter, const QRectF &cell, QString text, const QVariantList &marks) const;
    void drawComparisonMarks(QPainter *painter, const QRectF &cell, const QString &text, const QVariantList &marks,
                             const QColor &outline) const;
    int modelColumn(int column) const;
    bool inEndColumn(qreal x) const;
    bool onFoldArrow(qreal x) const;
    void drawFoldArrow(QPainter *painter, const QRectF &cell, bool folded) const;

    QPointer<QAbstractItemModel> m_model;
    std::vector<QMetaObject::Connection> m_connections;
    GridGeometry m_geometry;
    qreal m_contentY = 0;
    int m_lastPainted = 0;
    int m_currentColumn = -1;
    std::vector<int> m_columns;
    double m_markWidth = 0;
    QString m_accessibleName;
    QString m_headerToolTip;
    QRectF m_headerToolTipCell;
    void setHeaderToolTip(int column);
    void updateColumns();
    std::optional<core::LineId> m_dragLine;
    std::optional<core::LineId> m_announcedActive;
    int m_announcedSelected = 0;
    int m_announcedHidden = 0;
};

}
