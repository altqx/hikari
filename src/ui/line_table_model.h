#pragma once

// Read-only Qt projection of a Document's Lines (V1-M). Rows follow document
// order; every row carries its stable LineId, so views, filters and sorting
// never identify a Line by row number. Selection state comes from the
// application and is exposed as roles; the model stores no document content of
// its own beyond the snapshot it was given.

#include "hikari/application/edit_session.h"
#include "hikari/core/document.h"
#include "hikari/core/spelling.h"

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
    // The legacy Grid columns (SubsGridWindow at 20d647c4), in legacy order.
    enum Column {
        NumberColumn,
        LayerColumn,
        StartColumn,
        EndColumn,
        StyleColumn,
        ActorColumn,
        MarginLeftColumn,
        MarginRightColumn,
        MarginVerticalColumn,
        EffectColumn,
        CpsColumn,
        WrapsColumn,
        TextColumn,
        ColumnCount
    };
    // headerData role: whether the column is shown (the Document's format
    // has it and GRID_HIDE_COLUMNS does not hide it).
    static constexpr int ColumnShownRole = Qt::UserRole + 50;
    // headerData roles on section 0: the Document is filtered (legacy
    // IsFiltered: marks are drawn), and the mark before the first Line.
    static constexpr int FilteredRole = Qt::UserRole + 51;
    static constexpr int HeaderBlockRole = Qt::UserRole + 52;
    enum Role {
        LineIdRole = Qt::UserRole + 1,
        CommentRole,
        ActiveRole,
        SelectedRole,
        AnchorRole,
        StartMicrosecondsRole,
        EndMicrosecondsRole,
        CpsTooHighRole, // over 15 characters per second (legacy shorttime)
        BadWrapsRole,   // a wrap over 43 characters, or three wraps or more
        HiddenBlockRole, // after this Line: 1 a hidden block (+), 2 a revealed block (-), 0 none
        DocumentRowRole, // the Line's row in the Document
        GroupRole,       // 0 ordinary, 1 group description, 2 open member, 3 closed member
        GroupClosedRole, // on a description: its members are closed
        // F3: the Text column's spelling marks (legacy SpellErrors): flat
        // inclusive [start, end] pairs of the shown text; none for comments.
        SpellMarksRole,
    };
    // GRID_HIDE_COLUMNS bits (legacy LAYER=1 ... EFFECT=256, CPS=512, WRAPS=8192).
    static int hideBit(Column column);

    explicit LineTableModel(QObject *parent = nullptr);

    // Replaces the projected snapshot. Selection that names Lines no longer
    // present is dropped.
    void setDocument(const core::Document &document);
    void setSelection(const application::Selection &selection, std::optional<core::LineId> anchor);
    void setHiddenColumns(int mask);
    // F3: legacy TextData::Init for the Grid: the marks of a Line's text in
    // the Document's format, misspellings only when `spell` (bracket errors
    // always). Unset: no marks. Applied from the next setDocument.
    using Spelling = std::function<core::legacy::SpellMarks(std::u16string_view text, core::SubtitleFormat format,
                                                             bool spell)>;
    void setSpelling(Spelling spelling) { m_spelling = std::move(spelling); }
    int hiddenColumns() const { return m_hidden; }
    bool columnShown(int column) const;

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
    struct Measures {
        QString cps, wraps; // empty for comments (and CPS in TMPlayer)
        bool cpsTooHigh = false, badWraps = false;
    };
    struct Row {
        core::LineRecord line;
        mutable std::optional<Measures> measures; // measured when first shown, as legacy does
        int blockMark = 0;
        bool groupClosed = false; // descriptions: the first member is closed
        mutable std::optional<QVariantList> spellMarks; // checked when first shown (F3)
    };
    const Measures &measuresOf(const Row &row) const;
    const QVariantList &spellMarksOf(const Row &row) const;
    Spelling m_spelling;
    void emitStateChanged(const std::vector<core::LineId> &ids);

    std::vector<Row> m_rows;
    std::unordered_map<std::uint64_t, int> m_rowById;
    application::Selection m_selection;
    std::optional<core::LineId> m_anchor;
    core::SubtitleFormat m_format = core::SubtitleFormat::Ass;
    bool m_translationMode = false;
    bool m_filtered = false;
    int m_headerBlock = 0;
    int m_hidden = 0;
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
