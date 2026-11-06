#pragma once

#include "hikari/api/json.h"
#include "hikari/plugins/manifest.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hikari::app {

// What a plugin panel's place shows: the plugin's widget tree while it runs, else why it can't.
class PluginPanelState : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dock READ dock CONSTANT)
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(QString panelId READ panelId CONSTANT)
    // "missing", "off", "running", "stopped" or "failed".
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    // Empty while the plugin isn't installed: the layout's saved title stands in.
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString pluginName READ pluginName NOTIFY changed)
    Q_PROPERTY(QUrl packageRoot READ packageRoot NOTIFY changed)
    // The last tree hikari.panels.render gave, without its menu; null until the first render.
    Q_PROPERTY(QVariant tree READ tree NOTIFY treeChanged)
    Q_PROPERTY(QVariantList menu READ menu NOTIFY treeChanged)

public:
    PluginPanelState(QString dock, QString pluginId, QString panelId, QObject *parent);

    QString dock() const { return m_dock; }
    QString pluginId() const { return m_pluginId; }
    QString panelId() const { return m_panelId; }
    QString status() const { return m_status; }
    QString reason() const { return m_reason; }
    QString title() const { return m_title; }
    QString pluginName() const { return m_pluginName; }
    QUrl packageRoot() const { return m_packageRoot; }
    QVariant tree() const { return m_tree; }
    QVariantList menu() const { return m_menu; }

    void describe(QString status, QString reason, QString title, QString pluginName, QUrl packageRoot);
    void setTree(QVariant tree, QVariantList menu);

signals:
    void changed();
    void treeChanged();

private:
    QString m_dock, m_pluginId, m_panelId;
    QString m_status = QStringLiteral("missing");
    QString m_reason, m_title, m_pluginName;
    QUrl m_packageRoot;
    QVariant m_tree;
    QVariantList m_menu;
};

// One open hikari.dialogs.show or ask.
class PluginDialogState : public QObject {
    Q_OBJECT
    // plugin.<plugin id>.<dialog id>, also the dialog's placement id.
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString pluginName READ pluginName CONSTANT)
    Q_PROPERTY(QUrl packageRoot READ packageRoot CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QVariant body READ body NOTIFY changed)
    // [{id, text, style, default, cancel, stays_open}]
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(bool modal READ modal NOTIFY changed)

public:
    PluginDialogState(QString key, QString pluginName, QUrl packageRoot, QObject *parent);

    QString key() const { return m_key; }
    QString pluginName() const { return m_pluginName; }
    QUrl packageRoot() const { return m_packageRoot; }
    QString title() const { return m_title; }
    QVariant body() const { return m_body; }
    QVariantList buttons() const { return m_buttons; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    bool modal() const { return m_modal; }

    void present(const api::Json &spec, bool modal);

signals:
    void changed();

private:
    QString m_key, m_pluginName;
    QUrl m_packageRoot;
    QString m_title;
    QVariant m_body;
    QVariantList m_buttons;
    int m_width = 0;
    int m_height = 0;
    bool m_modal = false;
};

// The file picker or folder request a plugin waits on, for QML to show.
class PluginFileRequest : public QObject {
    Q_OBJECT
    // "open", "save" or "folder".
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString pluginName READ pluginName CONSTANT)
    // The picker's title from the plugin, or the folder request's title.
    Q_PROPERTY(QString title READ title CONSTANT)
    // Why a folder is asked for, from the manifest.
    Q_PROPERTY(QString reason READ reason CONSTANT)
    Q_PROPERTY(bool writable READ writable CONSTANT)
    Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT)
    // Where a save picker starts: the suggested name in Documents, or empty.
    Q_PROPERTY(QUrl file READ file CONSTANT)

public:
    using Answer = std::function<void(std::optional<QString> path, bool remember)>;
    PluginFileRequest(QString kind, QString pluginName, QString title, QString reason, bool writable, QStringList nameFilters,
                      QString name, Answer answer, QObject *parent);

    QString kind() const { return m_kind; }
    QString pluginName() const { return m_pluginName; }
    QString title() const { return m_title; }
    QString reason() const { return m_reason; }
    bool writable() const { return m_writable; }
    QStringList nameFilters() const { return m_nameFilters; }
    QUrl file() const { return m_file; }

    // `remember` keeps a folder for the plugin's later runs.
    Q_INVOKABLE void accept(const QUrl &chosen, bool remember = false);
    Q_INVOKABLE void reject();

signals:
    void settled();

private:
    QString m_kind, m_pluginName, m_title, m_reason;
    bool m_writable = false;
    QStringList m_nameFilters;
    QUrl m_file;
    Answer m_answer;
};

// The panels and dialogs of the installed plugins, between the plugin helpers and QML.
class PluginUi : public QObject {
    Q_OBJECT
    // [{dock, plugin, pluginName, panel, title, area ("left", "right" or "bottom")}] of the plugins turned on,
    // by title.
    Q_PROPERTY(QVariantList panels READ panels NOTIFY panelsChanged)
    // The oldest picker or folder request still waiting, or null.
    Q_PROPERTY(hikari::app::PluginFileRequest *fileRequest READ fileRequest NOTIFY fileRequestChanged)

public:
    struct Plugin {
        std::string id;
        std::string name;
        bool enabled = false;
        std::vector<plugins::Panel> panels;
        QUrl root;
    };
    // Hands an event to a running plugin's helper; false when it isn't running.
    using Deliver = std::function<bool(const std::string &plugin, const std::string &event, const std::string &json,
                                       const std::string &coalesceKey)>;
    // The answer to the helper: {"result": …} or {"error": {code, message}}.
    using Reply = std::function<void(std::string)>;
    struct FileAsk {
        QString kind; // "open", "save" or "folder"
        QString title;
        QString reason;
        bool writable = false;
        QStringList nameFilters;
        QString name;
    };
    // {file, name, access} for a file dropped on one of the plugin's panels, or null.
    using GrantDrop = std::function<api::Json(const std::string &plugin, const QString &path)>;

    explicit PluginUi(Deliver deliver, QObject *parent = nullptr);
    ~PluginUi() override;

    // Every installed plugin, after each sync. A plugin that went off closes its dialogs.
    void setPlugins(std::vector<Plugin> plugins);
    // The plugin's helper loaded: its shown panels are asked to draw.
    void running(const std::string &plugin);
    // status "stopped" or "failed": its panels lose their trees and its dialogs close.
    void stopped(const std::string &plugin, const QString &status, const QString &reason);
    void serve(const std::string &plugin, const std::string &operation, const api::Json &params, Reply reply);
    // Queued behind other plugins' requests; never answered when the plugin stops first.
    void askForFile(const std::string &plugin, FileAsk ask, PluginFileRequest::Answer answer);
    void setGrantDrop(GrantDrop grant) { m_grantDrop = std::move(grant); }
    PluginFileRequest *fileRequest() const { return m_fileRequests.empty() ? nullptr : m_fileRequests.front().second; }
    // Files dropped on a panel whose tree has `drop`: granted for reading and sent as a "drop" event.
    // False when the panel doesn't take drops.
    Q_INVOKABLE bool drop(const QString &dock, const QList<QUrl> &files);

    QVariantList panels() const { return m_rows; }
    // {status ("missing", "off", "running", "stopped" or "failed"), reason} of an installed plugin.
    Q_INVOKABLE QVariantMap pluginState(const QString &plugin) const;
    // Made on first use for any plugin panel id, so a layout's placeholder has one too.
    Q_INVOKABLE hikari::app::PluginPanelState *panel(const QString &dock);
    Q_INVOKABLE void setShown(const QString &dock, bool shown);
    // A widget's event in a panel: {kind, widget, value, …}. "change" and "select" keep the value.
    Q_INVOKABLE void send(const QString &dock, const QVariantMap &event);
    Q_INVOKABLE void dialogEvent(const QString &key, const QVariantMap &event);
    Q_INVOKABLE void press(const QString &key, const QString &button);
    // The close glyph or Esc: the cancel button when there is one.
    Q_INVOKABLE void dismiss(const QString &key);
    // A widget's current value in a panel or dialog.
    Q_INVOKABLE QVariant value(const QString &surface, const QString &widget) const;

signals:
    void panelsChanged();
    void pluginStatesChanged();
    void showRequested(const QString &dock);
    void dialogOpened(hikari::app::PluginDialogState *dialog);
    void dialogClosed(hikari::app::PluginDialogState *dialog);
    void fileRequestChanged();

private:
    struct Dialog {
        std::string plugin;
        std::string id;
        PluginDialogState *state = nullptr;
        api::Json spec;
        api::Json values;
        std::optional<Reply> asked;
    };
    struct Known {
        Plugin plugin;
        QString status = QStringLiteral("running");
        QString reason;
    };

    void refresh(PluginPanelState &state);
    void relist();
    void close(const QString &key);
    void closeDialogsOf(const std::string &plugin);
    void dropFileRequestsOf(const std::string &plugin);
    void answered(PluginFileRequest *request);
    void deliver(const std::string &plugin, const std::string &event, api::Json data, const std::string &key);
    void present(const std::string &plugin, const api::Json &params, std::optional<Reply> asked, const Reply &reply);

    Deliver m_deliver;
    std::map<std::string, Known> m_plugins;
    std::map<QString, PluginPanelState *> m_panels;
    std::map<QString, api::Json> m_values;
    std::set<QString> m_shown;
    std::map<QString, Dialog> m_dialogs;
    QVariantList m_rows;
    std::deque<std::pair<std::string, PluginFileRequest *>> m_fileRequests;
    GrantDrop m_grantDrop;
};

}
