#include "hikari/backends/ai_agent.h"

#include "hikari/application/hotkeys.h"
#include "hikari/backends/ai_requests.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <utility>
#include <variant>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;
using api::Json;

namespace {

constexpr std::string_view kSystemPrompt =
    "You are Hikari AI, the assistant in HikariSub, a subtitle editor. You read and edit the user's subtitles only "
    "through your tools. Read the Lines before you change them, keep the user's style and override tags, and edit "
    "with document_apply: name each change for the user, such as \"Shortened 2 Lines\". Your changes wait in the "
    "user's review until they keep or undo them. Some actions need the user's approval; when one is denied, don't "
    "try it again: ask what they want. When the work has steps, show them with update_plan. Answer briefly.";

constexpr std::string_view kJudgeSystem =
    "You check one action that an AI agent in HikariSub, a subtitle editor, wants to take for the user. You see the "
    "action and the user's own messages, nothing else. Approve the action when it plainly serves what the user asked "
    "for. Pass it to the user when it goes beyond that: when it changes settings, files or commands they didn't ask "
    "about, or could lose their work. Answer with JSON only: {\"decision\": \"approve\"} or {\"decision\": "
    "\"escalate\", \"reason\": \"<one short sentence for the user>\"}.";

constexpr std::size_t kJudgeActionChars = 8000;
constexpr std::size_t kJudgeMessagesChars = 8000;
constexpr std::size_t kOutputChars = 4000;
constexpr std::size_t kTitleChars = 60;

struct RiskyName {
    std::string_view name;
    Risk risk;
    std::string_view reason;
};

constexpr std::string_view kRunsScript = "It runs an Automation script.";

constexpr std::array kRiskyCommands{
    RiskyName{"GLOBAL_DELETE_TEMPORARY_FILES", Risk::Files, "It deletes files."},
    RiskyName{"GLOBAL_AUTOMATION_LOAD_SCRIPT", Risk::Programs, kRunsScript},
    RiskyName{"GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT", Risk::Programs, kRunsScript},
    RiskyName{"GLOBAL_AUTOMATION_REPEAT_LAST_RUN", Risk::Programs, kRunsScript},
    RiskyName{"GLOBAL_AUTOMATION_RELOAD_AUTOLOAD", Risk::Programs, "It runs the Autoload scripts."},
};

constexpr std::array kRiskySettings{
    RiskyName{"automation.scriptEditor", Risk::Programs, "It changes the program that opens Automation scripts."},
    RiskyName{"automation.lastScript", Risk::Programs, "It changes the script that Run last script runs."},
    RiskyName{"automation.disabledAutoload", Risk::Programs, "It turns Autoload scripts on or off."},
    RiskyName{"updater.autoInstall", Risk::Programs, "It lets HikariSub install its updates without asking."},
    RiskyName{"plugins.autoUpdate", Risk::Plugins, "It lets plugins install their updates without asking."},
    RiskyName{"plugins.network", Risk::Plugins, "It lets plugins reach the network."},
    RiskyName{"automation.trustedFolders", Risk::Plugins, "It trusts the Automation scripts in more folders."},
};

constexpr std::array kRiskySettingPrefixes{
    RiskyName{"privacy.", Risk::Network, "It changes what HikariSub sends over the network."},
    RiskyName{"ai.", Risk::AiSettings, "It changes an AI setting."},
};

constexpr std::array<std::string_view, 4> kViewWrites{"selection.set", "panels.open", "panels.close", "panels.focus"};

std::string stringAt(const Json &object, std::string_view key)
{
    const Json *value = object.isObject() ? object.find(key) : nullptr;
    return value && value->isString() ? value->asString() : std::string();
}

std::string clipped(std::string text, std::size_t limit)
{
    if (text.size() <= limit)
        return text;
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    text.resize(cut);
    return text + "…";
}

QString cleanFolder(const std::string &path)
{
    QString clean = QDir::cleanPath(QString::fromStdString(path));
    if (!clean.endsWith(QLatin1Char('/')))
        clean += QLatin1Char('/');
    return clean;
}

bool under(const std::string &path, const std::string &folder)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity kCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kCase = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QString::fromStdString(path)).startsWith(cleanFolder(folder), kCase);
}

std::string toolName(std::string operation)
{
    std::replace(operation.begin(), operation.end(), '.', '_');
    return operation;
}

std::string firstLine(const std::string &text)
{
    std::string line = text.substr(0, text.find('\n'));
    const auto start = line.find_first_not_of(" \t");
    return clipped(start == std::string::npos ? std::string() : line.substr(start), kTitleChars);
}

std::string randomId()
{
    const quint64 value = QRandomGenerator::global()->generate64();
    return QString::number(value, 16).rightJustified(16, QLatin1Char('0')).toStdString();
}

Json withoutImages(Json message)
{
    if (const Json *parts = message.find("parts"); parts && parts->isArray()) {
        Json kept = Json::array();
        for (const Json &part : parts->asArray())
            kept.push(stringAt(part, "type") == "image"
                          ? Json::object({{"type", "text"}, {"text", "[an image the user attached]"}})
                          : part);
        message["parts"] = std::move(kept);
    }
    return message;
}

bool isWebTool(std::string_view tool)
{
    return tool == "web_search" || tool == "web_fetch";
}

qsizetype utf16Size(const std::string &text)
{
    return QString::fromStdString(text).size();
}

/// The pages a web tool's answer names; the rest of it isn't kept.
Json urlsOf(const std::string &tool, const std::string &output)
{
    Json urls = Json::array();
    const auto parsed = Json::parse(output);
    if (!parsed || !parsed->isObject())
        return urls;
    if (tool == "web_fetch") {
        if (std::string url = stringAt(*parsed, "url"); !url.empty())
            urls.push(std::move(url));
        return urls;
    }
    if (const Json *results = parsed->find("results"); results && results->isArray())
        for (const Json &r : results->asArray())
            if (std::string url = stringAt(r, "url"); !url.empty())
                urls.push(std::move(url));
    return urls;
}

/// Search services' terms don't let us store their results, so a finished turn keeps only that it searched.
void dropSearchResults(AgentThread &thread, const std::set<std::string> &calls)
{
    if (calls.empty())
        return;
    for (Json &message : thread.history.asArray()) {
        if (!message.isObject() || !message.find("parts") || !message["parts"].isArray())
            continue;
        for (Json &part : message["parts"].asArray())
            if (stringAt(part, "type") == "toolResult" && calls.contains(stringAt(part, "callId")))
                part["content"] = std::string("The search results aren't kept after the turn.");
    }
}

std::optional<qsizetype> offsetAt(const Json &citation, std::string_view key)
{
    const Json *value = citation.find(key);
    if (!value || !value->isInt() || value->asInt() < 0)
        return std::nullopt;
    return static_cast<qsizetype>(value->asInt());
}

Json *itemWith(AgentThread &thread, std::string_view key, const Json &value)
{
    auto &items = thread.items.asArray();
    for (auto it = items.rbegin(); it != items.rend(); ++it)
        if (const Json *v = it->find(key); v && *v == value)
            return &*it;
    return nullptr;
}

std::optional<app::Price> priceOf(const app::Config *config, const app::ModelRef &ref)
{
    const app::Provider *provider = config ? config->provider(ref.providerId) : nullptr;
    if (!provider)
        return std::nullopt;
    for (const app::Model &m : provider->models)
        if (m.id == ref.modelId)
            return m.price;
    return std::nullopt;
}

Json modelJson(const app::ModelRef &ref)
{
    return Json::object({{"providerId", ref.providerId}, {"model", ref.modelId}});
}

}

std::string_view agentModeId(AgentMode mode)
{
    switch (mode) {
    case AgentMode::Manual:
        return "manual";
    case AgentMode::Auto:
        return "auto";
    case AgentMode::FullAccess:
        return "full";
    }
    return "auto";
}

std::optional<AgentMode> agentModeFromId(std::string_view id)
{
    for (const AgentMode mode : {AgentMode::Manual, AgentMode::Auto, AgentMode::FullAccess})
        if (agentModeId(mode) == id)
            return mode;
    return std::nullopt;
}

RiskCheck alwaysAsk(const std::string &operation, const Json &params,
                    const std::function<std::optional<std::string>(std::string_view)> &pathOf,
                    const std::vector<std::string> &safeFolders)
{
    const auto outside = [&](const std::optional<std::string> &path) {
        return !path || std::none_of(safeFolders.begin(), safeFolders.end(),
                                     [&](const std::string &folder) { return under(*path, folder); });
    };
    const auto named = [](const auto &table, std::string_view name) -> RiskCheck {
        for (const RiskyName &r : table)
            if (r.name == name)
                return {r.risk, std::string(r.reason)};
        return {};
    };
    if (operation == "files.write" && outside(pathOf ? pathOf(stringAt(params, "file")) : std::nullopt))
        return {Risk::Files, "It overwrites a file outside the subtitles' folders and your user folder."};
    if (operation == "commands.run") {
        const std::string command = stringAt(params, "command");
        if (application::isPluginCommand(command))
            return {Risk::Programs, "It runs a plugin's command."};
        return named(kRiskyCommands, command);
    }
    if (operation != "settings.set")
        return {};
    const std::string setting = stringAt(params, "setting");
    if (setting == "fontCollector.directory") {
        const Json *value = params.find("value");
        if (outside(value && value->isString() ? std::optional(value->asString()) : std::nullopt))
            return {Risk::Files, "It makes the Font collector copy fonts outside the subtitles' folders and your user folder."};
        return {};
    }
    if (RiskCheck check = named(kRiskySettings, setting); check.risk != Risk::None)
        return check;
    for (const RiskyName &r : kRiskySettingPrefixes)
        if (setting.starts_with(r.name))
            return {r.risk, std::string(r.reason)};
    return {};
}

bool asksPermission(const std::string &operation)
{
    const api::Operation *op = api::Api::registry().find(operation);
    if (!op || op->access != api::Access::Write)
        return false;
    return std::find(kViewWrites.begin(), kViewWrites.end(), operation) == kViewWrites.end();
}

Decision decide(AgentMode mode, const std::string &operation, bool alwaysAsk, bool allowed)
{
    if (alwaysAsk)
        return Decision::AlwaysAsk;
    if (mode == AgentMode::FullAccess || allowed)
        return Decision::Run;
    if (mode == AgentMode::Manual)
        return operation == "document.apply" ? Decision::Run : Decision::Ask;
    return Decision::Judge;
}

std::string allowanceKey(const std::string &operation, const Json &params)
{
    if (operation == "commands.run")
        return operation + ":" + stringAt(params, "command");
    return operation;
}

Json judgeRequest(const std::string &operation, const Json &params, const std::vector<std::string> &userMessages)
{
    std::vector<std::string> kept;
    std::size_t room = kJudgeMessagesChars;
    for (auto it = userMessages.rbegin(); it != userMessages.rend() && room > 0; ++it) {
        std::string message = clipped(*it, room);
        room -= std::min(room, message.size());
        kept.push_back(std::move(message));
    }
    std::string text = "The user's messages, oldest first:\n";
    for (std::size_t i = 0; i < kept.size(); ++i)
        text += std::to_string(i + 1) + ". " + kept[kept.size() - 1 - i] + "\n";
    const api::Operation *op = api::Api::registry().find(operation);
    text += "\nThe action: " + toolName(operation) + (op ? " (" + op->summary + ")" : std::string()) + "\n" +
            clipped(params.dump(), kJudgeActionChars);
    return Json::object({{"system", std::string(kJudgeSystem)},
                         {"messages", Json::array({Json::object(
                                          {{"author", "user"},
                                           {"parts", Json::array({Json::object({{"type", "text"}, {"text", text}})})}})})},
                         {"maxOutputTokens", 200}});
}

Verdict judgeVerdict(std::string_view answer)
{
    const auto open = answer.find('{');
    const auto close = answer.rfind('}');
    if (open != std::string_view::npos && close != std::string_view::npos && close > open)
        if (const auto parsed = Json::parse(answer.substr(open, close - open + 1)); parsed && parsed->isObject()) {
            if (stringAt(*parsed, "decision") == "approve")
                return {true, {}};
            if (std::string reason = stringAt(*parsed, "reason"); !reason.empty())
                return {false, clipped(std::move(reason), 300)};
        }
    return {false, "The judge didn't approve it."};
}

Json AgentThread::toJson() const
{
    Json allowedJson = Json::array();
    for (const std::string &a : allowed)
        allowedJson.push(a);
    Json filesJson = Json::array();
    for (const std::string &f : files)
        filesJson.push(f);
    Json out = Json::object({{"version", 1},
                             {"id", id},
                             {"title", title},
                             {"renamed", renamed},
                             {"created", created},
                             {"updated", updated},
                             {"mode", std::string(agentModeId(mode))},
                             {"history", history},
                             {"items", items},
                             {"files", std::move(filesJson)},
                             {"cost", cost},
                             {"costUnknown", costUnknown},
                             {"contextTokens", contextTokens},
                             {"cap", cap},
                             {"allowed", std::move(allowedJson)},
                             {"webSearch", webSearch}});
    if (model)
        out["model"] = modelJson(*model);
    return out;
}

std::optional<AgentThread> AgentThread::fromJson(const Json &json)
{
    if (!json.isObject() || stringAt(json, "id").empty())
        return std::nullopt;
    const auto number = [&](std::string_view key) {
        const Json *v = json.find(key);
        return v && v->isNumber() ? v->asNumber() : 0.0;
    };
    const auto flag = [&](std::string_view key) {
        const Json *v = json.find(key);
        return v && v->isBool() && v->asBool();
    };
    AgentThread t;
    t.id = stringAt(json, "id");
    t.title = stringAt(json, "title");
    t.renamed = flag("renamed");
    t.created = static_cast<std::int64_t>(number("created"));
    t.updated = static_cast<std::int64_t>(number("updated"));
    t.mode = agentModeFromId(stringAt(json, "mode")).value_or(AgentMode::Auto);
    if (const Json *m = json.find("model"); m && m->isObject() && !stringAt(*m, "providerId").empty())
        t.model = app::ModelRef{stringAt(*m, "providerId"), stringAt(*m, "model")};
    if (const Json *h = json.find("history"); h && h->isArray())
        t.history = *h;
    if (const Json *i = json.find("items"); i && i->isArray())
        t.items = *i;
    if (const Json *f = json.find("files"); f && f->isArray())
        for (const Json &file : f->asArray())
            if (file.isString())
                t.files.push_back(file.asString());
    t.cost = number("cost");
    t.costUnknown = flag("costUnknown");
    t.contextTokens = static_cast<std::int64_t>(number("contextTokens"));
    t.cap = number("cap");
    if (const Json *a = json.find("allowed"); a && a->isArray())
        for (const Json &key : a->asArray())
            if (key.isString())
                t.allowed.insert(key.asString());
    if (const Json *w = json.find("webSearch"); w && w->isBool())
        t.webSearch = w->asBool();
    return t;
}

ThreadStore::ThreadStore(QString folder) : m_folder(std::move(folder))
{
    QFile file(m_folder + QStringLiteral("/spend.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto parsed = Json::parse(file.readAll().toStdString());
    if (parsed && parsed->isObject())
        for (const Json::Member &m : parsed->asObject())
            if (m.value.isNumber())
                m_spend[QString::fromStdString(m.key)] = m.value.asNumber();
}

std::vector<AgentThread> ThreadStore::load() const
{
    std::vector<AgentThread> threads;
    const QDir dir(m_folder);
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        if (name == QStringLiteral("spend.json"))
            continue;
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (const auto parsed = Json::parse(file.readAll().toStdString()))
            if (auto thread = AgentThread::fromJson(*parsed); thread && QString::fromStdString(thread->id) + ".json" == name)
                threads.push_back(std::move(*thread));
    }
    return threads;
}

bool ThreadStore::save(const AgentThread &thread) const
{
    if (m_folder.isEmpty() || !QDir().mkpath(m_folder))
        return false;
    QSaveFile file(m_folder + QLatin1Char('/') + QString::fromStdString(thread.id) + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const std::string text = thread.toJson().dump();
    file.write(text.data(), static_cast<qint64>(text.size()));
    return file.commit();
}

void ThreadStore::remove(const std::string &id) const
{
    if (!m_folder.isEmpty())
        QFile::remove(m_folder + QLatin1Char('/') + QString::fromStdString(id) + QStringLiteral(".json"));
}

double ThreadStore::spentOn(QDate day) const
{
    const auto it = m_spend.find(day.toString(Qt::ISODate));
    return it == m_spend.end() ? 0.0 : it->second;
}

void ThreadStore::spend(QDate day, double dollars)
{
    m_spend[day.toString(Qt::ISODate)] += dollars;
    const QString oldest = day.addDays(-31).toString(Qt::ISODate);
    std::erase_if(m_spend, [&](const auto &entry) { return entry.first < oldest; });
    if (m_folder.isEmpty() || !QDir().mkpath(m_folder))
        return;
    Json out = Json::object();
    for (const auto &[key, value] : m_spend)
        out[key.toStdString()] = value;
    QSaveFile file(m_folder + QStringLiteral("/spend.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    const std::string text = out.dump();
    file.write(text.data(), static_cast<qint64>(text.size()));
    file.commit();
}

struct AgentThreads::Run {
    std::uint64_t request = 0;
    std::string stoppedFor;
    std::map<std::string, std::string> toolByCallId;
    /// Where the current completion's text starts: the item and the UTF-16 offset in its text.
    std::optional<std::pair<std::size_t, qsizetype>> answerStart;
    std::set<std::string> webCalls;
    bool searchFailureShown = false;
};

AgentThreads::AgentThreads(Wiring wiring, ThreadStore *store, QObject *parent)
    : QObject(parent), m_wiring(std::move(wiring)), m_store(store)
{
    if (m_store)
        for (AgentThread &t : m_store->load()) {
            const std::string id = t.id;
            m_threads.emplace(id, std::move(t));
        }
}

AgentThreads::~AgentThreads()
{
    if (m_host) {
        m_host->setSessionGate({});
        m_host->onSessionUpdate({});
    }
}

std::vector<const AgentThread *> AgentThreads::threads() const
{
    std::vector<const AgentThread *> out;
    for (const auto &[id, t] : m_threads)
        out.push_back(&t);
    std::sort(out.begin(), out.end(), [](const AgentThread *a, const AgentThread *b) {
        return a->updated != b->updated ? a->updated > b->updated : a->id < b->id;
    });
    return out;
}

const AgentThread *AgentThreads::thread(const std::string &id) const
{
    const auto it = m_threads.find(id);
    return it == m_threads.end() ? nullptr : &it->second;
}

AgentThread *AgentThreads::find(const std::string &id)
{
    const auto it = m_threads.find(id);
    return it == m_threads.end() ? nullptr : &it->second;
}

std::string AgentThreads::create()
{
    const Defaults defaults = m_wiring.defaults ? m_wiring.defaults() : Defaults{};
    AgentThread t;
    do
        t.id = randomId();
    while (m_threads.contains(t.id));
    t.created = t.updated = nowMs();
    t.mode = defaults.mode;
    t.cap = defaults.threadCap;
    const std::string id = t.id;
    m_threads.emplace(id, std::move(t));
    if (m_store)
        m_store->save(m_threads.at(id));
    emit threadsChanged();
    return id;
}

void AgentThreads::rename(const std::string &id, std::string title)
{
    AgentThread *t = find(id);
    if (!t)
        return;
    t->title = clipped(std::move(title), 200);
    t->renamed = true;
    touch(*t);
    emit threadsChanged();
}

void AgentThreads::remove(const std::string &id)
{
    AgentThread *t = find(id);
    if (!t)
        return;
    stop(id);
    if (AiSidecarHost *h = m_host) {
        h->request("_hikari/session.close", Json::object({{"sessionId", id}}), [](SidecarResult) {});
        if (const auto model = modelOf(*t); model && m_wiring.requests && m_wiring.requests())
            m_wiring.requests()->endConversation(model->providerId, id);
    }
    m_live.erase(id);
    m_attached.erase(id);
    m_threads.erase(id);
    if (m_store)
        m_store->remove(id);
    emit threadsChanged();
}

void AgentThreads::setMode(const std::string &id, AgentMode mode)
{
    if (AgentThread *t = find(id); t && t->mode != mode) {
        t->mode = mode;
        touch(*t);
    }
}

void AgentThreads::setModel(const std::string &id, app::ModelRef model)
{
    if (AgentThread *t = find(id)) {
        t->model = std::move(model);
        touch(*t);
    }
}

void AgentThreads::setWebSearch(const std::string &id, bool on)
{
    if (AgentThread *t = find(id); t && t->webSearch != on) {
        t->webSearch = on;
        touch(*t);
    }
}

bool AgentThreads::searches(const AgentThread &thread) const
{
    AiRequests *requests = m_wiring.requests ? m_wiring.requests() : nullptr;
    return thread.webSearch && requests && requests->webSearch();
}

void AgentThreads::setAttached(const std::string &id, std::vector<application::DocumentId> documents)
{
    m_attached[id] = std::move(documents);
}

std::optional<app::ModelRef> AgentThreads::modelOf(const AgentThread &thread) const
{
    const app::Config *config = m_wiring.config ? m_wiring.config() : nullptr;
    if (thread.model && config && config->provider(thread.model->providerId))
        return thread.model;
    return config ? config->modelFor(app::Role::Chat) : std::nullopt;
}

bool AgentThreads::running(const std::string &id) const
{
    return m_runs.contains(id);
}

std::string AgentThreads::queued(const std::string &id) const
{
    const auto it = m_queued.find(id);
    return it == m_queued.end() ? std::string() : it->second;
}

void AgentThreads::setQueued(const std::string &id, std::string text)
{
    if (text.empty())
        m_queued.erase(id);
    else
        m_queued[id] = std::move(text);
    emit threadChanged(QString::fromStdString(id));
}

void AgentThreads::send(const std::string &id, std::string text, Json extraParts)
{
    AgentThread *t = find(id);
    if (!t || text.empty())
        return;
    if (running(id))
        return setQueued(id, std::move(text));
    start(*t, std::move(text), std::move(extraParts));
}

AiSidecarHost *AgentThreads::host()
{
    AiRequests *requests = m_wiring.requests ? m_wiring.requests() : nullptr;
    AiSidecarHost *h = requests ? requests->connected() : nullptr;
    if (h == m_host)
        return h;
    if (m_host) {
        m_host->setSessionGate({});
        m_host->onSessionUpdate({});
        disconnect(m_host, nullptr, this, nullptr);
    }
    m_host = h;
    m_live.clear();
    if (!h)
        return nullptr;
    h->setSessionGate([this, alive = std::weak_ptr(m_alive)](const std::string &session, const std::string &operation,
                                                              const Json &params, AiSidecarHost::Admit admit,
                                                              std::function<void(std::function<void()>)> onWithdraw) {
        if (!alive.lock())
            return admit(std::unexpected(api::Error{std::string(api::errors::kUnavailable), "The agent is gone.", {}}));
        gate(session, operation, params, std::move(admit), std::move(onWithdraw));
    });
    h->onSessionUpdate([this, alive = std::weak_ptr(m_alive)](const std::string &, const Json &params) {
        if (alive.lock())
            sessionUpdate(params);
    });
    connect(h, &AiSidecarHost::lost, this, &AgentThreads::sessionsLost);
    connect(h, &AiSidecarHost::finished, this, &AgentThreads::sessionsLost);
    connect(h, &AiSidecarHost::stopped, this, &AgentThreads::sessionsLost);
    return h;
}

void AgentThreads::sessionsLost()
{
    m_live.clear();
}

void AgentThreads::start(AgentThread &thread, std::string text, Json parts)
{
    const std::string id = thread.id;
    const Defaults defaults = m_wiring.defaults ? m_wiring.defaults() : Defaults{};
    const auto model = modelOf(thread);
    AiSidecarHost *h = model ? host() : nullptr;
    const auto notice = [&](std::string_view kind) {
        push(thread, Json::object({{"kind", "notice"}, {"notice", std::string(kind)}}));
        touch(thread);
    };
    if (!model)
        return notice("noModel");
    if (!h)
        return notice("noSidecar");
    if (defaults.dailyCap > 0 && spentToday() >= defaults.dailyCap)
        return notice("dailyCap");
    if (thread.cap > 0 && thread.cost >= thread.cap)
        return notice("cap");
    if (!thread.renamed && thread.title.empty())
        thread.title = firstLine(text);
    const std::string caller = callerOf(thread).id;
    const std::uint64_t turn = m_wiring.beginTurn ? m_wiring.beginTurn(caller, firstLine(text)) : 0;
    push(thread, Json::object({{"kind", "user"},
                               {"text", text},
                               {"turn", turn},
                               {"history", static_cast<std::int64_t>(thread.history.asArray().size())},
                               {"time", nowMs()}}));
    Json prompt = Json::array({Json::object({{"type", "text"}, {"text", std::move(text)}})});
    if (parts.isArray())
        for (Json &part : parts.asArray())
            prompt.push(std::move(part));
    AiRequests *requests = m_wiring.requests();
    const bool caching = requests->caching();
    const bool search = searches(thread);
    if (!m_live.contains(id)) {
        h->request("session/new",
                   Json::object({{"sessionId", id},
                                 {"model", modelJson(*model)},
                                 {"system", std::string(kSystemPrompt)},
                                 {"history", thread.history},
                                 {"cacheKey", id},
                                 {"caching", caching},
                                 {"webSearch", search}}),
                   [](SidecarResult) {});
        m_live.insert(id);
    }
    auto run = std::make_unique<Run>();
    run->request = h->request("session/prompt",
                              Json::object({{"sessionId", id},
                                            {"prompt", std::move(prompt)},
                                            {"model", modelJson(*model)},
                                            {"caching", caching},
                                            {"webSearch", search}}),
                              [this, alive = std::weak_ptr(m_alive), id](SidecarResult result) {
                                  if (alive.lock())
                                      finished(id, result);
                              });
    m_runs[id] = std::move(run);
    touch(thread);
}

void AgentThreads::finished(const std::string &id, const SidecarResult &result)
{
    const auto it = m_runs.find(id);
    if (it == m_runs.end())
        return;
    const std::string stop = it->second->stoppedFor;
    const std::set<std::string> webCalls = std::move(it->second->webCalls);
    m_runs.erase(it);
    closeAsks(id, "The turn ended.");
    AgentThread *t = find(id);
    if (!t)
        return;
    dropSearchResults(*t, webCalls);
    std::string notice;
    std::string detail;
    if (result) {
        const std::string reason = stringAt(*result, "stopReason");
        if (reason == "cancelled")
            notice = stop.empty() ? "stopped" : stop;
        else if (reason == "max_tokens")
            notice = "maxTokens";
        else if (reason == "refusal")
            notice = "refusal";
        else if (reason == "max_turn_requests")
            notice = "maxRequests";
    } else {
        const SidecarFailure &failure = result.error();
        if (failure.error == SidecarError::SidecarLost || failure.error == SidecarError::Stopped) {
            notice = "lost";
            detail = failure.message;
            m_live.clear();
        } else if (failure.error == SidecarError::Cancelled) {
            notice = stop.empty() ? "stopped" : stop;
        } else {
            const app::Error error = requestError(failure);
            notice = "failed";
            detail = error.detail.empty() ? std::string(app::errorKindId(error.kind)) : error.detail;
            if (failure.code != rpc::kProviderFailed)
                m_live.erase(id);
        }
    }
    if (!notice.empty()) {
        Json item = Json::object({{"kind", "notice"}, {"notice", notice}});
        if (!detail.empty())
            item["detail"] = clipped(detail, kOutputChars);
        push(*t, std::move(item));
    }
    touch(*t);
    const bool ended = notice.empty() || notice == "maxTokens" || notice == "refusal" || notice == "maxRequests";
    if (const auto q = m_queued.find(id); ended && q != m_queued.end()) {
        std::string next = std::move(q->second);
        m_queued.erase(q);
        start(*t, std::move(next), Json::array());
    }
}

void AgentThreads::sessionUpdate(const Json &params)
{
    AgentThread *t = find(stringAt(params, "sessionId"));
    const Json *update = params.find("update");
    if (!t || !update || !update->isObject())
        return;
    const std::string kind = stringAt(*update, "sessionUpdate");
    const auto run = m_runs.find(t->id);
    if (kind == "agent_message_chunk") {
        const Json *content = update->find("content");
        const std::string text = content ? stringAt(*content, "text") : std::string();
        if (text.empty())
            return;
        auto &items = t->items.asArray();
        const bool appends = !items.empty() && stringAt(items.back(), "kind") == "text";
        if (run != m_runs.end() && !run->second->answerStart)
            run->second->answerStart = {appends ? items.size() - 1 : items.size(),
                                        appends ? utf16Size(stringAt(items.back(), "text")) : 0};
        if (appends)
            items.back()["text"] = stringAt(items.back(), "text") + text;
        else
            push(*t, Json::object({{"kind", "text"}, {"text", text}}));
        emit threadChanged(QString::fromStdString(t->id));
        return;
    }
    if (kind == "tool_call") {
        const std::string callId = stringAt(*update, "toolCallId");
        const std::string tool = stringAt(*update, "title");
        if (run != m_runs.end()) {
            run->second->toolByCallId[callId] = tool;
            if (isWebTool(tool))
                run->second->webCalls.insert(callId);
        }
        if (stringAt(*update, "kind") != "edit") {
            const Json *input = update->find("rawInput");
            Json item = Json::object({{"kind", "tool"},
                                      {"id", callId},
                                      {"tool", tool},
                                      {"read", stringAt(*update, "kind") == "read"},
                                      {"input", input ? *input : Json()},
                                      {"status", "running"}});
            AiRequests *requests = m_wiring.requests ? m_wiring.requests() : nullptr;
            if (const auto service = requests ? requests->searchService() : std::nullopt; service && isWebTool(tool))
                item["service"] = std::string(app::searchServiceId(*service));
            push(*t, std::move(item));
        }
    } else if (kind == "tool_call_update") {
        const std::string callId = stringAt(*update, "toolCallId");
        std::string output;
        if (const Json *content = update->find("content"); content && content->isArray())
            for (const Json &c : content->asArray())
                if (const Json *inner = c.find("content"))
                    output += stringAt(*inner, "text");
        const std::string status = stringAt(*update, "status") == "failed" ? "failed" : "done";
        if (Json *item = itemWith(*t, "id", Json(callId)); item && stringAt(*item, "kind") == "tool") {
            (*item)["status"] = status;
            if (isWebTool(stringAt(*item, "tool")) && status == "done")
                (*item)["urls"] = urlsOf(stringAt(*item, "tool"), output);
            else
                (*item)["output"] = clipped(output, kOutputChars);
        } else if (status == "failed") {
            std::string tool;
            if (run != m_runs.end())
                if (const auto name = run->second->toolByCallId.find(callId); name != run->second->toolByCallId.end())
                    tool = name->second;
            push(*t, Json::object({{"kind", "tool"},
                                   {"id", callId},
                                   {"tool", std::move(tool)},
                                   {"read", false},
                                   {"status", "failed"},
                                   {"output", clipped(output, kOutputChars)}}));
        }
    } else if (kind == "plan") {
        const Json *entries = update->find("entries");
        auto &items = t->items.asArray();
        if (!items.empty() && stringAt(items.back(), "kind") == "plan")
            items.back()["entries"] = entries ? *entries : Json::array();
        else
            push(*t, Json::object({{"kind", "plan"}, {"entries", entries ? *entries : Json::array()}}));
    } else if (kind == "sources") {
        addSources(*t, run == m_runs.end() ? nullptr : run->second.get(), *update);
    } else if (kind == "search_failed") {
        searchFailed(*t, run == m_runs.end() ? nullptr : run->second.get(), *update);
    } else if (kind == "usage") {
        if (run != m_runs.end())
            run->second->answerStart.reset();
        if (const Json *usage = update->find("usage"))
            addUsage(*t, *usage);
    } else if (kind == "history") {
        if (const Json *message = update->find("message"); message && message->isObject())
            t->history.push(withoutImages(*message));
        return;
    } else {
        return;
    }
    touch(*t);
}

void AgentThreads::addSources(AgentThread &thread, Run *run, const Json &update)
{
    auto &items = thread.items.asArray();
    const auto start = run ? run->answerStart : std::nullopt;
    Json *answer = start && start->first < items.size() && stringAt(items[start->first], "kind") == "text"
                       ? &items[start->first]
                       : nullptr;
    if (answer) {
        const qsizetype length = offsetAt(update, "textLength").value_or(-1);
        const qsizetype base = start->second;
        const bool placeable = base + length == utf16Size(stringAt(*answer, "text"));
        Json cited = answer->find("citations") && (*answer)["citations"].isArray() ? (*answer)["citations"] : Json::array();
        if (const Json *citations = update.find("citations"); citations && citations->isArray())
            for (const Json &c : citations->asArray()) {
                const std::string url = stringAt(c, "url");
                if (url.empty() || std::ranges::any_of(cited.asArray(), [&](const Json &k) { return stringAt(k, "url") == url; }))
                    continue;
                Json kept = Json::object({{"index", static_cast<std::int64_t>(cited.asArray().size() + 1)},
                                          {"url", url},
                                          {"title", stringAt(c, "title").empty() ? url : stringAt(c, "title")}});
                if (std::string quote = stringAt(c, "quote"); !quote.empty())
                    kept["quote"] = clipped(std::move(quote), kOutputChars);
                const auto from = offsetAt(c, "start");
                const auto to = offsetAt(c, "end");
                if (placeable && from && to && *from < *to && *to <= length) {
                    kept["start"] = static_cast<std::int64_t>(base + *from);
                    kept["end"] = static_cast<std::int64_t>(base + *to);
                }
                cited.push(std::move(kept));
            }
        if (!cited.asArray().empty())
            (*answer)["citations"] = std::move(cited);
        if (std::string suggestions = stringAt(update, "searchSuggestions"); !suggestions.empty())
            (*answer)["searchSuggestions"] = std::move(suggestions);
    }
    const Json *searches = update.find("searches");
    if (!searches || !searches->isArray() || searches->asArray().empty())
        return;
    const app::Config *config = m_wiring.config ? m_wiring.config() : nullptr;
    const auto model = modelOf(thread);
    const app::Provider *provider = config && model ? config->provider(model->providerId) : nullptr;
    Json item = Json::object({{"kind", "search"},
                              {"by", provider && provider->preset == app::Preset::Gemini ? "google" : "provider"},
                              {"searches", *searches}});
    if (answer && start->second == 0) {
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(start->first), std::move(item));
        run->answerStart.reset();
    } else {
        push(thread, std::move(item));
    }
}

void AgentThreads::searchFailed(AgentThread &thread, Run *run, const Json &update)
{
    if (run && std::exchange(run->searchFailureShown, true))
        return;
    const Json *status = update.find("httpStatus");
    push(thread, Json::object({{"kind", "notice"},
                               {"notice", stringAt(update, "reason") == "key" ? "searchKey" : "searchUsedUp"},
                               {"service", stringAt(update, "service")},
                               {"status", status && status->isNumber() ? static_cast<std::int64_t>(status->asNumber()) : 0}}));
}

api::Caller AgentThreads::callerOf(const AgentThread &thread) const
{
    api::Caller caller;
    caller.id = "ai:" + thread.id;
    caller.label = "AI";
    caller.reviewed = true;
    if (const auto it = m_attached.find(thread.id); it != m_attached.end())
        caller.attached = it->second;
    return caller;
}

void AgentThreads::gate(const std::string &session, const std::string &operation, const Json &params,
                        AiSidecarHost::Admit admit, std::function<void(std::function<void()>)> onWithdraw)
{
    AgentThread *t = find(session);
    if (!t)
        return admit(std::unexpected(api::Error{std::string(api::errors::kNotGranted), "No agent thread has that id.", {}}));
    const api::Operation *op = api::Api::registry().find(operation);
    if (!op || op->access != api::Access::Write)
        return admitCall(*t, operation, params, false, std::move(admit));
    const auto run = m_runs.find(session);
    if (run == m_runs.end() || !run->second->stoppedFor.empty())
        return admit(std::unexpected(api::Error{std::string(api::errors::kRefused),
                                                "This turn has stopped; make no more changes.", {}}));
    if (!asksPermission(operation))
        return admitCall(*t, operation, params, false, std::move(admit));
    const std::string caller = callerOf(*t).id;
    const auto pathOf = [this, caller](std::string_view handle) {
        return m_wiring.api ? m_wiring.api->grantedPath(caller, handle) : std::nullopt;
    };
    const RiskCheck risk =
        alwaysAsk(operation, params, pathOf, m_wiring.safeFolders ? m_wiring.safeFolders() : std::vector<std::string>());
    switch (decide(t->mode, operation, risk.risk != Risk::None, t->allowed.contains(allowanceKey(operation, params)))) {
    case Decision::Run:
        return admitCall(*t, operation, params, false, std::move(admit));
    case Decision::Judge:
        return judge(*t, operation, params, std::move(admit), std::move(onWithdraw));
    case Decision::Ask:
        return openAsk(*t, Decision::Ask, operation, params, {}, std::move(admit), std::move(onWithdraw));
    case Decision::AlwaysAsk:
        return openAsk(*t, Decision::AlwaysAsk, operation, params, risk.reason, std::move(admit), std::move(onWithdraw));
    }
}

void AgentThreads::judge(AgentThread &thread, const std::string &operation, const Json &params,
                         AiSidecarHost::Admit admit, std::function<void(std::function<void()>)> onWithdraw)
{
    AiRequests *requests = m_wiring.requests ? m_wiring.requests() : nullptr;
    if (!requests)
        return openAsk(thread, Decision::Judge, operation, params, "The judge isn't available.", std::move(admit),
                       std::move(onWithdraw));
    std::vector<std::string> messages;
    for (const Json &item : thread.items.asArray())
        if (stringAt(item, "kind") == "user")
            messages.push_back(stringAt(item, "text"));
    struct State {
        bool done = false;
        app::StreamId stream = 0;
    };
    auto state = std::make_shared<State>();
    onWithdraw([this, alive = std::weak_ptr(m_alive), state] {
        if (!alive.lock() || std::exchange(state->done, true))
            return;
        if (AiRequests *r = m_wiring.requests ? m_wiring.requests() : nullptr)
            r->cancel(state->stream);
    });
    const std::string id = thread.id;
    const auto judgeModel = m_wiring.config && m_wiring.config() ? m_wiring.config()->modelFor(app::Role::Judge) : std::nullopt;
    state->stream = requests->complete(
        app::Role::Judge, judgeRequest(operation, params, messages),
        [this, alive = std::weak_ptr(m_alive), state, id, operation, params, judgeModel, admit = std::move(admit),
         onWithdraw = std::move(onWithdraw)](const app::Event &event) mutable {
            if (!alive.lock() || state->done)
                return;
            const auto *finished = std::get_if<app::Finished>(&event);
            const auto *failed = std::get_if<app::Failed>(&event);
            if (!finished && !failed)
                return;
            state->done = true;
            AgentThread *t = find(id);
            if (!t)
                return admit(std::unexpected(api::Error{std::string(api::errors::kRefused), "The thread is gone.", {}}));
            if (failed && failed->error.kind == app::ErrorKind::Cancelled)
                return;
            if (finished && judgeModel) {
                if (const auto cost = app::costOf(finished->usage, priceOf(m_wiring.config(), *judgeModel))) {
                    t->cost += *cost;
                    if (m_store)
                        m_store->spend(today(), *cost);
                }
            }
            if (const auto run = m_runs.find(id); run == m_runs.end() || !run->second->stoppedFor.empty())
                return admit(std::unexpected(api::Error{std::string(api::errors::kRefused),
                                                        "This turn has stopped; make no more changes.", {}}));
            if (finished) {
                const Verdict verdict = judgeVerdict(finished->text);
                if (verdict.approved)
                    return admitCall(*t, operation, params, true, std::move(admit));
                return openAsk(*t, Decision::Judge, operation, params, verdict.reason, std::move(admit),
                               std::move(onWithdraw));
            }
            const std::string why = failed->error.detail.empty() ? std::string(app::errorKindId(failed->error.kind))
                                                                 : failed->error.detail;
            openAsk(*t, Decision::Judge, operation, params, "The judge couldn't answer: " + clipped(why, 200),
                    std::move(admit), std::move(onWithdraw));
        });
}

void AgentThreads::openAsk(AgentThread &thread, Decision kind, const std::string &operation, const Json &params,
                           std::string reason, AiSidecarHost::Admit admit,
                           std::function<void(std::function<void()>)> onWithdraw)
{
    const std::uint64_t id = m_nextAsk++;
    m_asks[id] = Ask{id, thread.id, kind, operation, params, reason};
    m_waiting[id] = std::move(admit);
    const std::string_view type = kind == Decision::AlwaysAsk ? "always" : kind == Decision::Judge ? "judge" : "ask";
    push(thread, Json::object({{"kind", "ask"},
                               {"ask", id},
                               {"type", std::string(type)},
                               {"tool", toolName(operation)},
                               {"operation", operation},
                               {"params", Json(clipped(params.dump(), kOutputChars))},
                               {"reason", std::move(reason)},
                               {"status", "open"}}));
    onWithdraw([this, alive = std::weak_ptr(m_alive), id] {
        if (!alive.lock())
            return;
        const auto it = m_asks.find(id);
        if (it == m_asks.end())
            return;
        const std::string threadId = it->second.thread;
        m_asks.erase(it);
        m_waiting.erase(id);
        if (AgentThread *t = find(threadId)) {
            if (Json *item = itemWith(*t, "ask", Json(id)))
                (*item)["status"] = "withdrawn";
            touch(*t);
        }
    });
    touch(thread);
    emit asked(id);
}

void AgentThreads::answer(std::uint64_t askId, Answer answer, const std::string &message)
{
    const auto it = m_asks.find(askId);
    if (it == m_asks.end())
        return;
    Ask ask = std::move(it->second);
    m_asks.erase(it);
    AiSidecarHost::Admit admit = std::move(m_waiting[askId]);
    m_waiting.erase(askId);
    AgentThread *t = find(ask.thread);
    if (!t)
        return admit(std::unexpected(api::Error{std::string(api::errors::kRefused), "The thread is gone.", {}}));
    std::string status = "allowed";
    if (answer == Answer::Deny) {
        status = "denied";
        std::string text = "The user denied " + toolName(ask.operation) + (message.empty() ? "." : ": " + message);
        admit(std::unexpected(api::Error{std::string(api::errors::kRefused),
                                         text + " Don't try it again; ask the user what they want instead.",
                                         Json::object({{"reason", "denied"}})}));
    } else {
        if (answer == Answer::AllowInThread && ask.kind != Decision::AlwaysAsk) {
            t->allowed.insert(allowanceKey(ask.operation, ask.params));
            status = "allowedInThread";
        }
        admitCall(*t, ask.operation, ask.params, false, std::move(admit));
    }
    if (Json *item = itemWith(*t, "ask", Json(askId))) {
        (*item)["status"] = status;
        if (!message.empty())
            (*item)["message"] = message;
    }
    touch(*t);
}

void AgentThreads::admitCall(AgentThread &thread, const std::string &operation, const Json &params, bool judged,
                             AiSidecarHost::Admit admit)
{
    AiSidecarHost::Admission admission{callerOf(thread), {}};
    if (operation == "document.apply") {
        std::optional<application::DocumentId> document;
        if (const Json *d = params.find("document"); d && d->isInt())
            document = application::DocumentId{static_cast<std::uint64_t>(d->asInt())};
        else if (m_wiring.editingTarget)
            document = m_wiring.editingTarget();
        admission.ran = [this, alive = std::weak_ptr(m_alive), id = thread.id, document, name = stringAt(params, "name"),
                         judged](const api::Result &result) {
            if (!alive.lock() || !result)
                return;
            AgentThread *t = find(id);
            const Json *changed = result->find("changed");
            if (!t || (changed && changed->isBool() && !changed->asBool()))
                return;
            const std::string title = document && m_wiring.documentTitle ? m_wiring.documentTitle(*document) : std::string();
            Json item = Json::object({{"kind", "change"}, {"name", name}, {"title", title}, {"judged", judged}});
            if (document)
                item["document"] = static_cast<std::int64_t>(document->value);
            push(*t, std::move(item));
            if (!title.empty() && std::find(t->files.begin(), t->files.end(), title) == t->files.end())
                t->files.push_back(title);
            touch(*t);
        };
    }
    admit(std::move(admission));
}

void AgentThreads::addUsage(AgentThread &thread, const Json &usageJson)
{
    const app::Usage usage = usageOf(usageJson);
    thread.contextTokens = usage.inputTokens + usage.outputTokens;
    const auto model = modelOf(thread);
    const std::optional<double> cost =
        model ? app::costOf(usage, priceOf(m_wiring.config ? m_wiring.config() : nullptr, *model)) : std::nullopt;
    if (!cost) {
        thread.costUnknown = true;
        return;
    }
    thread.cost += *cost;
    if (m_store)
        m_store->spend(today(), *cost);
    const Defaults defaults = m_wiring.defaults ? m_wiring.defaults() : Defaults{};
    if (thread.cap > 0 && thread.cost >= thread.cap)
        stopFor(thread, "cap");
    else if (defaults.dailyCap > 0 && spentToday() >= defaults.dailyCap)
        stopFor(thread, "dailyCap");
}

double AgentThreads::spentToday() const
{
    return m_store ? m_store->spentOn(today()) : 0.0;
}

void AgentThreads::stopFor(AgentThread &thread, const std::string &why)
{
    const auto run = m_runs.find(thread.id);
    if (run == m_runs.end() || !run->second->stoppedFor.empty())
        return;
    run->second->stoppedFor = why;
    if (m_host && m_host->state() != AiSidecarHost::State::Ready)
        return m_host->cancel(run->second->request);
    if (m_host)
        m_host->notify("session/cancel", Json::object({{"sessionId", thread.id}}));
    closeAsks(thread.id, why == "stopped" ? "The user stopped the turn." : "The cost cap stopped the turn.");
}

void AgentThreads::stop(const std::string &id)
{
    if (AgentThread *t = find(id))
        stopFor(*t, "stopped");
}

void AgentThreads::steer(const std::string &id)
{
    const auto q = m_queued.find(id);
    if (!running(id) || q == m_queued.end() || !m_host)
        return;
    std::string text = std::move(q->second);
    m_queued.erase(q);
    emit threadChanged(QString::fromStdString(id));
    m_host->request("_hikari/session.steer",
                    Json::object({{"sessionId", id},
                                  {"prompt", Json::array({Json::object({{"type", "text"}, {"text", text}})})}}),
                    [this, alive = std::weak_ptr(m_alive), id, text](SidecarResult result) {
                        if (!alive.lock())
                            return;
                        AgentThread *t = find(id);
                        if (!t)
                            return;
                        const Json *taken = result ? result->find("taken") : nullptr;
                        if (taken && taken->isBool() && taken->asBool()) {
                            push(*t, Json::object({{"kind", "user"}, {"text", text}, {"steered", true}}));
                            return touch(*t);
                        }
                        send(id, text);
                    });
}

void AgentThreads::retry(const std::string &id)
{
    AgentThread *t = find(id);
    if (!t || running(id))
        return;
    const auto &items = t->items.asArray();
    const auto user = std::find_if(items.rbegin(), items.rend(), [](const Json &item) {
        return stringAt(item, "kind") == "user" && !item.find("steered");
    });
    if (user == items.rend())
        return;
    const std::string text = stringAt(*user, "text");
    const Json *index = user->find("history");
    if (index && index->isInt()) {
        auto &history = t->history.asArray();
        const auto keep = static_cast<std::size_t>(std::max<std::int64_t>(0, index->asInt()));
        if (keep < history.size())
            history.erase(history.begin() + static_cast<std::ptrdiff_t>(keep), history.end());
    }
    m_live.erase(id);
    start(*t, text, Json::array());
}

void AgentThreads::raiseCap(const std::string &id)
{
    if (AgentThread *t = find(id)) {
        const Defaults defaults = m_wiring.defaults ? m_wiring.defaults() : Defaults{};
        t->cap = std::max(t->cap, t->cost) + defaults.threadCap;
        touch(*t);
    }
}

void AgentThreads::closeAsks(const std::string &thread, const std::string &why)
{
    std::vector<std::uint64_t> open;
    for (const auto &[id, ask] : m_asks)
        if (ask.thread == thread)
            open.push_back(id);
    for (const std::uint64_t id : open) {
        Ask ask = std::move(m_asks.at(id));
        m_asks.erase(id);
        AiSidecarHost::Admit admit = std::move(m_waiting[id]);
        m_waiting.erase(id);
        if (admit)
            admit(std::unexpected(api::Error{std::string(api::errors::kRefused), why, {}}));
        if (AgentThread *t = find(thread))
            if (Json *item = itemWith(*t, "ask", Json(id)))
                (*item)["status"] = "withdrawn";
    }
}

std::int64_t AgentThreads::nowMs() const
{
    return m_wiring.now ? m_wiring.now() : static_cast<std::int64_t>(QDateTime::currentMSecsSinceEpoch());
}

QDate AgentThreads::today() const
{
    return m_wiring.today ? m_wiring.today() : QDate::currentDate();
}

void AgentThreads::push(AgentThread &thread, Json item)
{
    thread.items.push(std::move(item));
}

void AgentThreads::touch(AgentThread &thread)
{
    thread.updated = nowMs();
    if (m_store)
        m_store->save(thread);
    emit threadChanged(QString::fromStdString(thread.id));
}

}
