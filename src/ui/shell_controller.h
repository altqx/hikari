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

public:
    explicit ShellController(application::Workspace &workspace, QObject *parent = nullptr);

    // Re-reads the workspace targets and projects the given snapshots: the
    // editing target's and the reference's (nullptr when there is none).
    void refresh(const core::Document *target, const core::Document *reference);

    bool hasEditingTarget() const { return m_workspace.editingTarget().has_value(); }
    QString editingTitle() const;
    bool hasReference() const { return m_workspace.reference().has_value(); }
    QString referenceTitle() const;
    QAbstractItemModel *lines() { return &m_lines; }
    QAbstractItemModel *referenceLines() { return &m_referenceLines; }
    QString activeLineText() const { return m_activeText; }
    QString activeLineStyle() const { return m_activeStyle; }
    QString selectionStatus() const { return m_selectionStatus; }

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

private:
    QString titleOf(std::optional<application::DocumentId> id) const;

    application::Workspace &m_workspace;
    LineTableModel m_lines;
    LineTableModel m_referenceLines;
    QString m_activeText;
    QString m_activeStyle;
    QString m_selectionStatus;
};

} // namespace hikari::ui
