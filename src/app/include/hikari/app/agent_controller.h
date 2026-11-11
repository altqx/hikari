#pragma once

#include "hikari/application/workspace.h"

#include <QAbstractListModel>
#include <QColor>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::api {
class Api;
}
namespace hikari::application {
class AiReview;
class DocumentFiles;
}
namespace hikari::backends::ai {
class AgentThreads;
class ThreadStore;
}
namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class AiScripts;
class AutomationShell;

class AiFeatures;

/// The open thread's timeline. Rows change in place.
class AgentItemsModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KindRole = Qt::UserRole + 1, ItemRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// `items` as the thread's JSON dumps, oldest first.
    void update(std::vector<std::string> items);

private:
    std::vector<std::string> m_dumps;
    std::vector<QVariantMap> m_items;
};

/// The Agent panel over backends::ai::AgentThreads (AI7 #435). With AI off it holds no threads and no engine.
class AgentController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool on READ on NOTIFY switched FINAL)
    // A chat model exists, so a thread can run.
    Q_PROPERTY(bool ready READ ready NOTIFY modelsChanged FINAL)
    // Newest first: {id, title, files, model, cost, costUnknown, state ("running", "cap", "full" or ""), day}.
    Q_PROPERTY(QVariantList threads READ threads NOTIFY threadsChanged FINAL)
    Q_PROPERTY(QString current READ current NOTIFY currentChanged FINAL)
    // The open thread: {id, title, mode, running, queued, cost, cap, costUnknown, contextTokens, contextWindow,
    // model, modelText, attached: [{id, title}], webSearch (the globe), webSearchReady (the provider or a search
    // service can search), webSearchCacheMiss (Anthropic)}; empty with none open.
    Q_PROPERTY(QVariantMap thread READ thread NOTIFY threadChanged FINAL)
    Q_PROPERTY(QObject *items READ items CONSTANT FINAL)
    // [{value, text, provider}] from Settings › AI.
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged FINAL)
    // The open tabs @ can attach: [{id, title}], without the active one.
    Q_PROPERTY(QVariantList attachable READ attachable NOTIFY threadChanged FINAL)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY threadChanged FINAL)
    Q_PROPERTY(bool costMeter READ costMeter NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool fullAccessWarned READ fullAccessWarned NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double spentToday READ spentToday NOTIFY threadChanged FINAL)
    Q_PROPERTY(double dailyCap READ dailyCap NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double threadCap READ threadCap NOTIFY settingsChanged FINAL)
    // Settings › AI lets AI search the web, so the composer shows the globe.
    Q_PROPERTY(bool webSearchShown READ webSearchShown NOTIFY threadChanged FINAL)

public:
    struct Wiring {
        AiFeatures &ai;
        ui::SettingsStore &settings;
        api::Api &api;
        application::AiReview &review;
        application::Workspace &workspace;
        application::DocumentFiles &files;
        QString userFolder;
        std::function<std::int64_t()> now; // ms since the epoch; the clock when null
        AiScripts *scripts = nullptr;
        AutomationShell *automation = nullptr;
    };

    static constexpr const char *kModeSetting = "ai.agent.mode";
    static constexpr const char *kThreadCapSetting = "ai.agent.threadCapCents";
    static constexpr const char *kDailyCapSetting = "ai.agent.dailyCapCents";
    static constexpr const char *kCostMeterSetting = "ai.agent.costMeter";
    static constexpr const char *kFullAccessWarnedSetting = "ai.agent.fullAccessWarned";

    explicit AgentController(Wiring wiring, QObject *parent = nullptr);
    ~AgentController() override;

    bool on() const;
    bool ready() const;
    QVariantList threads() const;
    QString current() const { return QString::fromStdString(m_current); }
    QVariantMap thread() const;
    QObject *items() { return &m_items; }
    QVariantList models() const;
    QVariantList attachable() const;
    QString activeTitle() const;
    bool costMeter() const;
    bool fullAccessWarned() const;
    double spentToday() const;
    double dailyCap() const;
    double threadCap() const;
    bool webSearchShown() const;
    bool running() const;
    backends::ai::AgentThreads *engine() const { return m_threads.get(); }
    /// The tabs or the editing target changed.
    void refreshed() { emit threadChanged(); }

    Q_INVOKABLE void newThread();
    Q_INVOKABLE void open(const QString &id);
    Q_INVOKABLE void rename(const QString &id, const QString &title);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE QString markdown(const QString &id) const;
    Q_INVOKABLE bool exportMarkdown(const QString &id, const QUrl &file) const;
    /// Starts a turn, or queues the message while one runs; opens a thread first when none is open.
    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void steer();
    Q_INVOKABLE void setQueued(const QString &text);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void raiseCap();
    /// "manual", "auto" or "full".
    Q_INVOKABLE void setMode(const QString &mode);
    /// A value from `models`.
    Q_INVOKABLE void setModel(const QString &value);
    /// "allowOnce", "allowInThread" or "deny".
    /// `answer`: allowOnce, allowInThread, deny; for a proposal apply, decline or askAgain.
    Q_INVOKABLE void answer(qulonglong ask, const QString &answer, const QString &message);
    // A script item's saved file, in the user's script editor.
    Q_INVOKABLE void openScript(const QString &file);
    Q_INVOKABLE bool forceStopScript();
    Q_INVOKABLE void selectLine(qulonglong document, qulonglong line);
    Q_INVOKABLE void attach(qulonglong document);
    Q_INVOKABLE void detach(qulonglong document);
    Q_INVOKABLE void setFullAccessWarned();
    Q_INVOKABLE void setWebSearch(bool on);
    /// A text item's answer with its citation chips, as rich text.
    Q_INVOKABLE QString citedText(const QVariantMap &item, const QColor &chip, const QColor &ink) const;
    Q_INVOKABLE QString siteOf(const QString &url) const;
    /// Opens an http or https page in the browser.
    Q_INVOKABLE bool openLink(const QString &url) const;
    /// SearchSuggestions.qml's properties for Google's snippet.
    Q_INVOKABLE QVariantMap suggestions(const QString &html) const;

signals:
    void switched();
    void threadsChanged();
    void currentChanged();
    void threadChanged();
    void modelsChanged();
    void settingsChanged();
    /// A question needs the user, maybe in a thread that isn't open.
    void asked(QString thread);

private:
    void apply();
    void select(std::string id);
    void refreshThread();
    std::vector<std::string> safeFolders() const;
    std::optional<std::string> pathOf(application::DocumentId document) const;

    Wiring m_wiring;
    std::unique_ptr<backends::ai::ThreadStore> m_store;
    std::unique_ptr<backends::ai::AgentThreads> m_threads;
    std::map<std::string, std::vector<application::DocumentId>> m_attached;
    std::string m_current;
    AgentItemsModel m_items;
    int m_busySource = 0;
};

}
