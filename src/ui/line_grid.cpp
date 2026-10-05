#include "line_grid.h"

#include "line_grid_accessible.h"
#include "line_table_model.h"
#include "theme.h"

#include <QAbstractProxyModel>
#include <QAccessible>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
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
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton); // selection gestures (G1), context menu (G3)
    setFlag(ItemIsFocusScope, false);
    installGridAccessibility();
    updateRowHeight();
    connect(this, &QQuickItem::heightChanged, this, [this] { setContentY(m_contentY); });
    // K2: painted in the theme's colours, live.
    theme::onChanged(this, [this] { update(); });
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
                update();
            }),
            connect(m_model, &QAbstractItemModel::dataChanged, this,
                    [this, repaint](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                        repaint();
                        if (roles.contains(LineTableModel::ActiveRole) || roles.contains(LineTableModel::SelectedRole))
                            stateChanged();
                    }),
        };
    }
    emit modelChanged();
    updateColumns();
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

void LineGrid::updateColumns()
{
    // The model column the caret was on stays current when it is still shown.
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
        words << tr("changed");
    else if ((state & 3) == 2)
        words << tr("changed, saved");
    if (state & 4)
        words << tr("unconfirmed");
    if (state & 8)
        words << tr("bookmarked");
    return words.join(QStringLiteral(", "));
}

QString LineGrid::columnTitle(int column) const
{
    return m_model ? m_model->headerData(modelColumn(column), Qt::Horizontal).toString() : QString();
}

int LineGrid::columnCount() const
{
    return m_model ? static_cast<int>(m_columns.size()) : 0;
}

QRectF LineGrid::cellRect(int row, int column) const
{
    const auto widths = columnWidths(width() - m_markWidth);
    double x = m_markWidth;
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

void LineGrid::makeLineVisible(qulonglong id)
{
    // SubsGridPreview.cpp:76-89: erow the active row, panel rows
    // h / (GridHeight + 1) (the header row included, as legacy counted it).
    const int row = rowOfLine(core::LineId{id});
    if (row < 0 || m_geometry.rowHeight <= 0)
        return;
    const int rows = static_cast<int>(height() / m_geometry.rowHeight);
    const int top = static_cast<int>(m_contentY / m_geometry.rowHeight);
    if (top > row || top + rows < row + 2)
        setContentY(std::max(0, row - rows / 2 + 1) * m_geometry.rowHeight);
}

void LineGrid::keyPressEvent(QKeyEvent *event)
{
    const int rows = m_model ? m_model->rowCount() : 0;
    if (rows == 0) {
        event->ignore();
        return;
    }
    const int page = std::max(1, m_geometry.visibleRowCount(m_contentY, height(), rows) - 1);
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

void LineGrid::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        forceActiveFocus(Qt::MouseFocusReason);
        // Legacy: a right click on an unselected row selects it first.
        const int row = rowAt(event->position().y());
        if (row >= 0 && m_model->index(row, 0).data(LineTableModel::GroupRole).toInt() == 1) {
            emit groupMenuRequested(lineAtRow(row)->value, event->position().x(), event->position().y());
            event->accept();
            return;
        }
        if (const auto id = row >= 0 ? lineAtRow(row) : std::nullopt; id && !isRowSelected(row))
            emit activeLineRequested(id->value); // a plain selection, not a press (V6: the video stays)
        emit contextMenuRequested(event->position().x(), event->position().y());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    if (m_markWidth > 0 && event->position().x() < m_markWidth) {
        // The mark on the border nearest the click (legacy: half a row down).
        const int row = rowAt(event->position().y() + m_geometry.rowHeight / 2) - 1;
        int mark = 0, documentRow = -1;
        if (row < 0) {
            mark = m_model->headerData(0, Qt::Horizontal, LineTableModel::HeaderBlockRole).toInt();
        } else if (row < m_model->rowCount()) {
            const QModelIndex idx = m_model->index(row, 0);
            mark = idx.data(LineTableModel::HiddenBlockRole).toInt();
            documentRow = idx.data(LineTableModel::DocumentRowRole).toInt();
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
        emit groupToggleRequested(id->value); // legacy: the click only opens or closes
        event->accept();
        return;
    }
    m_dragLine = id;
    if (id)
        emit lineClicked(id->value, static_cast<int>(event->modifiers()), inEndColumn(event->position().x()), false);
    event->accept();
}

// V6: legacy's LeftDClick on a Line moves the video to it (SubsGridWindow.cpp:1647,
// SetVideoLineTime); the press before it has already selected the Line.
void LineGrid::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || (m_markWidth > 0 && event->position().x() < m_markWidth)) {
        event->ignore();
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
    const auto widths = columnWidths(width() - m_markWidth);
    double left = m_markWidth;
    for (std::size_t c = 0; c < widths.size(); ++c) {
        if (x >= left && x < left + widths[c])
            return modelColumn(static_cast<int>(c)) == LineTableModel::EndColumn;
        left += widths[c];
    }
    return false;
}

void LineGrid::mouseMoveEvent(QMouseEvent *event)
{
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
    m_dragLine.reset();
    event->accept();
}

void LineGrid::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    update(); // the focused cell's ring
    announceState(true);
}

void LineGrid::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    update();
}

void LineGrid::stateChanged()
{
    // A filtered-out current Line moves to the nearest visible row; hidden
    // selection itself is kept and reported (accepted announcement policy).
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
    // Fixed widths per model column; Text takes the rest.
    const QFontMetricsF m(QFont{});
    auto fit = [&](const char *sample, double pad) { return m.horizontalAdvance(QLatin1String(sample)) + pad; };
    std::vector<double> w;
    double used = 0;
    for (const int c : m_columns) {
        double width = 0;
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
        default: width = -1; break; // Text (and E5's Translation)
        }
        w.push_back(width);
        used += std::max(0.0, width);
    }
    // E5: with the original shown, "Original text" and "Translation" take
    // half of the rest each (SubsGridWindow.cpp:481-485).
    const auto rest = static_cast<double>(std::count_if(w.begin(), w.end(), [](double v) { return v < 0; }));
    for (double &v : w)
        if (v < 0)
            v = std::max(40.0, (total - used) / rest);
    return w;
}

// Legacy SubsGridWindow: a 9px box on the border below a row, with a line
// across; '+' for a hidden block, '-' for a revealed one.
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

// F3: legacy TextData::DrawMisspells: behind each error range the width of
// its text (trailing spaces trimmed), from the width of the text before it;
// the full row height, in GRID_SPELLCHECKER's colour (dark default).
std::optional<QColor> comparisonBackground(int state, bool comment, bool selected, const QVariantList &colours,
                                           const QColor &selection)
{
    if (state != 1 && state != 2)
        return std::nullopt;
    // kol = comparison ? ComparisonBG : ComparisonBGMatch, and the comment
    // colours on a Comment (SubsGridWindow.cpp:422-426).
    const int slot = comment ? (state == 2 ? 3 : 4) : (state == 2 ? 1 : 2);
    QColor colour = colours.value(slot).value<QColor>();
    if (!selected)
        return colour;
    // GetColorWithAlpha(seldial, kol), in its integer arithmetic.
    const int r = selection.red(), g = selection.green(), b = selection.blue(), invA = 0xFF - selection.alpha();
    return QColor(colour.red() * invA / 0xFF + (r - invA * r / 0xFF), colour.green() * invA / 0xFF + (g - invA * g / 0xFF),
                  colour.blue() * invA / 0xFF + (b - invA * b / 0xFF));
}

// E6: legacy paints column 0 of every Line in its label colour by State
// (SubsGridWindow.cpp:478-479, 495: j == 0 && !isHeadline ? label : kol),
// over selection and comparison colours alike. The rewrite adds a shape for
// the changed-Line mark, so it does not rest on colour alone (subtitle-grid.md):
// a filled dot for a changed Line, a ring for a changed and saved one
// (E6-mark-shape). The mark takes the theme layer's text role, or its field
// role where text contrasts less with the label colour.
void LineGrid::drawLabel(QPainter *painter, const QRectF &cell, int state, const QVariantList &colours) const
{
    const QColor label = colours.value(LineTableModel::labelSlot(state)).value<QColor>();
    if (colours.size() == 4)
        painter->fillRect(cell, label);
    const int changed = state & 3;
    if (!changed)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const auto &roles = theme::current().roles;
    QColor mark = roles.text;
    if (label.isValid() && theme::contrastRatio(roles.field, label) > theme::contrastRatio(mark, label))
        mark = roles.field;
    // The centre on a device pixel corner, so the ring's 1-wide pen falls on
    // whole pixels: at a fractional column edge or row height (other fonts,
    // another platform) it would otherwise smear over two half-tone pixels.
    const QTransform &device = painter->deviceTransform();
    const QPointF centre = device.map(QPointF(cell.right() - 6.5, cell.center().y()));
    const QPointF snapped = device.inverted().map(QPointF(std::round(centre.x()), std::round(centre.y())));
    const QRectF dot(snapped.x() - 2.5, snapped.y() - 2.5, 5, 5);
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

// R1: legacy SubsGridWindow.cpp:508-528. Each run of differing characters
// (inclusive offsets into the shown text, wxString::SubString) is drawn in
// GRID_COMPARISON_OUTLINE one pixel left, right, above and below where the
// text itself is drawn afterwards, outlining it; a lone space is drawn as
// "_" so that it shows; a run outside the text draws nothing.
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
        const int start = marks[m].toInt(), end = marks[m + 1].toInt();
        if (start < 0 || start >= text.size() || end < start)
            continue;
        QString run = text.mid(start, end - start + 1);
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
    painter->save();
    painter->setClipRect(cell, Qt::IntersectClip);
    for (qsizetype s = 0; s + 1 < marks.size(); s += 2) {
        const int start = marks[s].toInt(), end = marks[s + 1].toInt();
        QString error = text.mid(start, end - start + 1);
        while (!error.isEmpty() && error.back().isSpace() && error.back().unicode() < 127)
            error.chop(1);
        const double before = start > 0 ? metrics.horizontalAdvance(text.left(start)) : 0;
        painter->fillRect(QRectF(cell.x() + before, cell.y(), metrics.horizontalAdvance(error), cell.height()),
                          theme::current().content.spellcheck);
    }
    painter->restore();
}

void LineGrid::paint(QPainter *painter)
{
    const QRectF bounds = boundingRect();
    // K2: the theme's roles: rows on the panel surface (every other one a
    // shade apart), the header raised with secondary text, a selected row on
    // the selected background with the leading accent marker, the active
    // Line outlined in the accent, a Comment in secondary text. While the
    // Grid has keyboard focus its focused cell, the active Line's row,
    // carries the focus ring (visual-language.md, "Keyboard focus"): 2 wide
    // in the focus role, the text colour, just inside the accent outline,
    // under a selected row's marker.
    const auto &roles = theme::current().roles;
    const auto &content = theme::current().content;
    painter->fillRect(bounds, roles.panel);
    m_lastPainted = 0;
    const int rows = m_model ? m_model->rowCount() : 0;
    const int columns = columnCount();
    const auto widths = columnWidths(bounds.width() - m_markWidth);
    const double rh = m_geometry.rowHeight;

    // Header.
    painter->fillRect(QRectF(0, 0, bounds.width(), m_geometry.headerHeight), roles.raised);
    painter->setPen(roles.muted);
    double x = m_markWidth;
    for (int c = 0; c < columns; ++c) {
        painter->drawText(QRectF(x + 4, 0, widths[c] - 8, m_geometry.headerHeight), Qt::AlignVCenter,
                          columnTitle(c));
        x += widths[c];
    }

    const int first = m_geometry.firstVisibleRow(m_contentY);
    const int count = m_geometry.visibleRowCount(m_contentY, bounds.height(), rows);
    const QVariantList comparisonColours =
        m_model ? m_model->headerData(0, Qt::Horizontal, LineTableModel::ComparisonColoursRole).toList() : QVariantList();
    const QVariantList labelColours =
        m_model ? m_model->headerData(0, Qt::Horizontal, LineTableModel::LabelColoursRole).toList() : QVariantList();
    const bool numberShown = columns > 0 && modelColumn(0) == LineTableModel::NumberColumn;
    painter->save();
    painter->setClipRect(QRectF(0, m_geometry.headerHeight, bounds.width(), bounds.height() - m_geometry.headerHeight));
    for (int row = first; row < first + count; ++row) {
        const double top = m_geometry.headerHeight + row * rh - m_contentY;
        const QModelIndex idx = m_model->index(row, 0);
        const bool selected = idx.data(LineTableModel::SelectedRole).toBool();
        const bool active = idx.data(LineTableModel::ActiveRole).toBool();
        const bool comment = idx.data(LineTableModel::CommentRole).toBool();
        QColor background = row % 2 ? content.gridAlternate : roles.panel;
        if (selected)
            background = roles.select;
        const int comparison = idx.data(LineTableModel::ComparisonRole).toInt();
        painter->fillRect(QRectF(0, top, bounds.width(), rh), background);
        if (const auto compared =
                comparisonBackground(comparison, comment, selected, comparisonColours, content.comparisonSelection)) {
            // Legacy paints column 0, the number, in its label colour and the
            // other columns in kol (SubsGridWindow.cpp:495, j == 0 && !isHeadline
            // ? label : kol); the number cell takes its label colour (E6).
            const double from = m_markWidth + (numberShown ? widths[0] : 0);
            painter->fillRect(QRectF(from, top, bounds.width() - from, rh), *compared);
        }
        if (numberShown)
            drawLabel(painter, QRectF(m_markWidth, top, widths[0], rh), idx.data(LineTableModel::LineStateRole).toInt(),
                      labelColours);
        painter->setPen(comment ? roles.muted : roles.text);
        x = m_markWidth;
        for (int c = 0; c < columns; ++c) {
            const int mc = modelColumn(c);
            // Legacy marks a fast CPS and bad wraps on their cells.
            if ((mc == LineTableModel::CpsColumn && idx.data(LineTableModel::CpsTooHighRole).toBool()) ||
                (mc == LineTableModel::WrapsColumn && idx.data(LineTableModel::BadWrapsRole).toBool()))
                painter->fillRect(QRectF(x, top, widths[c], rh), content.gridWarning);
            QString text = m_model->index(row, mc).data().toString();
            if (mc == LineTableModel::NumberColumn && idx.data(LineTableModel::GroupRole).toInt() == 1) {
                // A group description: [+] closed, [-] open.
                const QRectF box(x + 4, top + (rh - 9) / 2, 9, 9);
                painter->drawRect(box);
                painter->drawLine(QPointF(box.left() + 2, box.center().y()), QPointF(box.right() - 2, box.center().y()));
                if (idx.data(LineTableModel::GroupClosedRole).toBool())
                    painter->drawLine(QPointF(box.center().x(), box.top() + 2), QPointF(box.center().x(), box.bottom() - 2));
                text.clear();
            }
            if (mc == LineTableModel::TextColumn) {
                drawSpellMarks(painter, QRectF(x + 4, top, widths[c] - 8, rh), text,
                               idx.data(LineTableModel::SpellMarksRole).toList());
                if (comparison == 2 && !comparisonColours.isEmpty())
                    drawComparisonMarks(painter, QRectF(x + 4, top, widths[c] - 8, rh), text,
                                        idx.data(LineTableModel::ComparisonMarksRole).toList(),
                                        comparisonColours.value(0).value<QColor>());
            }
            painter->drawText(QRectF(x + 4, top, widths[c] - 8, rh), Qt::AlignVCenter | Qt::TextSingleLine,
                              QFontMetricsF(painter->font()).elidedText(text, Qt::ElideRight, widths[c] - 8));
            x += widths[c];
        }
        if (active && hasActiveFocus()) {
            const double w = bounds.width();
            painter->fillRect(QRectF(1, top + 1, w - 2, 2), roles.focus);
            painter->fillRect(QRectF(1, top + rh - 3, w - 2, 2), roles.focus);
            painter->fillRect(QRectF(1, top + 3, 2, rh - 6), roles.focus);
            painter->fillRect(QRectF(w - 3, top + 3, 2, rh - 6), roles.focus);
        }
        if (selected)
            painter->fillRect(QRectF(0, top, 3, rh), roles.accent);
        if (active) {
            painter->setPen(roles.accent);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(0.5, top + 0.5, bounds.width() - 1, rh - 1));
        }
        ++m_lastPainted;
        if (m_markWidth > 0)
            drawBlockMark(painter, top + rh, idx.data(LineTableModel::HiddenBlockRole).toInt(), bounds.width());
    }
    if (m_markWidth > 0)
        drawBlockMark(painter, m_geometry.headerHeight,
                      m_model->headerData(0, Qt::Horizontal, LineTableModel::HeaderBlockRole).toInt(), bounds.width());
    painter->restore();
    emit painted();
}

} // namespace hikari::ui
