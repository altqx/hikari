#pragma once

#include "hikari/api/api.h"
#include "hikari/api/json.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/write_coordinator.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::backends::ai {

class AiRequests;

/// A thread's permission mode (ADR 0024); Qt enforces it on every call.
enum class AgentMode { Manual, Auto, FullAccess };

std::string_view agentModeId(AgentMode mode);
std::optional<AgentMode> agentModeFromId(std::string_view id);

/// What Qt does with an agent's call before HA1 runs it.
enum class Decision { Run, Judge, Ask, AlwaysAsk };

/// Why the call asks in every mode, or empty. `pathOf` gives a granted file's path; `safeFolders` are the user
/// folder, the open subtitles' folders and the Project.
std::string alwaysAskReason(const std::string &operation, const api::Json &params,
                            const std::function<std::optional<std::string>(std::string_view handle)> &pathOf,
                            const std::vector<std::string> &safeFolders);

/// Reads, and moving the selection or a panel, never ask: they change no content and no setting.
bool asksPermission(const std::string &operation);

/// `allowed` holds what the user allowed for the whole thread (see allowanceKey).
Decision decide(AgentMode mode, const std::string &operation, bool alwaysAsk, bool allowed);

/// What "Allow in this thread" covers: the operation, or the one command for commands.run.
std::string allowanceKey(const std::string &operation, const api::Json &params);

/// The judge's request without its model: the action and the user's messages, never file content or tool output.
api::Json judgeRequest(const std::string &operation, const api::Json &params, const std::vector<std::string> &userMessages);

struct Verdict {
    bool approved = false;
    std::string reason;
};

/// Anything but a clear approval escalates.
Verdict judgeVerdict(std::string_view answer);

/// One conversation in the agent panel. Qt owns it and saves it in the user folder.
struct AgentThread {
    std::string id;
    std::string title;
    bool renamed = false;
    std::int64_t created = 0; // ms since the epoch
    std::int64_t updated = 0;
    AgentMode mode = AgentMode::Auto;
    std::optional<application::ai::ModelRef> model;
    /// The messages the sidecar keeps, in the protocol's shape; replayed into session/new.
    api::Json history = api::Json::array();
    /// What the panel shows, oldest first; each item has a `kind` (see docs/ai-agent.md).
    api::Json items = api::Json::array();
    /// The Documents its changes went to, by title.
    std::vector<std::string> files;
    double cost = 0;
    /// Some turn ran on a model whose cost nobody reports.
    bool costUnknown = false;
    std::int64_t contextTokens = 0;
    /// The soft cap in US dollars; 0 means none.
    double cap = 0;
    std::set<std::string> allowed;

    api::Json toJson() const;
    static std::optional<AgentThread> fromJson(const api::Json &json);
};

/// Saves threads as <folder>/<id>.json, and the spending per day as <folder>/spend.json.
class ThreadStore {
public:
    explicit ThreadStore(QString folder);

    std::vector<AgentThread> load() const;
    bool save(const AgentThread &thread) const;
    void remove(const std::string &id) const;
    double spentOn(QDate day) const;
    void spend(QDate day, double dollars);

private:
    QString m_folder;
    std::map<QString, double> m_spend; // by ISO day
};

/// Runs the agent's threads in the sidecar and decides every call they make. Settings and the review come in
/// through Wiring, so the class has no view of the window.
class AgentThreads final : public QObject {
    Q_OBJECT
public:
    struct Defaults {
        AgentMode mode = AgentMode::Auto;
        double threadCap = 2.0;
        double dailyCap = 10.0;
    };

    struct Wiring {
        /// Null with AI off.
        std::function<AiRequests *()> requests;
        std::function<const application::ai::Config *()> config;
        api::Api *api = nullptr;
        std::function<Defaults()> defaults;
        /// Opens a review turn for the thread's caller, labelled with the user's message.
        std::function<std::uint64_t(const std::string &caller, const std::string &label)> beginTurn;
        std::function<std::optional<application::DocumentId>()> editingTarget;
        std::function<std::string(application::DocumentId)> documentTitle;
        std::function<std::vector<std::string>()> safeFolders;
        std::function<std::int64_t()> now; // ms since the epoch
        std::function<QDate()> today;
    };

    enum class Answer { AllowOnce, AllowInThread, Deny };

    /// An open permission question.
    struct Ask {
        std::uint64_t id = 0;
        std::string thread;
        Decision kind = Decision::Ask; // Ask, Judge (the judge escalated) or AlwaysAsk
        std::string operation;
        api::Json params;
        std::string reason;
    };

    AgentThreads(Wiring wiring, ThreadStore *store, QObject *parent = nullptr);
    ~AgentThreads() override;

    /// Newest first.
    std::vector<const AgentThread *> threads() const;
    const AgentThread *thread(const std::string &id) const;
    std::string create();
    void rename(const std::string &id, std::string title);
    void remove(const std::string &id);
    void setMode(const std::string &id, AgentMode mode);
    void setModel(const std::string &id, application::ai::ModelRef model);
    /// Tabs the user attached with @, besides the editing target.
    void setAttached(const std::string &id, std::vector<application::DocumentId> documents);
    /// The model a thread's next turn runs on: its own, else the chat model.
    std::optional<application::ai::ModelRef> modelOf(const AgentThread &thread) const;

    /// Starts a turn with `parts` (protocol Parts: text, and images for attachments), or queues it while one runs.
    void send(const std::string &id, std::string text, api::Json extraParts = api::Json::array());
    bool running(const std::string &id) const;
    /// The message waiting for the running turn to end.
    std::string queued(const std::string &id) const;
    void setQueued(const std::string &id, std::string text);
    /// Sends the queued message into the running turn at its next tool call.
    void steer(const std::string &id);
    void stop(const std::string &id);
    /// Sends the interrupted turn's message again.
    void retry(const std::string &id);
    /// Raises the thread's cap by one more default cap.
    void raiseCap(const std::string &id);
    double spentToday() const;

    const std::map<std::uint64_t, Ask> &asks() const { return m_asks; }
    /// `message` goes to the agent with a denial.
    void answer(std::uint64_t ask, Answer answer, const std::string &message = {});

signals:
    void threadsChanged();
    void threadChanged(QString id);
    void asked(quint64 ask);

private:
    struct Run;
    AiSidecarHost *host();
    AgentThread *find(const std::string &id);
    void start(AgentThread &thread, std::string text, api::Json parts);
    void finished(const std::string &id, const SidecarResult &result);
    void sessionUpdate(const api::Json &params);
    void gate(const std::string &session, const std::string &operation, const api::Json &params,
              AiSidecarHost::Admit admit, std::function<void(std::function<void()>)> onWithdraw);
    void judge(AgentThread &thread, const std::string &operation, const api::Json &params, AiSidecarHost::Admit admit,
               std::function<void(std::function<void()>)> onWithdraw);
    void openAsk(AgentThread &thread, Decision kind, const std::string &operation, const api::Json &params,
                 std::string reason, AiSidecarHost::Admit admit, std::function<void(std::function<void()>)> onWithdraw);
    void admitCall(AgentThread &thread, const std::string &operation, const api::Json &params, bool judged,
                   AiSidecarHost::Admit admit);
    api::Caller callerOf(const AgentThread &thread) const;
    void addUsage(AgentThread &thread, const api::Json &usage);
    void stopFor(AgentThread &thread, const std::string &notice);
    void closeAsks(const std::string &thread, const std::string &why);
    void push(AgentThread &thread, api::Json item);
    void touch(AgentThread &thread);
    void sessionsLost();
    std::int64_t nowMs() const;
    QDate today() const;

    Wiring m_wiring;
    ThreadStore *m_store;
    std::map<std::string, AgentThread> m_threads;
    std::map<std::string, std::unique_ptr<Run>> m_runs;
    std::map<std::string, std::vector<application::DocumentId>> m_attached;
    std::map<std::uint64_t, Ask> m_asks;
    std::map<std::uint64_t, AiSidecarHost::Admit> m_waiting;
    std::map<std::string, std::string> m_queued;
    std::uint64_t m_nextAsk = 1;
    QPointer<AiSidecarHost> m_host;
    std::set<std::string> m_live; // sessions the running sidecar holds
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
