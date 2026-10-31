#pragma once

#include "line_table_model.h"

#include "hikari/application/reference_navigation.h"
#include "hikari/application/workspace.h"

#include <QObject>
#include <QVariantMap>

#include <functional>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class ShellController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool hasEditingTarget READ hasEditingTarget NOTIFY targetsChanged)
    Q_PROPERTY(QString editingTitle READ editingTitle NOTIFY targetsChanged)
    Q_PROPERTY(bool hasReference READ hasReference NOTIFY targetsChanged)
    Q_PROPERTY(QString referenceTitle READ referenceTitle NOTIFY targetsChanged)
    Q_PROPERTY(QAbstractItemModel *lines READ lines CONSTANT)
    Q_PROPERTY(QAbstractItemModel *referenceLines READ referenceLines CONSTANT)
    Q_PROPERTY(QString activeLineText READ activeLineText NOTIFY activeLineChanged)
    Q_PROPERTY(QString activeLineStyle READ activeLineStyle NOTIFY activeLineChanged)
    Q_PROPERTY(int activeLineNumber READ activeLineNumber NOTIFY activeLineChanged)
    Q_PROPERTY(QString selectionStatus READ selectionStatus NOTIFY activeLineChanged)
    // MUST KILL: hiddenColumns is a bitmask of legacy GRID_HIDE_* column bits
    Q_PROPERTY(int hiddenColumns READ hiddenColumns WRITE setHiddenColumns NOTIFY hiddenColumnsChanged)
    Q_PROPERTY(bool autoHideColumns READ autoHideColumns WRITE setAutoHideColumns NOTIFY autoHideColumnsChanged)
    Q_PROPERTY(QVariantMap gridColumnWidths READ gridColumnWidths WRITE setGridColumnWidths NOTIFY gridColumnWidthsChanged)
    Q_PROPERTY(bool foldGenerated READ foldGenerated WRITE setFoldGenerated NOTIFY foldGeneratedChanged)
    Q_PROPERTY(bool assColumns READ assColumns NOTIFY targetsChanged)
    Q_PROPERTY(bool endColumn READ endColumn NOTIFY targetsChanged)
    Q_PROPERTY(bool filtered READ filtered NOTIFY targetsChanged)
    Q_PROPERTY(bool hideTags READ hideTags NOTIFY hideTagsChanged)
    Q_PROPERTY(QString statusText READ statusText WRITE setStatusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool referenceLinked READ referenceLinked NOTIFY referenceNavigationChanged)
    Q_PROPERTY(int referenceMatchCount READ referenceMatchCount NOTIFY referenceNavigationChanged)
    Q_PROPERTY(int referenceMatchIndex READ referenceMatchIndex NOTIFY referenceNavigationChanged)
    Q_PROPERTY(bool referenceNoMatch READ referenceNoMatch NOTIFY referenceNavigationChanged)
    Q_PROPERTY(bool referenceHasNearest READ referenceHasNearest NOTIFY referenceNavigationChanged)
    Q_PROPERTY(bool referenceIsTab READ referenceIsTab NOTIFY targetsChanged)

public:
    explicit ShellController(application::Workspace &workspace, QObject *parent = nullptr);

    void refresh(const core::Document *target, const core::Document *reference);
    void refresh(const core::Document *target, const core::Document *reference,
                 const std::vector<application::LineComparison> *targetComparison,
                 const std::vector<application::LineComparison> *referenceComparison);
    void setShowOriginal(bool target, bool reference)
    {
        m_lines.setShowOriginal(target);
        m_referenceLines.setShowOriginal(reference);
    }
    void setComparisonColours(const LineTableModel::ComparisonColours &colours)
    {
        m_lines.setComparisonColours(colours);
        m_referenceLines.setComparisonColours(colours);
    }

    bool hasEditingTarget() const { return m_workspace.editingTarget().has_value(); }
    QString editingTitle() const;
    bool hasReference() const { return m_workspace.reference().has_value(); }
    QString referenceTitle() const;
    QAbstractItemModel *lines() { return &m_shown; }
    QAbstractItemModel *referenceLines() { return &m_referenceLines; }
    QString activeLineText() const { return m_activeText; }
    QString activeLineStyle() const { return m_activeStyle; }
    int activeLineNumber() const { return m_activeNumber; }
    QString selectionStatus() const { return m_selectionStatus; }
    QString statusText() const { return m_statusText; }
    void setStatusText(const QString &text)
    {
        if (text != m_statusText) {
            m_statusText = text;
            emit statusTextChanged();
        }
    }

    void startProgress(const QString &title);
    void setProgress(int percent);
    void endProgress();
    void setProgressClock(std::function<qint64()> clock) { m_progressClock = std::move(clock); }

    int hiddenColumns() const { return m_lines.hiddenColumns(); }
    void setHiddenColumns(int mask)
    {
        if (mask == m_lines.hiddenColumns())
            return;
        m_lines.setHiddenColumns(mask);
        m_referenceLines.setHiddenColumns(mask);
        emit hiddenColumnsChanged();
    }
    bool autoHideColumns() const { return m_lines.autoHideColumns(); }
    void setAutoHideColumns(bool on)
    {
        if (on == m_lines.autoHideColumns())
            return;
        m_lines.setAutoHideColumns(on);
        m_referenceLines.setAutoHideColumns(on);
        emit autoHideColumnsChanged();
    }
    Q_INVOKABLE void toggleColumn(int bit) { setHiddenColumns(hiddenColumns() ^ bit); }
    QVariantMap gridColumnWidths() const { return m_gridColumnWidths; }
    void setGridColumnWidths(const QVariantMap &widths)
    {
        if (widths == m_gridColumnWidths)
            return;
        m_gridColumnWidths = widths;
        emit gridColumnWidthsChanged();
    }
    void setFrameTimebase(std::optional<application::LegacyTimebase> frames) { m_lines.setFrameTimebase(std::move(frames)); }
    bool assColumns() const { return m_assFormat; }
    bool endColumn() const { return m_endColumn; }
    bool filtered() const { return m_lines.headerData(0, Qt::Horizontal, LineTableModel::FilteredRole).toBool(); }
    bool hideTags() const { return m_lines.hideTags(); }
    void setHideTags(bool hide, const QString &swap)
    {
        const bool changed = hide != m_lines.hideTags();
        m_lines.setHideTags(hide, swap);
        m_referenceLines.setHideTags(hide, swap);
        if (changed)
            emit hideTagsChanged();
    }
    void setRunMarks(bool on)
    {
        m_lines.setRunMarks(on);
        m_referenceLines.setRunMarks(on);
    }
    void setChangeStates(const LineTableModel::ChangeState &target, const LineTableModel::ChangeState &reference)
    {
        m_lines.setChangeState(target);
        m_referenceLines.setChangeState(reference);
    }
    void setSpelling(const LineTableModel::TagSpelling &spelling)
    {
        m_lines.setSpelling(spelling);
        m_referenceLines.setSpelling(spelling);
    }

    bool foldGenerated() const { return m_lines.foldGenerated(); }
    // MUST KILL: setFoldGenerated emits foldGeneratedChanged before the rows fold; Application relies on that order
    void setFoldGenerated(bool on);
    void setRunFolded(core::LineId first, bool folded);
    std::optional<core::LineId> runFirst(core::LineId line) const;
    bool runFolded(core::LineId first) const;
    std::vector<core::LineId> runLines(core::LineId first) const;
    application::Selection withFoldedRuns(application::Selection selection) const;
    application::Selection foldedWhole(application::Selection selection,
                                       std::optional<core::LineId> first = std::nullopt) const;

    void setDraft(std::optional<core::LineRecord> draft) { m_lines.setDraft(std::move(draft)); }

    Q_INVOKABLE void activateLine(qulonglong id);
    Q_INVOKABLE void selectLine(qulonglong id);
    // MUST KILL: setSelection also unfolds a folded run it selects in part or activates inside
    void setSelection(const application::Selection &selection);
    std::vector<core::LineId> displayedLines() const;
    std::vector<core::LineId> actionLines() const;
    void retranslate();

    void setReferenceSelection(const application::Selection &selection);
    std::vector<core::LineId> referenceDisplayedLines() const;
    void setReferenceNavigation(bool linked, const application::LinkedMatch &match);
    void showReferenceLine(core::LineId line) { emit referenceLineShown(line.value); }
    bool referenceLinked() const { return m_referenceLinked; }
    int referenceMatchCount() const { return static_cast<int>(m_referenceMatch.count()); }
    int referenceMatchIndex() const { return static_cast<int>(m_referenceMatch.index()); }
    bool referenceNoMatch() const { return m_referenceLinked && m_referenceMatch.noMatch(); }
    bool referenceHasNearest() const { return referenceNoMatch() && m_referenceMatch.nearest().has_value(); }
    bool referenceIsTab() const { return m_workspace.referenceIsTab(); }

signals:
    void targetsChanged();
    void activeLineChanged();
    void statusTextChanged();
    void hiddenColumnsChanged();
    void autoHideColumnsChanged();
    void gridColumnWidthsChanged();
    void foldGeneratedChanged();
    void hideTagsChanged();
    void referenceNavigationChanged();
    void referenceLineShown(qulonglong id);

private:
    QString titleOf(std::optional<application::DocumentId> id) const;
    QString selectionText() const;

    application::Workspace &m_workspace;
    LineTableModel m_lines;
    LineFilterModel m_shown;
    LineTableModel m_referenceLines;
    QString m_activeText;
    QString m_activeStyle;
    int m_activeNumber = 0;
    QString m_selectionStatus;
    qsizetype m_selectedCount = 0, m_hiddenCount = 0;
    QString m_statusText;
    QVariantMap m_gridColumnWidths;
    QString m_progressTitle;
    qint64 m_progressStart = 0;
    std::function<qint64()> m_progressClock;
    qint64 progressNow() const;
    bool m_assFormat = true;
    bool m_endColumn = true;
    bool m_referenceLinked = false;
    application::LinkedMatch m_referenceMatch;
};

}
