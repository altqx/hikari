#pragma once

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

struct WorkspaceArrangement {
    QString preset = QStringLiteral("Editing");
    QByteArray payload;
    QString layout;

    QByteArray serialise() const;
    static std::optional<WorkspaceArrangement> parse(const QByteArray &bytes, QString *problem = nullptr);
    QByteArray serialiseForFile() const;
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
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY changed)
    Q_PROPERTY(QStringList presets READ presets CONSTANT)
    Q_PROPERTY(QString layout READ layout NOTIFY changed)
    Q_PROPERTY(QString layoutLabel READ layoutLabel NOTIFY changed)
    Q_PROPERTY(QVariantList savedLayouts READ savedLayouts NOTIFY savedLayoutsChanged)
    // QWindow::isActive is also true for the parent of an active floating panel.
    Q_PROPERTY(QObject *focusWindow READ focusWindow NOTIFY focusWindowChanged)
    // Qt Quick Controls menus are Popup.Item, so the focus window stays the shell's while one is open.
    Q_PROPERTY(bool menuHasFocus READ menuHasFocus NOTIFY menuHasFocusChanged)
    Q_PROPERTY(bool modalOpen READ modalOpen NOTIFY modalOpenChanged)
public:
    enum class TabStart { Copy, Translation };

    static constexpr int kSchema = 1;
    // MUST KILL: kPanelRegistry is a version int bumped per added panel (2 Timing ... 10 Spellchecker)
    static constexpr int kPanelRegistry = 10;
    static constexpr qsizetype kMaximumBytes = 4 * 1024 * 1024;
    static constexpr int kSavedLayoutsSchema = 1;
    static constexpr int kMaximumNameLength = 64;
    static const QStringList &panelIds();

    explicit WorkspaceLayoutController(QString layoutFile = {}, QString savedLayoutsFile = {}, QObject *parent = nullptr);

    Q_INVOKABLE void captureDefault();
    Q_INVOKABLE void buildsArrangement(const QString &name);
    Q_INVOKABLE bool restoreSaved();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveForQuit();
    Q_INVOKABLE bool resetLayout();
    Q_INVOKABLE bool applyPreset(const QString &name);
    Q_INVOKABLE static QString presetLabel(const QString &name);
    Q_INVOKABLE static QString presetMode(const QString &preset);
    Q_INVOKABLE void resumeMode(const QString &mode);
    void retranslate() { emit changed(); }
    void reservePresetNames(const QStringList &names);
    Q_INVOKABLE bool resetCurrentLayout();
    Q_INVOKABLE bool saveLayoutAs(const QString &name);
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
    QVariantList savedLayouts() const;
    QString savedLayoutsFile() const { return m_savedFile; }
    QObject *focusWindow() const;
    bool menuHasFocus() const { return m_menuHasFocus; }
    bool modalOpen() const { return m_modalOpen; }
    bool hasBackup() const;

    static QByteArray envelope(const QByteArray &payload, const QString &preset = QStringLiteral("Editing"),
                               const QString &layout = {});
    static std::optional<QByteArray> payloadOf(const QByteArray &file, QString *problem, QString *preset = nullptr,
                                               QString *layout = nullptr);

signals:
    void changed();
    void buildArrangement(const QString &name);
    void focusWindowChanged();
    void menuHasFocusChanged();
    void modalOpenChanged();
    void savedLayoutsChanged();
    void arrangementSwapped();
    void presetModeChosen(const QString &mode);
    void presetModeLeft(const QString &mode);

private:
    void checkModal();
    bool applyPresetArrangement(const QString &name);
    bool applySavedArrangement(const QString &name);
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

    struct SavedLayout {
        QString name;
        std::optional<WorkspaceArrangement> arrangement;
        QString problem;
        QJsonObject record;
    };
    void readSavedLayouts();
    void lockSavedLayouts();
    bool writeSavedLayouts();
    int savedLayoutIndex(const QString &name) const;
    void renameLayoutOrigin(const QString &from, const QString &to);
    QString knownLayout(const QString &name) const;
    QStringList m_reservedNames;
    std::vector<SavedLayout> m_saved;
    QString m_savedFile;
    bool m_savedLocked = false;
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
    bool m_restoring = false;
    bool m_loaded = false;
    bool m_quitting = false;
    bool m_fileLocked = false;
    bool m_menuHasFocus = false;
    bool m_modalOpen = false;
    QTimer m_screenCheck;
};

}
