#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/application/subtitle_comparison.h"
#include "hikari/core/document.h"
#include "hikari/core/spelling.h"
#include "hikari/core/style.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QSortFilterProxyModel>

#include <array>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hikari::ui {

class LineTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
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
        TranslationColumn,
        ColumnCount
    };
    static constexpr int ColumnShownRole = Qt::UserRole + 50;
    static constexpr int FilteredRole = Qt::UserRole + 51;
    static constexpr int HeaderBlockRole = Qt::UserRole + 52;
    // MUST KILL: ComparisonColoursRole is an untyped list whose order (outline, mismatch, match, comment mismatch, comment match) is a hidden contract
    static constexpr int ComparisonColoursRole = Qt::UserRole + 53;
    // MUST KILL: LabelColoursRole is an untyped list whose order (normal, modified, saved, doubtful) is a hidden contract
    static constexpr int LabelColoursRole = Qt::UserRole + 54;
    enum Role {
        LineIdRole = Qt::UserRole + 1,
        CommentRole,
        ActiveRole,
        SelectedRole,
        AnchorRole,
        StartMicrosecondsRole,
        EndMicrosecondsRole,
        CpsTooHighRole,
        BadWrapsRole,
        // MUST KILL: HiddenBlockRole is a magic int (0 none, 1 hidden block, 2 revealed block)
        HiddenBlockRole,
        DocumentRowRole,
        // MUST KILL: GroupRole is a magic int (0 ordinary, 1 description, 2 open member, 3 closed member)
        GroupRole,
        GroupClosedRole,
        // MUST KILL: SpellMarksRole is a flat list of inclusive [start, end] pairs
        SpellMarksRole,
        // MUST KILL: ComparisonRole is a magic int (0 not compared, 1 match, 2 mismatch)
        ComparisonRole,
        // MUST KILL: ComparisonMarksRole is a flat list of inclusive [start, end] pairs
        ComparisonMarksRole,
        // MUST KILL: LineStateRole is magic bit flags (1 changed, 2 saved, 4 Unconfirmed, 8 bookmarked)
        LineStateRole,
        DraftRole,
        UnplacedRunsRole,
        // MUST KILL: FoldRole is a magic int (0 not a run's first row, 1 folded, 2 unfolded)
        FoldRole,
        FoldCountRole,
        HiddenBlockRowRole,
    };
    // MUST KILL: labelSlot returns a magic int (0 normal, 1 modified, 2 saved, 3 doubtful)
    static int labelSlot(int state);
    static int hideBit(Column column);
    static QString headerToolTip(int section);

    explicit LineTableModel(QObject *parent = nullptr);

    void setDocument(const core::Document &document);
    void setDocument(const core::Document &document, const std::vector<application::LineComparison> *comparison);
    using ComparisonColours = std::array<QColor, 5>;
    static ComparisonColours themeComparisonColours();
    void setComparisonColours(const ComparisonColours &colours);
    using LabelColours = std::array<QColor, 4>;
    static LabelColours themeLabelColours(bool dark);
    static LabelColours themeLabelColours();
    void setLabelColours(const LabelColours &colours);
    // MUST KILL: ChangeState returns a magic int (0 unchanged, 1 changed, 2 changed and saved)
    using ChangeState = std::function<int(const core::LineRecord &)>;
    void setChangeState(ChangeState changeState) { m_changeState = std::move(changeState); }
    void setHideTags(bool hide, const QString &swap);
    bool hideTags() const { return m_hideTags; }
    void setSelection(const application::Selection &selection, std::optional<core::LineId> anchor);
    void setDraft(std::optional<core::LineRecord> draft);
    void setHiddenColumns(int mask);
    void setAutoHideColumns(bool on);
    bool autoHideColumns() const { return m_autoHide; }
    int emptyColumns() const;
    void setShowOriginal(bool show)
    {
        if (show == m_showOriginal)
            return;
        m_showOriginal = show;
        for (const Row &row : m_rows)
            row.shownText.reset();
        if (m_draft)
            m_draft->shownText.reset();
    }
    bool showOriginal() const { return m_showOriginal; }
    void setRunMarks(bool on);
    using Spelling = std::function<core::legacy::SpellMarks(std::u16string_view text, core::SubtitleFormat format,
                                                             bool spell)>;
    void setSpelling(Spelling spelling);
    using TagSpelling = std::function<core::legacy::SpellMarks(std::u16string_view text, core::SubtitleFormat format,
                                                                bool spell, int replaceTagsLen)>;
    void setSpelling(TagSpelling spelling) { m_spelling = std::move(spelling); }
    int hiddenColumns() const { return m_hidden; }
    bool columnShown(int column) const;
    void setFrameTimebase(std::optional<application::LegacyTimebase> frames);

    void setFoldGenerated(bool on);
    bool foldGenerated() const { return m_foldGenerated; }
    void setRunFolded(core::LineId first, bool folded);
    int runFirstRow(int row) const;
    bool runFolded(int firstRow) const;
    bool foldedMember(int row) const;
    std::vector<int> foldedRuns() const;
    std::vector<core::LineId> runLines(int firstRow) const;
    int runSize(int firstRow) const;

    std::optional<int> rowOf(core::LineId id) const;
    std::optional<core::LineId> lineAt(int row) const;
    const core::LineRecord *recordAt(int row) const;
    const application::Selection &selection() const { return m_selection; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void retranslate() { emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1); }
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Measures {
        QString cps, wraps;
        bool cpsTooHigh = false, badWraps = false;
    };
    struct Row {
        core::LineRecord line;
        mutable std::optional<Measures> measures;
        int blockMark = 0;
        bool groupClosed = false;
        mutable std::optional<QVariantList> spellMarks;
        int comparison = 0;
        QVariantList comparisonMarks;
        int state = 0;
        mutable std::optional<QString> shownText;
        mutable std::optional<bool> unplacedRuns;
        int run = -1;
    };
    struct Run {
        int first = 0, count = 0;
        int earliest = 0, latest = 0;
        int blockRow = 0;
        QString styles, effects;
    };
    QString startText(const core::LineRecord &line) const;
    QString endText(const core::LineRecord &line) const;
    QVariant foldedData(const Run &run, int column) const;
    void emitFoldsChanged();
    const QString &shownTextOf(const Row &row) const;
    const Row &shownRow(int row) const;
    void emitRowChanged(int row);
    bool tagsSwapped() const { return m_hideTags && !m_translationMode; }
    const Measures &measuresOf(const Row &row) const;
    const QVariantList &spellMarksOf(const Row &row) const;
    bool unplacedRunsOf(const Row &row) const;
    TagSpelling m_spelling;
    ChangeState m_changeState;
    bool m_hideTags = false;
    QString m_tagSwap;
    void emitStateChanged(const std::vector<core::LineId> &ids);

    std::vector<Row> m_rows;
    std::unordered_map<std::uint64_t, int> m_rowById;
    std::optional<Row> m_draft;
    int m_draftRow = -1;
    application::Selection m_selection;
    std::optional<core::LineId> m_anchor;
    core::SubtitleFormat m_format = core::SubtitleFormat::Ass;
    bool m_translationMode = false;
    bool m_showOriginal = false;
    bool m_runMarks = false;
    std::vector<core::StyleValues> m_styles;
    bool m_tooManyStyles = false;
    bool m_filtered = false;
    std::vector<Run> m_runs;
    bool m_foldGenerated = false;
    std::unordered_set<std::uint64_t> m_unfolded;
    int m_headerBlock = 0;
    int m_hidden = 0;
    bool m_autoHide = false;
    int m_empty = 0;
    std::optional<application::LegacyTimebase> m_frames;
    ComparisonColours m_comparisonColours = themeComparisonColours();
    LabelColours m_labelColours = themeLabelColours();
};

class LineFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using Predicate = std::function<bool(const core::LineRecord &)>;
    explicit LineFilterModel(QObject *parent = nullptr);

    void setLineModel(LineTableModel *model);
    void setPredicate(Predicate predicate);
    int hiddenSelectedCount() const;
    bool passes(int sourceRow) const;
    int nearestVisibleRow(core::LineId id) const;
    template <typename Change> void changeFolds(Change &&change)
    {
        beginFilterChange();
        change();
        endFilterChange(Direction::Rows);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    // Not virtual until Qt 7: QSortFilterProxyModel finds this redeclaration through the meta-object.
    Q_INVOKABLE DataChangeRelevanceFlags dataChangeRelevanceFlags(const QModelIndex &sourceTopLeft,
                                                                  const QModelIndex &sourceBottomRight,
                                                                  const QList<int> &roles) const;

private:
    LineTableModel *m_lines = nullptr;
    Predicate m_predicate;
};

}
