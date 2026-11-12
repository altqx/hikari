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
/// Propose: Manual subtitle edits wait as a proposal the user applies or declines.
/// Script: AI-written Lua, which the user approves on every run in every mode (ADR 0024).
enum class Decision { Run, Judge, Ask, AlwaysAsk, Propose, Script };

/// What makes a call ask in every mode: #410's always-ask categories, and AI settings (ADR 0024). docs/ai-agent.md
/// maps each to its operations, commands and settings.
enum class Risk { None, Files, Programs, Plugins, Network, AiSettings };

struct RiskCheck {
    Risk risk = Risk::None;
    std::string reason;
};

/// `pathOf` gives a granted file's path; `safeFolders` are the user folder, the open subtitles' folders and the
/// Project.
RiskCheck alwaysAsk(const std::string &operation, const api::Json &params,
                    const std::function<std::optional<std::string>(std::string_view handle)> &pathOf,
                    const std::vector<std::string> &safeFolders);

/// Reads, and moving the selection or a panel, never ask: they change no content and no setting.
bool asksPermission(const std::string &operation);

/// `allowed` holds what the user allowed for the whole thread (see allowanceKey).
Decision decide(AgentMode mode, const std::string &operation, bool alwaysAsk, bool allowed);

/// An MCP tool call, from its hints after trust: reads run, destructive tools ask in every mode, tools the user
/// approved run, and the mode decides the rest. `allowed` is "Allow in this thread".
Decision decideMcp(AgentMode mode, bool readOnly, bool destructive, bool approved, bool allowed);

/// What "Allow in this thread" covers: the operation, the one command for commands.run, or the one MCP tool.
std::string allowanceKey(const std::string &operation, const api::Json &params);

/// The judge's request without its model: the action and the user's messages, never file content or tool output.
api::Json judgeRequest(const std::string &operation, const api::Json &params, const std::vector<std::string> &userMessages);

struct Verdict {
    bool approved = false;
    std::string reason;
};

/// Anything but a clear approval escalates.
Verdict judgeVerdict(std::string_view answer);

/// The model's price from its provider's catalogue; nullopt when the catalogue has none.
std::optional<application::ai::Price> priceOf(const application::ai::Config *config,
                                              const application::ai::ModelRef &ref);

/// The context window the sidecar assumes when the catalogue doesn't say.
inline constexpr std::int64_t kDefaultContextTokens = 128'000;
std::int64_t contextWindowOf(const application::ai::Config *config, const application::ai::ModelRef &ref);

/// A model whose provider is "agent:<name>" runs the thread on that external agent ("claude").
inline constexpr std::string_view kAgentProviderPrefix = "agent:";
std::optional<std::string> externalAgentOf(const application::ai::ModelRef &model);

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
    /// The globe; the thread searches only while Settings › AI lets AI search the web too.
    bool webSearch = true;
    /// The Project folder of the window it was started in, UTF-8; its skills join the user's. Empty: none.
    std::string project;

    api::Json toJson() const;
    static std::optional<AgentThread> fromJson(const api::Json &json);
};

/// Applies the sidecar's `compacted` update (docs/ai-agent.md) to what the thread replays. Its items keep the whole
/// conversation; a user item's `history` follows its message, and goes when the summary replaced it.
void applyCompaction(AgentThread &thread, const api::Json &update);

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
        bool autoCompact = true;
    };

    struct Wiring {
        /// Null with AI off.
        std::function<AiRequests *()> requests;
        /// The sidecar for a thread on an external agent, which needs no provider.
        std::function<AiSidecarHost *()> agentHost;
        std::function<const application::ai::Config *()> config;
        api::Api *api = nullptr;
        std::function<Defaults()> defaults;
        /// Opens a review turn for the thread's caller, labelled with the user's message.
        std::function<std::uint64_t(const std::string &caller, const std::string &label)> beginTurn;
        std::function<std::optional<application::DocumentId>()> editingTarget;
        std::function<std::string(application::DocumentId)> documentTitle;
        std::function<std::vector<std::string>()> safeFolders;
        /// The active window's Project folder, UTF-8, which a new thread keeps; empty without one.
        std::function<std::string()> project;
        std::function<std::int64_t()> now; // ms since the epoch
        std::function<QDate()> today;
        /// The DependencyControl modules a Lua script asks for: [{module, version?, installed?}].
        std::function<api::Json(const std::string &script)> scriptNeeds;
    };

    /// A proposal takes AllowOnce (Apply), Deny (Decline) or AskAgain, once the document changed under it; a script
    /// takes AllowOnce (Run) or Deny.
    enum class Answer { AllowOnce, AllowInThread, Deny, AskAgain };

    /// An open permission question.
    struct Ask {
        std::uint64_t id = 0;
        std::string thread;
        Decision kind = Decision::Ask; // Ask, Judge (the judge escalated), AlwaysAsk or Propose
        std::string operation;
        api::Json params;
        std::string reason;
        std::optional<application::DocumentId> document; // a proposal's
        bool stale = false;                               // a proposal whose document changed
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
    void setWebSearch(const std::string &id, bool on);
    bool searches(const AgentThread &thread) const;
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
    /// Compacts what the thread replays to the model; while a turn runs, it waits for the turn to end.
    void compact(const std::string &id, std::string instruction = {});
    /// A compaction runs or waits for the turn.
    bool compacting(const std::string &id) const;
    /// Summarises the thread into a new one; `summarised` names it when the summary arrives.
    void newThreadFromSummary(const std::string &id);
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
    void summarised(QString from, QString to);

private:
    struct Run;
    AiSidecarHost *host(bool externalAgent = false);
    AgentThread *find(const std::string &id);
    void start(AgentThread &thread, std::string text, api::Json parts);
    void openSession(AgentThread &thread, AiSidecarHost &host, const application::ai::ModelRef &model);
    void startCompaction(AgentThread &thread, std::string instruction, bool apply);
    void compactionDone(const std::string &id, const SidecarResult &result, bool apply);
    void runQueued(AgentThread &thread);
    void finished(const std::string &id, const SidecarResult &result);
    void sessionUpdate(const api::Json &params);
    void gate(const std::string &session, const std::string &operation, const api::Json &params,
              AiSidecarHost::Admit admit, std::function<void(std::function<void()>)> onWithdraw);
    void gateMcp(AgentThread &thread, const api::Json &params, AiSidecarHost::Admit admit,
                 std::function<void(std::function<void()>)> onWithdraw);
    void judge(AgentThread &thread, const std::string &operation, const api::Json &params, AiSidecarHost::Admit admit,
               std::function<void(std::function<void()>)> onWithdraw);
    void openAsk(AgentThread &thread, Decision kind, const std::string &operation, const api::Json &params,
                 std::string reason, AiSidecarHost::Admit admit, std::function<void(std::function<void()>)> onWithdraw);
    void openProposal(AgentThread &thread, const api::Json &params, AiSidecarHost::Admit admit,
                      std::function<void(std::function<void()>)> onWithdraw);
    void openScript(AgentThread &thread, const api::Json &params, AiSidecarHost::Admit admit,
                    std::function<void(std::function<void()>)> onWithdraw);
    void runScript(AgentThread &thread, std::uint64_t ask, AiSidecarHost::Admit admit);
    void wait(AgentThread &thread, Ask ask, api::Json item, AiSidecarHost::Admit admit,
              std::function<void(std::function<void()>)> onWithdraw);
    void documentChanged(application::DocumentId document, std::int64_t revision);
    void admitCall(AgentThread &thread, const std::string &operation, const api::Json &params, bool judged,
                   AiSidecarHost::Admit admit);
    api::Caller callerOf(const AgentThread &thread) const;
    void addUsage(AgentThread &thread, const api::Json &usage, bool context = true);
    void addSources(AgentThread &thread, Run *run, const api::Json &update);
    void searchFailed(AgentThread &thread, Run *run, const api::Json &update);
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
    std::map<std::string, std::string> m_compactAfter; // the instruction, by thread
    std::uint64_t m_nextAsk = 1;
    std::uint64_t m_documentEvents = 0;
    QPointer<AiSidecarHost> m_host;
    std::set<std::string> m_live; // sessions the running sidecar holds
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
