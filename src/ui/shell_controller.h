#pragma once

// Presenter for the Classic shell (V1-S). It projects the workspace's editing
// target and protected reference into read-only Line models and labels. It
// owns no Document: callers pass snapshots, and the workspace stays the only
// authority on which Document is which.

#include "line_table_model.h"

#include "hikari/application/workspace.h"

#include <QObject>
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
    // "3 Lines selected", with the hidden count when some are hidden (G1).
    Q_PROPERTY(QString selectionStatus READ selectionStatus NOTIFY activeLineChanged)
    // GRID_HIDE_COLUMNS: legacy column bits hidden in both Grids (G7).
    Q_PROPERTY(int hiddenColumns READ hiddenColumns WRITE setHiddenColumns NOTIFY hiddenColumnsChanged)
    // The editing target's format has the ASS columns (Layer ... Effect), and an End time.
    Q_PROPERTY(bool assColumns READ assColumns NOTIFY targetsChanged)
    Q_PROPERTY(bool endColumn READ endColumn NOTIFY targetsChanged)
    // The editing target is filtered (legacy IsFiltered): "Turn off filtering".
    Q_PROPERTY(bool filtered READ filtered NOTIFY targetsChanged)
    // A transient status message (automation's set_status_text).
    Q_PROPERTY(QString statusText READ statusText WRITE setStatusText NOTIFY statusTextChanged)

public:
    explicit ShellController(application::Workspace &workspace, QObject *parent = nullptr);

    // Re-reads the workspace targets and projects the given snapshots: the
    // editing target's and the reference's (nullptr when there is none).
    void refresh(const core::Document *target, const core::Document *reference);
    // R1: with each one's comparison table (nullptr for none).
    void refresh(const core::Document *target, const core::Document *reference,
                 const std::vector<application::LineComparison> *targetComparison,
                 const std::vector<application::LineComparison> *referenceComparison);
    // R1: the comparison colours of both Grids (GRID_COMPARISON_*).
    void setComparisonColours(const LineTableModel::ComparisonColours &colours)
    {
        m_lines.setComparisonColours(colours);
        m_referenceLines.setComparisonColours(colours);
    }

    bool hasEditingTarget() const { return m_workspace.editingTarget().has_value(); }
    QString editingTitle() const;
    bool hasReference() const { return m_workspace.reference().has_value(); }
    QString referenceTitle() const;
    // The Grid shows the editing target's Lines that are not hidden (G8).
    QAbstractItemModel *lines() { return &m_shown; }
    QAbstractItemModel *referenceLines() { return &m_referenceLines; }
    QString activeLineText() const { return m_activeText; }
    QString activeLineStyle() const { return m_activeStyle; }
    QString selectionStatus() const { return m_selectionStatus; }
    QString statusText() const { return m_statusText; }
    void setStatusText(const QString &text)
    {
        if (text != m_statusText) {
            m_statusText = text;
            emit statusTextChanged();
        }
    }

    int hiddenColumns() const { return m_lines.hiddenColumns(); }
    void setHiddenColumns(int mask)
    {
        if (mask == m_lines.hiddenColumns())
            return;
        m_lines.setHiddenColumns(mask);
        m_referenceLines.setHiddenColumns(mask);
        emit hiddenColumnsChanged();
    }
    // Legacy toggles one column's bit (GRID_HIDE_LAYER ... GRID_HIDE_WRAPS).
    Q_INVOKABLE void toggleColumn(int bit) { setHiddenColumns(hiddenColumns() ^ bit); }
    // E4: the Grid's Start/End as frames (the editor's Times/Frames switch).
    void setFrameTimebase(std::optional<application::LegacyTimebase> frames) { m_lines.setFrameTimebase(std::move(frames)); }
    bool assColumns() const { return m_assFormat; }
    bool endColumn() const { return m_endColumn; }
    bool filtered() const { return m_lines.headerData(0, Qt::Horizontal, LineTableModel::FilteredRole).toBool(); }
    // F3: spelling marks in both Grids, from the next refresh.
    void setSpelling(const LineTableModel::Spelling &spelling)
    {
        m_lines.setSpelling(spelling);
        m_referenceLines.setSpelling(spelling);
    }

    // The editing target's Grid asked for a Line to become active.
    Q_INVOKABLE void activateLine(qulonglong id);
    // Marks a Line active and selected in the editing target's Grid.
    Q_INVOKABLE void selectLine(qulonglong id);
    // Shows the editing target's whole selection (G1).
    void setSelection(const application::Selection &selection);
    // The editing target's Lines in the order the Grid shows them.
    std::vector<core::LineId> displayedLines() const;

signals:
    void targetsChanged();
    void activeLineChanged();
    void statusTextChanged();
    void hiddenColumnsChanged();

private:
    QString titleOf(std::optional<application::DocumentId> id) const;

    application::Workspace &m_workspace;
    LineTableModel m_lines;
    LineFilterModel m_shown;
    LineTableModel m_referenceLines;
    QString m_activeText;
    QString m_activeStyle;
    QString m_selectionStatus;
    QString m_statusText;
    bool m_assFormat = true;
    bool m_endColumn = true;
};

} // namespace hikari::ui
