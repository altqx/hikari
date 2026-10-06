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
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace hikari::ui {

class WorkspaceLayoutController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(bool hasBackup READ hasBackup NOTIFY changed)
    // The built-in template the arrangement started from (Editing, Timing,
    // Translation, Typesetting); Reset layout returns to it.
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)
    // The application's focus window (QWindow::isActive is also true for the
    // parent of an active floating panel, so the shell compares windows).
    Q_PROPERTY(QObject *focusWindow READ focusWindow NOTIFY focusWindowChanged)
    // Whether the keyboard is in an open menu. Qt Quick Controls menus are
    // items of the window they open in (Popup.Item), so the focus window stays
    // the shell's while one is open: the shell's shortcuts stay off then, as
    // legacy's menu took every key while it was shown (D1 Windows gate).
    Q_PROPERTY(bool menuHasFocus READ menuHasFocus NOTIFY menuHasFocusChanged)
    // D2: the player layout (GLOBAL_EDITOR off) holds the editing
    // arrangement until the editor comes back; nothing is saved meanwhile,
    // so the saved layout stays the editing one.
    Q_PROPERTY(bool holding READ holding NOTIFY changed)
public:
    static constexpr int kSchema = 1;
    static constexpr int kPanelRegistry = 3; // 2: Timing (F5), 3: Search (F1)
    static constexpr qsizetype kMaximumBytes = 4 * 1024 * 1024;
    static const QStringList &panelIds(); // Video, Audio, Editor, Grid, Reference, Timing, Search

    // `layoutFile` empty: nothing is persisted (tests, a profile without settings).
    explicit WorkspaceLayoutController(QString layoutFile = {}, QObject *parent = nullptr);

    // After the shell built its default arrangement: what Reset layout returns to.
    Q_INVOKABLE void captureDefault();
    // At start: the saved layout, if it validates and restores; false (with a
    // notice when a file was there) leaves the default arrangement.
    Q_INVOKABLE bool restoreSaved();
    // The current arrangement, when it changed since the last save; nothing
    // until restoreSaved() has read the saved file, so a save before then
    // (the window closed before its first frame) cannot replace it.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool resetLayout();
    Q_INVOKABLE bool restoreBackup();
    Q_INVOKABLE void dismissNotice();
    // Keyboard resize (docs/qt/docking.md): a docked panel's size in its
    // layout, and a new width/height for it (the engine moves the panel's
    // right and bottom separators, within the neighbours' minimum sizes).
    Q_INVOKABLE QSize panelSize(QObject *dock) const;
    Q_INVOKABLE bool resizePanel(QObject *dock, int width, int height);
    // Floating panel windows whose title bar no screen shows move onto the
    // main window's screen (also done when screens change and after a restore).
    Q_INVOKABLE int keepFloatingPanelsOnScreen();
    // D2: legacy View menu's arrangements (HikariSubFrame.cpp:849-892), by
    // their GLOBAL_VIEW_* names: the core panels each one shows (the others
    // of Video, Audio, Editor and Grid are hidden). Empty for another name.
    Q_INVOKABLE static QStringList arrangementPanels(const QString &arrangement);
    static const QStringList &arrangements();
    // D2: holds the current arrangement (the player layout follows), and
    // gives it back; false when nothing is held or it does not restore.
    Q_INVOKABLE void holdArrangement();
    Q_INVOKABLE bool releaseArrangement();
    bool holding() const { return !m_held.isEmpty(); }
    // D2: the arrangement as it was when Video, Audio, Editor and Grid were
    // last all shown (the shell remembers it before an arrangement hides
    // one, and while they stay shown); an arrangement that shows a hidden
    // core panel again starts from it, as legacy's views laid the panels
    // out at their own sizes (VIDEO_WINDOW_SIZE). Reset layout, a preset,
    // the backup and the saved layout at start forget it.
    Q_INVOKABLE void rememberFullArrangement();
    Q_INVOKABLE bool restoreFullArrangement();

    QString notice() const { return m_notice; }
    QString preset() const { return m_preset; }
    void setPreset(const QString &preset);
    static const QStringList &presets();
    QObject *focusWindow() const;
    bool menuHasFocus() const { return m_menuHasFocus; }
    bool hasBackup() const;

    // The envelope around an engine payload, and its validation.
    static QByteArray envelope(const QByteArray &payload, const QString &preset = QStringLiteral("Editing"));
    static std::optional<QByteArray> payloadOf(const QByteArray &file, QString *problem, QString *preset = nullptr);

signals:
    void changed();
    void focusWindowChanged();
    void menuHasFocusChanged();

private:
    bool restorePayload(const QByteArray &payload, bool inPlace = false);
    QString backupFile() const { return m_file + QStringLiteral(".bak"); }

    QString m_file;
    QByteArray m_default;
    QByteArray m_lastSaved;
    QByteArray m_held;
    QByteArray m_full;
    QString m_notice;
    QString m_preset = QStringLiteral("Editing");
    bool m_restoring = false;
    bool m_loaded = false; // restoreSaved() has run
    bool m_menuHasFocus = false;
    QTimer m_screenCheck;
};

} // namespace hikari::ui
