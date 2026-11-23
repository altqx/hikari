#pragma once

#include "hikari/application/ai_providers.h"
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
#include <utility>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::api {
class Api;
}
namespace hikari::json {
class Json;
}
namespace hikari::application {
class AiReview;
class DocumentFiles;
}
namespace hikari::backends::ai {
struct AgentThread;
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
    // A provider model or an agent exists, so the composer can choose one.
    Q_PROPERTY(bool ready READ ready NOTIFY modelsChanged FINAL)
    // Newest first: {id, title, files, model, cost, costUnknown, state ("running", "cap", "full" or ""), day}.
    Q_PROPERTY(QVariantList threads READ threads NOTIFY threadsChanged FINAL)
    Q_PROPERTY(QString current READ current NOTIFY currentChanged FINAL)
    // The open thread: {id, title, mode, running, queued, cost, cap, costUnknown, contextTokens, contextWindow,
    // model, modelText, attached: [{id, title}]}; empty with none open.
    Q_PROPERTY(QVariantMap thread READ thread NOTIFY threadChanged FINAL)
    Q_PROPERTY(QObject *items READ items CONSTANT FINAL)
    // [{value, text, provider, agent}]: the found agents (agent: its program) at their default, then Settings › AI's.
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged FINAL)
    // The found agents: [{agent, text, value}], value being the agent at its own default model.
    Q_PROPERTY(QVariantList agents READ agents NOTIFY modelsChanged FINAL)
    // Bumps whenever an agent's model list or its loading changes.
    Q_PROPERTY(int agentModelsRevision READ agentModelsRevision NOTIFY agentModelsChanged FINAL)
    Q_PROPERTY(bool agentModelsLoading READ agentModelsLoading NOTIFY agentModelsChanged FINAL)
    /// The model a new thread starts with, or empty when none is configured.
    Q_PROPERTY(QString defaultModelText READ defaultModelText NOTIFY modelsChanged FINAL)
    /// That model's value, or empty.
    Q_PROPERTY(QString defaultModel READ defaultModel NOTIFY modelsChanged FINAL)
    // The open tabs @ can attach: [{id, title}], without the active one.
    Q_PROPERTY(QVariantList attachable READ attachable NOTIFY threadChanged FINAL)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY threadChanged FINAL)
    Q_PROPERTY(bool costMeter READ costMeter NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool autoCompact READ autoCompact NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool fullAccessWarned READ fullAccessWarned NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double spentToday READ spentToday NOTIFY threadChanged FINAL)
    Q_PROPERTY(double dailyCap READ dailyCap NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double threadCap READ threadCap NOTIFY settingsChanged FINAL)

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
        // The active window's Project folder; empty without one.
        std::function<QString()> project;
        // Settings › AI's last known key and sign-in state; unset knows none.
        std::function<bool(const QString &providerId)> lacksKey;
        std::function<bool()> codexSignedOut;
    };

    static constexpr const char *kModeSetting = "ai.agent.mode";
    static constexpr const char *kThreadCapSetting = "ai.agent.threadCapCents";
    static constexpr const char *kDailyCapSetting = "ai.agent.dailyCapCents";
    static constexpr const char *kCostMeterSetting = "ai.agent.costMeter";
    static constexpr const char *kAutoCompactSetting = "ai.agent.autoCompact";
    static constexpr const char *kFullAccessWarnedSetting = "ai.agent.fullAccessWarned";
    static constexpr const char *kAgentModelsSetting = "ai.agent.models";
    static constexpr const char *kRecentModelsSetting = "ai.models.recent";
    static constexpr int kRecentModels = 5;

    explicit AgentController(Wiring wiring, QObject *parent = nullptr);
    ~AgentController() override;

    bool on() const;
    bool ready() const;
    QVariantList threads() const;
    QString current() const { return QString::fromStdString(m_current); }
    QVariantMap thread() const;
    QObject *items() { return &m_items; }
    QVariantList models() const;
    QString defaultModelText() const;
    QString defaultModel() const;
    QVariantList agents() const;
    int agentModelsRevision() const { return m_agentModelsRevision; }
    /// {loading, models: [{value, text}]}: the agent's last known list, while a fresh one may be loading.
    Q_INVOKABLE QVariantMap agentModels(const QString &agent) const;
    /// Asks a found agent for its models, once per run.
    Q_INVOKABLE void loadAgentModels(const QString &agent);
    /// Asks again after a failed load.
    Q_INVOKABLE void retryAgentModels(const QString &agent);
    bool agentModelsLoading() const;
    /// The model picker's items, `current` being marked; see modelPickerItems.
    Q_INVOKABLE QVariantList pickerItems(const QString &current, const QString &filter) const;
    QVariantList attachable() const;
    QString activeTitle() const;
    bool costMeter() const;
    bool autoCompact() const;
    bool fullAccessWarned() const;
    double spentToday() const;
    /// Counts toward the day's cap with the threads' spending; inline actions use it.
    void spend(double dollars);
    double dailyCap() const;
    double threadCap() const;
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
    Q_INVOKABLE void stopChild(const QString &id);
    Q_INVOKABLE void stopChildren();
    Q_INVOKABLE void openParent();
    Q_INVOKABLE void steer();
    Q_INVOKABLE void setQueued(const QString &text);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void raiseCap();
    /// Compacts the open thread now, or once its turn ends.
    Q_INVOKABLE void compact(const QString &instruction = {});
    Q_INVOKABLE void newThreadFromSummary();
    /// "manual", "auto" or "full".
    Q_INVOKABLE void setMode(const QString &mode);
    /// A value from `models`; it goes first in `ai.models.recent`.
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
    /// A text item's answer with its citation chips, as rich text.
    Q_INVOKABLE QString citedText(const QVariantMap &item, const QColor &chip, const QColor &ink) const;
    Q_INVOKABLE QString siteOf(const QString &url) const;
    /// Opens an http or https page in the browser.
    Q_INVOKABLE bool openLink(const QString &url) const;
    /// SearchSuggestions.qml's properties for Google's snippet.
    Q_INVOKABLE QVariantMap suggestions(const QString &html) const;
    /// Opens the open thread's Project AGENTS.md in the system editor.
    Q_INVOKABLE void openProjectInstructions() const;
    /// The active window or its Project changed: the thread list puts that Project's threads first.
    void projectChanged();

signals:
    void switched();
    void threadsChanged();
    void currentChanged();
    void threadChanged();
    void modelsChanged();
    void agentModelsChanged();
    void settingsChanged();
    /// A question needs the user, maybe in a thread that isn't open.
    void asked(QString thread);

private:
    void apply();
    /// The first installed agent, which a thread falls back to when no provider has a Chat model.
    std::optional<application::ai::ModelRef> foundAgent() const;
    std::optional<application::ai::ModelRef> defaultRef() const;
    QString modelTextOf(const application::ai::ModelRef &ref) const;
    void fallBackToAnAgent(const std::string &thread);
    void select(std::string id);
    void refreshThread();
    json::Json childCard(const backends::ai::AgentThread &child) const;
    std::vector<std::string> safeFolders() const;
    std::optional<std::string> pathOf(application::DocumentId document) const;
    QString projectInstructionsFile(const backends::ai::AgentThread &thread) const;

    Wiring m_wiring;
    std::unique_ptr<backends::ai::ThreadStore> m_store;
    std::unique_ptr<backends::ai::AgentThreads> m_threads;
    std::map<std::string, std::vector<application::DocumentId>> m_attached;
    struct AgentModelList {
        std::vector<std::pair<QString, QString>> models;
        bool loading = false;
        bool asked = false;
        std::optional<QString> failure;
    };
    AgentModelList &agentModelList(const std::string &agent);
    bool agentFound(const std::string &agent) const;
    void saveAgentModels();
    std::map<std::string, AgentModelList> m_agentModels;
    int m_agentModelsRevision = 0;
    std::string m_current;
    AgentItemsModel m_items;
    int m_busySource = 0;
};

}
