#pragma once

// Read-only Qt projection of a Document's Lines (V1-M). Rows follow document
// order; every row carries its stable LineId, so views, filters and sorting
// never identify a Line by row number. Selection state comes from the
// application and is exposed as roles; the model stores no document content of
// its own beyond the snapshot it was given.

#include "hikari/application/edit_session.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/application/subtitle_comparison.h"
#include "hikari/core/document.h"
#include "hikari/core/spelling.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QSortFilterProxyModel>

#include <array>
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
        // E5: "Translation", shown with the original (legacy showOriginal,
        // SubsGridWindow.cpp:329-330 and 415-416); TextColumn is then
        // "Original text".
        TranslationColumn,
        ColumnCount
    };
    // headerData role: whether the column is shown (the Document's format
    // has it and GRID_HIDE_COLUMNS does not hide it).
    static constexpr int ColumnShownRole = Qt::UserRole + 50;
    // headerData roles on section 0: the Document is filtered (legacy
    // IsFiltered: marks are drawn), and the mark before the first Line.
    static constexpr int FilteredRole = Qt::UserRole + 51;
    static constexpr int HeaderBlockRole = Qt::UserRole + 52;
    // headerData role on section 0 (R1): the comparison colours, a list of
    // GRID_COMPARISON_OUTLINE, _BACKGROUND_NOT_MATCH, _BACKGROUND_MATCH,
    // _COMMENT_BACKGROUND_NOT_MATCH and _COMMENT_BACKGROUND_MATCH.
    static constexpr int ComparisonColoursRole = Qt::UserRole + 53;
    // headerData role on section 0 (E6): the label colours, a list of
    // GRID_LABEL_NORMAL, _MODIFIED, _SAVED and _DOUBTFUL.
    static constexpr int LabelColoursRole = Qt::UserRole + 54;
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
        // R1: the Line's comparison state (legacy SubsGridWindow.cpp:419-420):
        // 0 not compared or no table, 1 a match (equal text), 2 a mismatch.
        ComparisonRole,
        // R1: the differing characters, flat inclusive [start, end] pairs
        // (legacy lineCompare without its leading 1).
        ComparisonMarksRole,
        // E6: the Line's legacy State (SubsGridWindow.cpp:343, 478-479): 1
        // changed, 2 changed and saved (the session's changeState), 4
        // Unconfirmed, 8 bookmarked, or'ed together; 0 none.
        LineStateRole,
    };
    // E6: the number cell's colour index in LabelColoursRole for a State
    // (SubsGridWindow.cpp:478-479): 0 normal, 1 modified, 2 saved, 3 doubtful
    // (any other State, Unconfirmed or bookmarked included).
    static int labelSlot(int state);
    // GRID_HIDE_COLUMNS bits (legacy LAYER=1 ... EFFECT=256, CPS=512, WRAPS=8192).
    static int hideBit(Column column);

    explicit LineTableModel(QObject *parent = nullptr);

    // Replaces the projected snapshot. Selection that names Lines no longer
    // present is dropped.
    void setDocument(const core::Document &document);
    // R1: with the Document's comparison table (legacy SubsGrid::Comparison,
    // by Document row), or none.
    void setDocument(const core::Document &document, const std::vector<application::LineComparison> *comparison);
    using ComparisonColours = std::array<QColor, 5>;
    // R1: the comparison colours are fixed per theme, not settings: the
    // current theme's (K2, theme::content: Dark and Light are legacy's theme
    // defaults, config.cpp:427-431, LoadDefaultColors(dark); the
    // high-contrast themes' are drawn to match). The model follows the theme.
    static ComparisonColours themeComparisonColours();
    void setComparisonColours(const ComparisonColours &colours);
    // E6: the label colours are fixed per theme as well: legacy's theme
    // defaults (config.cpp:422-425), the dark ones for Dark and High
    // contrast black, the light ones for Light and High contrast white. The
    // model follows the theme (K2).
    using LabelColours = std::array<QColor, 4>;
    static LabelColours themeLabelColours(bool dark);
    static LabelColours themeLabelColours();
    void setLabelColours(const LabelColours &colours);
    // E6: a Line's changed-Line mark, 0, 1 or 2 (EditSession::changeState),
    // read at the next setDocument. Unset: 0.
    using ChangeState = std::function<int(const core::LineRecord &)>;
    void setChangeState(ChangeState changeState) { m_changeState = std::move(changeState); }
    // E6: GLOBAL_HIDE_TAGS (GRID_HIDE_TAGS): the Text column shows each
    // override block (SRT: each <...>) as `swap` (GRID_TAGS_SWAP_CHARACTER).
    void setHideTags(bool hide, const QString &swap);
    bool hideTags() const { return m_hideTags; }
    void setSelection(const application::Selection &selection, std::optional<core::LineId> anchor);
    void setHiddenColumns(int mask);
    // E5: legacy SubsGrid::showOriginal (application::OriginalColumns).
    // Applied from the next setDocument.
    void setShowOriginal(bool show)
    {
        if (show == m_showOriginal)
            return;
        m_showOriginal = show;
        for (const Row &row : m_rows)
            row.shownText.reset(); // E6's cache of the Text column
    }
    bool showOriginal() const { return m_showOriginal; }
    // F3: legacy TextData::Init for the Grid: the marks of a Line's text in
    // the Document's format, misspellings only when `spell` (bracket errors
    // always). Unset: no marks. Applied from the next setDocument.
    using Spelling = std::function<core::legacy::SpellMarks(std::u16string_view text, core::SubtitleFormat format,
                                                             bool spell)>;
    void setSpelling(Spelling spelling);
    // E6: with GLOBAL_HIDE_TAGS on, the marks are of the text with its tags
    // swapped, each block counted as `replaceTagsLen` characters (legacy
    // TextData::Init's tagReplaceLen); -1 otherwise.
    using TagSpelling = std::function<core::legacy::SpellMarks(std::u16string_view text, core::SubtitleFormat format,
                                                                bool spell, int replaceTagsLen)>;
    void setSpelling(TagSpelling spelling) { m_spelling = std::move(spelling); }
    int hiddenColumns() const { return m_hidden; }
    bool columnShown(int column) const;
    // E4: the Times/Frames switch (SubsGrid::ChangeTimeDisplay): with an exact
    // timebase Start shows the frame at or after it and End the frame before
    // it (SubsGridWindow.cpp:348-357); nullopt shows times.
    void setFrameTimebase(std::optional<application::LegacyTimebase> frames);

    std::optional<int> rowOf(core::LineId id) const;
    std::optional<core::LineId> lineAt(int row) const;
    const core::LineRecord *recordAt(int row) const; // valid until the next setDocument
    const application::Selection &selection() const { return m_selection; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    // O5: the headings in the interface language again.
    void retranslate() { emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1); }
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
        int comparison = 0;          // R1: ComparisonRole
        QVariantList comparisonMarks; // R1: ComparisonMarksRole
        int state = 0;               // E6: LineStateRole
        mutable std::optional<QString> shownText; // E6: the Text column, tags swapped when hidden
    };
    const QString &shownTextOf(const Row &row) const;
    bool tagsSwapped() const { return m_hideTags && !m_translationMode; }
    const Measures &measuresOf(const Row &row) const;
    const QVariantList &spellMarksOf(const Row &row) const;
    TagSpelling m_spelling;
    ChangeState m_changeState;
    bool m_hideTags = false;
    QString m_tagSwap;
    void emitStateChanged(const std::vector<core::LineId> &ids);

    std::vector<Row> m_rows;
    std::unordered_map<std::uint64_t, int> m_rowById;
    application::Selection m_selection;
    std::optional<core::LineId> m_anchor;
    core::SubtitleFormat m_format = core::SubtitleFormat::Ass;
    bool m_translationMode = false;
    bool m_showOriginal = false;
    bool m_filtered = false;
    int m_headerBlock = 0;
    int m_hidden = 0;
    std::optional<application::LegacyTimebase> m_frames;
    ComparisonColours m_comparisonColours = themeComparisonColours();
    LabelColours m_labelColours = themeLabelColours();
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
