#pragma once

// D1: the WorkspaceLayout service (docs/qt/docking.md, "Save, restore and
// recovery"). The docking engine's layout is kept in Hikari's own versioned
// envelope, written atomically, with the previous valid file kept as a
// backup. A layout that fails validation or restore is never overwritten:
// the default arrangement is used and a notice names the kept file.
//
// WS1 (ADR 0020): each Document tab has its own arrangement. The engine
// shows the current tab's; the others are kept as WorkspaceArrangement
// values and swapped in when their tab is shown. The saved file holds the
// arrangement shown last, which the first tab of a start without a Session
// takes.
//
// WS3 (#217): the user's saved layouts, named arrangements usable from every
// tab, are kept in a file of their own beside the saved layout. Applying one
// changes the current tab only, as a preset does; Reset this layout goes back
// to the saved layout the tab's arrangement started from, or its preset.

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace hikari::ui {

// WS1: one Document tab's panel arrangement, a value apart from the engine
// (the Session's "Layout: " line, WS2's [Hikari] section): the built-in
// template it started from and the engine's payload. A value without a
// payload is the template itself, built when the tab is shown. WS3: `layout`
// names the saved layout it started from, if any.
struct WorkspaceArrangement {
    QString preset = QStringLiteral("Editing");
    QByteArray payload; // the engine's layout (JSON), or empty: the preset as built in
    QString layout;     // the saved layout's name, or empty: the preset's

    // The envelope in one line (compact JSON).
    QByteArray serialise() const;
    // The envelope's arrangement, validated as the saved file is; a missing
    // "payload" is the preset alone. nullopt with `problem` naming why.
    static std::optional<WorkspaceArrangement> parse(const QByteArray &bytes, QString *problem = nullptr);
    // WS2: the arrangement as an ASS file's [Hikari] section holds it
    // ("Layout="): the envelope in base64 (RFC 4648), which has no ':' for
    // legacy LoadASS to take into Script Info. No screen positions: the
    // screens, each window's screen and the main window's position are left
    // out, and a floating panel keeps its place from the main window's
    // top-left corner. The main window's size stays, so the docked panels
    // keep their proportions in a window of another size.
    QByteArray serialiseForFile() const;
    // WS2: an arrangement serialiseForFile wrote, validated as parse does,
    // its floating panels placed from `origin` (the main window's top-left).
    // Text longer than any layout within kMaximumBytes encodes to is refused
    // before it is copied or decoded.
    static std::optional<WorkspaceArrangement> parseFromFile(QByteArrayView text, QPoint origin,
                                                             QString *problem = nullptr);
    bool operator==(const WorkspaceArrangement &) const = default;
};

class WorkspaceLayoutController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(bool hasBackup READ hasBackup NOTIFY changed)
    // The built-in template the arrangement started from (Editing, Timing,
    // Translation, Typesetting, Karaoke); Reset layout returns to it unless the
    // arrangement started from a saved layout.
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)
    // FR1 (#363): the built-in templates in menu order, for Getting started.
    Q_PROPERTY(QStringList presets READ presets CONSTANT)
    // WS3: the saved layout the current tab's arrangement started from, or
    // empty (a preset, or a layout since deleted); and the name the layout
    // switcher shows for it (the saved layout's, else the preset's).
    Q_PROPERTY(QString layout READ layout NOTIFY changed)
    Q_PROPERTY(QString layoutLabel READ layoutLabel NOTIFY changed)
    // WS3: the saved layouts, in the order they were saved: {name, usable,
    // problem}. One this version cannot use is kept, unused, until deleted.
    Q_PROPERTY(QVariantList savedLayouts READ savedLayouts NOTIFY savedLayoutsChanged)
    // The application's focus window (QWindow::isActive is also true for the
    // parent of an active floating panel, so the shell compares windows).
    Q_PROPERTY(QObject *focusWindow READ focusWindow NOTIFY focusWindowChanged)
    // Whether the keyboard is in an open menu. Qt Quick Controls menus are
    // items of the window they open in (Popup.Item), so the focus window stays
    // the shell's while one is open: the shell's shortcuts stay off then, as
    // legacy's menu took every key while it was shown (D1 Windows gate).
    Q_PROPERTY(bool menuHasFocus READ menuHasFocus NOTIFY menuHasFocusChanged)
    // AM1: whether a modal popup (a dialog over its window, Popup.Item) is
    // open in a shown window, or a modal window (DG1's frame) is shown. The
    // application-wide macro shortcuts stay off then: a macro started behind
    // it shows its run controls in the Automation panel, which the modal
    // keeps out of reach.
    Q_PROPERTY(bool modalOpen READ modalOpen NOTIFY modalOpenChanged)
public:
    // WS1: how a tab shown for the first time starts.
    enum class TabStart { Copy, Translation };

    static constexpr int kSchema = 1;
    // 2: Timing (F5), 3: Search (F1), 4: Automation (AM1), 5: Original (TM1), 6: Log (NT1), 7: Styles (TP1),
    // 8: FixMinorErrors (PU2), 9: Template (KE2), 10: Spellchecker (PU6)
    static constexpr int kPanelRegistry = 10;
    static constexpr qsizetype kMaximumBytes = 4 * 1024 * 1024;
    static constexpr int kSavedLayoutsSchema = 1;
    static constexpr int kMaximumNameLength = 64;
    // Video, Audio, Editor, Grid, Reference, Timing, Search, Automation, Original, Log, Styles, FixMinorErrors,
    // Template, Spellchecker
    static const QStringList &panelIds();

    // `layoutFile` empty: nothing is persisted (tests, a profile without
    // settings). The saved layouts are kept beside it, in layouts.json.
    explicit WorkspaceLayoutController(QString layoutFile = {}, QObject *parent = nullptr);

    // After the shell built its default arrangement: what Reset layout returns to.
    Q_INVOKABLE void captureDefault();
    // TP1 (#274): a preset whose arrangement the shell builds itself
    // (Typesetting, Karaoke). The first time it is shown (applyPreset, Reset this
    // layout) the default arrangement is restored and buildArrangement asks
    // the shell to arrange it; what the shell built is the preset's from then
    // on. Nothing is built at start, before anyone chooses it.
    Q_INVOKABLE void buildsArrangement(const QString &name);
    // At start: the saved layout, if it validates and restores; false (with a
    // notice when a file was there) leaves the default arrangement.
    Q_INVOKABLE bool restoreSaved();
    // The current arrangement, when it changed since the last save; nothing
    // until restoreSaved() has read the saved file, so a save before then
    // (the window closed before its first frame) cannot replace it.
    Q_INVOKABLE bool save();
    // When the main window's close goes ahead: the last save. Taking the
    // window down then closes every panel and hides the window; no save after
    // this one writes that over the saved layout.
    Q_INVOKABLE bool saveForQuit();
    Q_INVOKABLE bool resetLayout();
    // A built-in template (Editing, Timing, Translation, Typesetting, Karaoke)
    // for the current tab: its own arrangement (Typesetting's and Karaoke's,
    // buildsArrangement), else
    // the default arrangement with the panels it leaves closed.
    Q_INVOKABLE bool applyPreset(const QString &name);
    // A preset's name as the user reads it.
    Q_INVOKABLE static QString presetLabel(const QString &name);
    // TM1 (#232): the mode a preset carries, or empty. Choosing the preset
    // (applyPreset, or applySavedLayout of a layout that started from it)
    // starts that mode for the current tab (presetModeChosen); choosing an
    // arrangement whose preset carries another mode or none afterwards, in
    // this tab or another, leaves it (presetModeLeft). What starting and leaving do is the mode's
    // own: Translation asks to turn the Document's Translation mode on and
    // stays on when left; Karaoke (KP1) turns the audio panel's Karaoke mode
    // and the Grid's folding on, and puts back what it changed.
    // Showing a tab, opening a file, a Session's arrangement, the backup and
    // Reset this layout start and leave nothing.
    Q_INVOKABLE static QString presetMode(const QString &preset);
    // A language switch: layoutLabel reads the new language's preset names.
    void retranslate() { emit changed(); }
    // WS3: the presets' names in every shipped language, refused as saved
    // layouts' names (a switch to that language would show two); the saved
    // layouts are read again under them.
    void reservePresetNames(const QStringList &names);
    // WS3: Reset this layout, for the current tab: the saved layout its
    // arrangement started from, else its preset.
    Q_INVOKABLE bool resetCurrentLayout();
    // WS3: the current tab's arrangement saved as `name` (one of that name,
    // in any case, is replaced); the tab's arrangement now started from it.
    Q_INVOKABLE bool saveLayoutAs(const QString &name);
    // WS3: a saved layout for the current tab, in place (the main window
    // keeps its geometry). One the engine refuses gives the tab the Editing
    // layout with the notice.
    Q_INVOKABLE bool applySavedLayout(const QString &name);
    // WS3: renaming and deleting a saved layout. A tab whose arrangement
    // started from it keeps the arrangement, under the new name, or its
    // preset's once the layout is deleted.
    Q_INVOKABLE bool renameSavedLayout(const QString &from, const QString &to);
    Q_INVOKABLE bool deleteSavedLayout(const QString &name);
    // WS3: why `name` (trimmed) cannot name a saved layout, or empty. A name
    // in use is a problem only for a rename (`renaming`: the layout's
    // current name); Save layout as… replaces that layout.
    Q_INVOKABLE QString layoutNameProblem(const QString &name, const QString &renaming = {}) const;
    // The saved layout called `name`, in any case: its name as saved, or empty.
    Q_INVOKABLE QString savedLayoutNamed(const QString &name) const;
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

    // WS1: the Document tab `tab` (its Document's id) is the one shown. The
    // tab shown so far keeps the arrangement it has now; `tab` gets its own
    // back, or, the first time, `start`: a copy of the arrangement shown, or
    // the Translation preset (a TLMode file). Before the shell's first
    // arrangement (restoreSaved) only the values change.
    void showTab(std::uint64_t tab, TabStart start = TabStart::Copy);
    // A tab opened, shown or not: unless it has an arrangement, it takes
    // `start` now (a copy of the arrangement shown, or the Translation
    // preset), so a background tab starts as the tab was when it opened.
    void addTab(std::uint64_t tab, TabStart start = TabStart::Copy);
    // The tab's Document was replaced by another (opened into the tab): the
    // tab keeps its arrangement, or takes the Translation preset.
    void replaceTab(std::uint64_t tab, std::uint64_t replacement, TabStart start = TabStart::Copy);
    void forgetTab(std::uint64_t tab);
    void forgetTabs();
    // The tab's arrangement (the one shown for the current tab); nullopt for
    // a tab that has none yet.
    std::optional<WorkspaceArrangement> tabArrangement(std::uint64_t tab) const;
    // A Session's arrangement for `tab` ("Layout: "). One that does not
    // validate gives the tab the Editing preset, keeps the text for
    // diagnosis and says so in the notice, naming `title`.
    void setTabArrangement(std::uint64_t tab, const QByteArray &serialised, const QString &title = {});
    // WS2: the tab's arrangement as its subtitle file keeps it
    // (serialiseForFile); empty for a tab that has none yet.
    QByteArray fileArrangement(std::uint64_t tab) const;
    // WS2: the arrangement `tab`'s subtitle file holds, its floating panels
    // placed from the main window as it is now (before the window's first
    // arrangement, from where restoreSaved puts it). One that does not
    // validate leaves the tab as it was: false, with `problem` naming why.
    bool setFileArrangement(std::uint64_t tab, const QByteArray &text, QString *problem = nullptr);

    // Every notice not dismissed yet, one per line: the panel layout's and
    // the saved layouts' are kept side by side (both can come at start).
    QString notice() const { return (m_savedNotices + m_panelNotices).join(QLatin1Char('\n')); }
    QString preset() const { return m_preset; }
    void setPreset(const QString &preset);
    static const QStringList &presets();
    QString layout() const;
    QString layoutLabel() const;
    QVariantList savedLayouts() const;
    QString savedLayoutsFile() const { return m_savedFile; }
    QObject *focusWindow() const;
    bool menuHasFocus() const { return m_menuHasFocus; }
    bool modalOpen() const { return m_modalOpen; }
    bool hasBackup() const;

    // The envelope around an engine payload, and its validation.
    static QByteArray envelope(const QByteArray &payload, const QString &preset = QStringLiteral("Editing"),
                               const QString &layout = {});
    static std::optional<QByteArray> payloadOf(const QByteArray &file, QString *problem, QString *preset = nullptr,
                                               QString *layout = nullptr);

signals:
    void changed();
    // TP1: arrange preset `name` now, from the default arrangement shown
    // (buildsArrangement); the controller keeps what the handler leaves.
    void buildArrangement(const QString &name);
    void focusWindowChanged();
    void menuHasFocusChanged();
    void modalOpenChanged();
    void savedLayoutsChanged();
    // WS1: another arrangement replaced the one shown: another tab's, a
    // preset, a saved layout, the backup (the shell puts the panels that
    // follow the application, such as the Reference tray, back as they must
    // be).
    void arrangementSwapped();
    // TM1: the user chose an arrangement whose preset carries `mode`, or
    // left one that did (presetMode).
    void presetModeChosen(const QString &mode);
    void presetModeLeft(const QString &mode);

private:
    void checkModal();
    bool applyPresetArrangement(const QString &name);
    bool applySavedArrangement(const QString &name);
    // TM1: the mode of the preset just chosen starts, and the mode chosen
    // last, in this tab or another (KP1), is left when it differs.
    void choseMode();
    bool restorePayload(const QByteArray &payload, bool inPlace = false);
    bool restoreSavedFile();
    bool buildPreset(const QString &name, bool inPlace);
    bool showArrangement(const WorkspaceArrangement &arrangement);
    WorkspaceArrangement shownArrangement() const;
    void refuseArrangement(const WorkspaceArrangement &arrangement);
    void keepUnrestored(const QByteArray &bytes, const QString &problem, const QString &title);
    void giveTab(std::uint64_t tab, WorkspaceArrangement arrangement);
    QString backupFile() const { return m_file + QStringLiteral(".bak"); }

    // WS3: one saved layout; one that does not validate keeps its record as
    // read, so the file is written back with it.
    struct SavedLayout {
        QString name;
        std::optional<WorkspaceArrangement> arrangement;
        QString problem;
        QJsonObject record;
    };
    void readSavedLayouts();
    // The layouts file could not be read or kept aside: nothing writes over
    // it this session, and the notice says so.
    void lockSavedLayouts();
    bool writeSavedLayouts();
    int savedLayoutIndex(const QString &name) const; // any case; -1: none
    // Every arrangement (each tab's, the shown one's) that started from
    // saved layout `from` starts from `to` now (empty: its preset).
    void renameLayoutOrigin(const QString &from, const QString &to);
    // `name` as a saved layout of this profile names it, or empty.
    QString knownLayout(const QString &name) const;
    QStringList m_reservedNames;
    std::vector<SavedLayout> m_saved;
    QString m_savedFile;
    bool m_savedLocked = false;
    QString m_writeFailure; // the notice of the last saved-layouts write that failed

    struct Tab {
        std::optional<WorkspaceArrangement> arrangement;
        // WS2: a file's arrangement taken before the main window had its
        // place, its windows still from (0, 0) until restoreSaved.
        bool unplaced = false;
    };
    std::map<std::uint64_t, Tab> m_tabs;
    std::optional<std::uint64_t> m_tab; // the tab the engine shows
    std::optional<WorkspaceArrangement> m_quitArrangement; // shown when the close went ahead

    // WS3: what a change to the saved layouts touches, taken before it. When
    // the file cannot be written the change is undone, so the saved layouts
    // stay as the file has them and the change can be made again.
    struct SavedLayoutsState {
        std::vector<SavedLayout> saved;
        std::map<std::uint64_t, Tab> tabs;
        std::optional<WorkspaceArrangement> quitArrangement;
        QString layout;
        QByteArray lastSaved;
    };
    SavedLayoutsState savedLayoutsState() const;
    // The saved layouts written, or `before` put back (with the notice).
    bool commitSavedLayouts(SavedLayoutsState before);

    QString m_file;
    QByteArray m_default;
    QStringList m_builtPresets;                      // TP1: buildsArrangement
    std::map<QString, QByteArray> m_presetPayloads; // their arrangements, once built
    QByteArray m_lastSaved;
    // The notices of the panel layout (restore failures: a layout chosen
    // after them answers them) and of the saved layouts (files kept aside,
    // writes that failed: they stay until dismissed or, for a write, until
    // one succeeds).
    QStringList m_panelNotices;
    QStringList m_savedNotices;
    void addPanelNotice(const QString &text);
    void addSavedNotice(const QString &text);
    void dismissPanelNotice();
    QString m_chosenMode; // the mode the last explicit choice started
    QString m_preset = QStringLiteral("Editing");
    QString m_layout; // WS3: the saved layout the shown arrangement started from
    bool m_restoring = false;
    bool m_loaded = false; // restoreSaved() has run
    bool m_quitting = false; // saveForQuit() has run
    bool m_fileLocked = false; // the saved file could not be read or copied: never written over
    bool m_menuHasFocus = false;
    bool m_modalOpen = false;
    QTimer m_screenCheck;
};

} // namespace hikari::ui
