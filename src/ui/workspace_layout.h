#pragma once

// D1: the WorkspaceLayout service (docs/qt/docking.md, "Save, restore and
// recovery"). The docking engine's layout is kept in Hikari's own versioned
// envelope, written atomically, with the previous valid file kept as a
// backup. A layout that fails validation or restore is never overwritten:
// the default arrangement is used and a notice names the kept file.

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace hikari::ui {

class WorkspaceLayoutController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(bool hasBackup READ hasBackup NOTIFY changed)
    // The application's focus window (QWindow::isActive is also true for the
    // parent of an active floating panel, so the shell compares windows).
    Q_PROPERTY(QObject *focusWindow READ focusWindow NOTIFY focusWindowChanged)
public:
    static constexpr int kSchema = 1;
    static constexpr int kPanelRegistry = 1;
    static constexpr qsizetype kMaximumBytes = 4 * 1024 * 1024;
    static const QStringList &panelIds(); // Video, Audio, Editor, Grid, Reference

    // `layoutFile` empty: nothing is persisted (tests, a profile without settings).
    explicit WorkspaceLayoutController(QString layoutFile = {}, QObject *parent = nullptr);

    // After the shell built its default arrangement: what Reset layout returns to.
    Q_INVOKABLE void captureDefault();
    // At start: the saved layout, if it validates and restores; false (with a
    // notice when a file was there) leaves the default arrangement.
    Q_INVOKABLE bool restoreSaved();
    // The current arrangement, when it changed since the last save.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool resetLayout();
    Q_INVOKABLE bool restoreBackup();
    Q_INVOKABLE void dismissNotice();
    // Keyboard resize (docs/qt/docking.md): a docked panel's size in its
    // layout, and a new width/height for it (the engine moves the panel's
    // right and bottom separators, within the neighbours' minimum sizes).
    Q_INVOKABLE QSize panelSize(QObject *dock) const;
    Q_INVOKABLE bool resizePanel(QObject *dock, int width, int height);

    QString notice() const { return m_notice; }
    QObject *focusWindow() const;
    bool hasBackup() const;

    // The envelope around an engine payload, and its validation.
    static QByteArray envelope(const QByteArray &payload, const QString &preset = QStringLiteral("Editing"));
    static std::optional<QByteArray> payloadOf(const QByteArray &file, QString *problem);

signals:
    void changed();
    void focusWindowChanged();

private:
    bool restorePayload(const QByteArray &payload);
    QString backupFile() const { return m_file + QStringLiteral(".bak"); }

    QString m_file;
    QByteArray m_default;
    QByteArray m_lastSaved;
    QString m_notice;
    bool m_restoring = false;
};

} // namespace hikari::ui
