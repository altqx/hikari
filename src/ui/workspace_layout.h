#pragma once

#include "hikari/application/tiers.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace hikari::ui {

struct WorkspaceArrangement {
    QString preset = QStringLiteral("Editing");
    QByteArray payload;
    QString layout;
    bool locked = false;

    QByteArray serialise() const;
    static std::optional<WorkspaceArrangement> parse(const QByteArray &bytes, QString *problem = nullptr);
    QByteArray serialiseForFile() const;
    static std::optional<WorkspaceArrangement> parseFromFile(QByteArrayView text, QPoint origin,
                                                             QString *problem = nullptr);
    bool operator==(const WorkspaceArrangement &) const = default;
};

// Each shell window docks under names of its own in the engine; arrangements are stored with the first window's.
struct ShellWindowNames {
    int window = 0;

    QString affinity() const;
    QString engineName(const QString &name) const;
    QByteArray toEngine(const QByteArray &payload) const;
    QByteArray toStored(const QByteArray &payload) const;
};

class WorkspaceLayoutController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(bool hasBackup READ hasBackup NOTIFY changed)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)
    Q_PROPERTY(QStringList presets READ presets CONSTANT)
    Q_PROPERTY(QString layout READ layout NOTIFY changed)
    Q_PROPERTY(QString layoutLabel READ layoutLabel NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    // [{name, usable, problem, tier: "project" | "mine", hidden}], the Project's first (ADR 0030). Name
    // lookups resolve the Project first and skip the hidden ones.
    Q_PROPERTY(QVariantList savedLayouts READ savedLayouts NOTIFY savedLayoutsChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY savedLayoutsChanged)
    // QWindow::isActive is also true for the parent of an active floating panel.
    Q_PROPERTY(QObject *focusWindow READ focusWindow NOTIFY focusWindowChanged)
    // Qt Quick Controls menus are Popup.Item, so the focus window stays the shell's while one is open.
    Q_PROPERTY(bool menuHasFocus READ menuHasFocus NOTIFY menuHasFocusChanged)
    Q_PROPERTY(bool modalOpen READ modalOpen NOTIFY modalOpenChanged)
    Q_PROPERTY(bool nodeEffects READ nodeEffects NOTIFY changed)
public:
    enum class TabStart { Copy, Translation };

    static constexpr int kSchema = 1;
    // MUST KILL: kPanelRegistry is a version int bumped per added panel (2 Timing ... 12 QcNotes, 13 Review, 14 Agent, 15-17 node effects, 18 Explorer)
    static constexpr int kPanelRegistry = 18;
    static constexpr qsizetype kMaximumBytes = 4 * 1024 * 1024;
    static constexpr int kSavedLayoutsSchema = 1;
    static constexpr int kMaximumNameLength = 64;
    static const QStringList &panelIds();
    static constexpr int kMaxPluginPanels = 64;
    // plugin:<plugin id>/<panel id>, a plugin's panel outside panelIds().
    Q_INVOKABLE static QString pluginPanelId(const QString &plugin, const QString &panel);
    Q_INVOKABLE static bool isPluginPanel(const QString &name);
    // {title, pluginName} for a plugin panel, as a layout keeps it for the panel's placeholder.
    Q_INVOKABLE static QVariantMap pluginPanelInfo(const QString &dock);
    Q_INVOKABLE static void rememberPluginPanel(const QString &dock, const QString &title, const QString &pluginName);

    explicit WorkspaceLayoutController(QString layoutFile = {}, QString savedLayoutsFile = {}, QObject *parent = nullptr);

    void setShellWindow(int window) { m_names.window = window; }
    const ShellWindowNames &names() const { return m_names; }

    Q_INVOKABLE void captureDefault();
    Q_INVOKABLE void buildsArrangement(const QString &name);
    Q_INVOKABLE bool restoreSaved();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveForQuit();
    // The file keeps `active`, another window's arrangement (ADR 0025); this window keeps its own.
    bool saveForQuit(const WorkspaceArrangement &active);
    Q_INVOKABLE bool resetLayout();
    Q_INVOKABLE bool applyPreset(const QString &name);
    Q_INVOKABLE static QString presetLabel(const QString &name);
    Q_INVOKABLE static QString presetMode(const QString &preset);
    Q_INVOKABLE void resumeMode(const QString &mode);
    void retranslate() { emit changed(); }
    void reservePresetNames(const QStringList &names);
    void reloadSavedLayouts();
    const QStringList &reservedPresetNames() const { return m_reservedNames; }
    Q_INVOKABLE bool resetCurrentLayout();
    // `tier` "project" or "mine"; empty is Mine.
    Q_INVOKABLE bool saveLayoutAs(const QString &name, const QString &tier = {});
    // [{name, suggestion}] when the other tier has `name`, else [].
    Q_INVOKABLE QVariantList layoutClashes(const QString &name, const QString &from) const;
    // `choices` from the clash review. The name it landed under; empty when skipped or not written.
    Q_INVOKABLE QString copyLayout(const QString &name, const QString &from, const QVariantList &choices = {});
    Q_INVOKABLE QString moveLayout(const QString &name, const QString &from, const QVariantList &choices = {});
    // The window's Project copy; empty without a Project. Rereads both tiers.
    void setProjectLayoutsFile(const QString &file);
    bool hasProject() const { return !m_projectSavedFile.isEmpty(); }
    Q_INVOKABLE bool applySavedLayout(const QString &name);
    Q_INVOKABLE bool renameSavedLayout(const QString &from, const QString &to);
    Q_INVOKABLE bool deleteSavedLayout(const QString &name);
    Q_INVOKABLE QString layoutNameProblem(const QString &name, const QString &renaming = {}) const;
    Q_INVOKABLE QString savedLayoutNamed(const QString &name) const;
    Q_INVOKABLE bool restoreBackup();
    Q_INVOKABLE void dismissNotice();
    Q_INVOKABLE QSize panelSize(QObject *dock) const;
    Q_INVOKABLE bool resizePanel(QObject *dock, int width, int height);
    Q_INVOKABLE int keepFloatingPanelsOnScreen();
    // False while a panel floats: a locked arrangement has none.
    Q_INVOKABLE bool setLocked(bool locked);
    // Node effects have their own layout: entering keeps the tab's arrangement and shows the mode's, leaving swaps back.
    Q_INVOKABLE bool enterNodeEffects();
    Q_INVOKABLE bool leaveNodeEffects();
    bool nodeEffects() const { return m_beforeNodeEffects.has_value(); }

    void showTab(std::uint64_t tab, TabStart start = TabStart::Copy);
    void addTab(std::uint64_t tab, TabStart start = TabStart::Copy);
    void replaceTab(std::uint64_t tab, std::uint64_t replacement, TabStart start = TabStart::Copy);
    void forgetTab(std::uint64_t tab);
    void forgetTabs();
    std::optional<WorkspaceArrangement> tabArrangement(std::uint64_t tab) const;
    void setTabArrangement(std::uint64_t tab, const QByteArray &serialised, const QString &title = {});
    QByteArray fileArrangement(std::uint64_t tab) const;
    bool setFileArrangement(std::uint64_t tab, const QByteArray &text, QString *problem = nullptr);

    QString notice() const { return (m_savedNotices + m_panelNotices).join(QLatin1Char('\n')); }
    QString preset() const { return m_preset; }
    void setPreset(const QString &preset);
    static const QStringList &presets();
    QString layout() const;
    QString layoutLabel() const;
    bool locked() const { return m_locked; }
    QVariantList savedLayouts() const;
    QString savedLayoutsFile() const { return m_savedFile; }
    QObject *focusWindow() const;
    bool menuHasFocus() const { return m_menuHasFocus; }
    bool modalOpen() const { return m_modalOpen; }
    bool hasBackup() const;

    static QByteArray envelope(const QByteArray &payload, const QString &preset = QStringLiteral("Editing"),
                               const QString &layout = {}, bool locked = false);
    static std::optional<QByteArray> payloadOf(const QByteArray &file, QString *problem, QString *preset = nullptr,
                                               QString *layout = nullptr, bool *locked = nullptr);

signals:
    void changed();
    void buildArrangement(const QString &name);
    void focusWindowChanged();
    void menuHasFocusChanged();
    void modalOpenChanged();
    void savedLayoutsChanged();
    void savedLayoutsWritten();
    void arrangementSwapped();
    void presetModeChosen(const QString &mode);
    void presetModeLeft(const QString &mode);
    void presetApplied(const QString &name);
    // Before a restore, the plugin panels its layout names, so that each has a dock to restore into.
    void pluginDocksNeeded(const QStringList &docks);

private:
    void checkModal();
    bool applyPresetArrangement(const QString &name);
    bool applySavedArrangement(const QString &name);
    void choseMode();
    bool restorePayload(const QByteArray &payload, bool inPlace = false);
    QByteArray serialiseWindow() const;
    bool restoreWindow(const QByteArray &payload, int options) const;
    bool restoreSavedFile();
    bool write(const WorkspaceArrangement &arrangement);
    bool buildPreset(const QString &name, bool inPlace);
    bool showArrangement(const WorkspaceArrangement &arrangement);
    WorkspaceArrangement shownArrangement() const;
    void refuseArrangement(const WorkspaceArrangement &arrangement);
    void keepUnrestored(const QByteArray &bytes, const QString &problem, const QString &title);
    void giveTab(std::uint64_t tab, WorkspaceArrangement arrangement);
    QString backupFile() const { return m_file + QStringLiteral(".bak"); }
    QString nodeEffectsFile() const;
    QByteArray tabPayload() const;
    void buildNodeEffects();
    void dropNodeEffects();

    struct SavedLayout {
        QString name;
        std::optional<WorkspaceArrangement> arrangement;
        QString problem;
        QJsonObject record;
        application::TierMark mark;
    };
    void readSavedLayouts();
    std::vector<SavedLayout> readLayoutsFile(const QString &path, bool &locked);
    void lockSavedLayouts(const QString &path, bool &locked);
    bool writeSavedLayouts();
    bool writeLayoutsFile(const QString &path, application::Tier tier, bool &locked);
    int savedLayoutIndex(const QString &name) const;
    int tierLayoutIndex(const QString &name, application::Tier tier) const;
    void markHiddenLayouts();
    QString sendLayout(const QString &name, const QString &from, const QVariantList &choices, bool move);
    void renameLayoutOrigin(const QString &from, const QString &to);
    QString knownLayout(const QString &name) const;
    QStringList m_reservedNames;
    std::vector<SavedLayout> m_saved;
    QString m_savedFile;
    bool m_savedLocked = false;
    QString m_projectSavedFile;
    bool m_projectLocked = false;
    QString m_writeFailure;

    struct Tab {
        std::optional<WorkspaceArrangement> arrangement;
        bool unplaced = false;
    };
    std::map<std::uint64_t, Tab> m_tabs;
    std::optional<std::uint64_t> m_tab;
    std::optional<WorkspaceArrangement> m_quitArrangement;

    struct SavedLayoutsState {
        std::vector<SavedLayout> saved;
        std::map<std::uint64_t, Tab> tabs;
        std::optional<WorkspaceArrangement> quitArrangement;
        QString layout;
        QByteArray lastSaved;
    };
    SavedLayoutsState savedLayoutsState() const;
    bool commitSavedLayouts(SavedLayoutsState before);

    ShellWindowNames m_names;
    QString m_file;
    QByteArray m_default;
    QStringList m_builtPresets;
    std::map<QString, QByteArray> m_presetPayloads;
    QByteArray m_lastSaved;
    QStringList m_panelNotices;
    QStringList m_savedNotices;
    void addPanelNotice(const QString &text);
    void addSavedNotice(const QString &text);
    void dismissPanelNotice();
    QString m_chosenMode;
    QString m_preset = QStringLiteral("Editing");
    QString m_layout;
    bool m_locked = false;
    void takeLock(bool locked);
    bool m_restoring = false;
    bool m_loaded = false;
    bool m_quitting = false;
    bool m_fileLocked = false;
    bool m_menuHasFocus = false;
    bool m_modalOpen = false;
    QTimer m_screenCheck;
    std::optional<QByteArray> m_beforeNodeEffects;
};

}
