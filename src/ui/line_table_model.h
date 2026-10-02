#pragma once

// Read-only Qt projection of a Document's Lines (V1-M). Rows follow document
// order; every row carries its stable LineId, so views, filters and sorting
// never identify a Line by row number. Selection state comes from the
// application and is exposed as roles; the model stores no document content of
// its own beyond the snapshot it was given.

#include "hikari/application/edit_session.h"
#include "hikari/core/document.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace hikari::ui {

class LineTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { NumberColumn, StartColumn, EndColumn, StyleColumn, ActorColumn, TextColumn, ColumnCount };
    enum Role {
        LineIdRole = Qt::UserRole + 1,
        CommentRole,
        ActiveRole,
        SelectedRole,
        AnchorRole,
        StartMicrosecondsRole,
        EndMicrosecondsRole,
    };

    explicit LineTableModel(QObject *parent = nullptr);

    // Replaces the projected snapshot. Selection that names Lines no longer
    // present is dropped.
    void setDocument(const core::Document &document);
    void setSelection(const application::Selection &selection, std::optional<core::LineId> anchor);

    std::optional<int> rowOf(core::LineId id) const;
    std::optional<core::LineId> lineAt(int row) const;
    const core::LineRecord *recordAt(int row) const; // valid until the next setDocument
    const application::Selection &selection() const { return m_selection; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Row {
        core::LineRecord line;
    };
    void emitStateChanged(const std::vector<core::LineId> &ids);

    std::vector<Row> m_rows;
    std::unordered_map<std::uint64_t, int> m_rowById;
    application::Selection m_selection;
    std::optional<core::LineId> m_anchor;
};

// Filtered view over a LineTableModel. Hidden Lines stay selected: the
// selection is application state, not a property of visible rows.
class LineFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using Predicate = std::function<bool(const core::LineRecord &)>;
    explicit LineFilterModel(QObject *parent = nullptr);

    void setLineModel(LineTableModel *model);
    void setPredicate(Predicate predicate); // empty predicate shows every Line
    int hiddenSelectedCount() const;
    // Proxy row of the shown Line nearest (in document order) to `id`, which
    // may itself be hidden; ties prefer the following Line. -1 if none shown.
    int nearestVisibleRow(core::LineId id) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    LineTableModel *m_lines = nullptr;
    Predicate m_predicate;
};

} // namespace hikari::ui
