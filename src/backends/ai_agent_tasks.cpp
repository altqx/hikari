#include "hikari/backends/ai_agent.h"

#include "ai_agent_detail.h"

#include "hikari/backends/ai_requests.h"

#include <algorithm>
#include <cmath>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;
using api::Json;
using namespace detail;

namespace {

constexpr std::string_view kTaskRequest = "_hikari/task.request";
constexpr std::string_view kTaskCapabilities = "_hikari/task.capabilities";
constexpr std::string_view kTaskStatus = "_hikari/task.status";
constexpr std::string_view kTaskWait = "_hikari/task.wait";
constexpr std::string_view kTaskCancel = "_hikari/task.cancel";
constexpr int kDefaultWaitMs = 600'000;
constexpr int kMaxWaitMs = 1'800'000;
constexpr int kCancelWaitMs = 10'000;

std::string refusalText(TaskRefusal refusal)
{
    switch (refusal) {
    case TaskRefusal::Depth:
        return "A sub-agent can't hand work to another agent; do the work yourself.";
    case TaskRefusal::TooManyChildren:
        return "4 sub-agents already run for this thread. Wait for one to finish, or do this part yourself.";
    case TaskRefusal::ModeEscalation:
        return "A sub-agent's mode can't be broader than this thread's.";
    case TaskRefusal::CostCap:
        return "This thread is over its cost cap, so no sub-agent starts. Only the user can raise the cap.";
    case TaskRefusal::DailyCap:
        return "Today's AI cost cap is reached, so no sub-agent starts.";
    case TaskRefusal::ScopeOverlap:
        return "That scope overlaps a running sub-agent's. Give each running sub-agent Lines no other one has.";
    case TaskRefusal::InvalidScope:
        return "The scope needs a Document this thread may edit, as documents_list gives it, and Line ids in it.";
    case TaskRefusal::NoTarget:
        return "No such target. agent_capabilities lists the ones that work now.";
    case TaskRefusal::Declined:
        return "The user didn't start more sub-agents in this turn. Do the work yourself, or ask them.";
    case TaskRefusal::NotFound:
        return "This thread has no task with that id.";
    case TaskRefusal::Stopped:
        return "This turn has stopped; start no sub-agents.";
    }
    return {};
}

void refuse(SidecarRequest &request, TaskRefusal refusal)
{
    request.fail(rpc::kTaskRefused, refusalText(refusal),
                 Json::object({{"code", std::string(taskRefusalId(refusal))}}));
}

std::vector<std::int64_t> intsAt(const Json &object, std::string_view key)
{
    std::vector<std::int64_t> out;
    if (const Json *a = object.find(key); a && a->isArray())
        for (const Json &v : a->asArray())
            if (v.isInt())
                out.push_back(v.asInt());
    return out;
}

bool contains(const std::vector<std::int64_t> &lines, const Json *line)
{
    return line && line->isInt() && std::find(lines.begin(), lines.end(), line->asInt()) != lines.end();
}

std::string linesText(const std::vector<std::int64_t> &lines)
{
    std::string out;
    for (std::size_t i = 0; i < lines.size() && i < 50; ++i)
        out += (i ? ", " : "") + std::to_string(lines[i]);
    if (lines.size() > 50)
        out += ", and " + std::to_string(lines.size() - 50) + " more";
    return out;
}

Json scopeJson(const TaskScope &scope)
{
    Json out = Json::object({{"document", static_cast<std::int64_t>(scope.document.value)}});
    if (!scope.lines.empty()) {
        Json lines = Json::array();
        for (const std::int64_t id : scope.lines)
            lines.push(id);
        out["lines"] = std::move(lines);
    }
    return out;
}

std::uint64_t currentTurn(const AgentThread &thread)
{
    const auto &items = thread.items.asArray();
    for (auto it = items.rbegin(); it != items.rend(); ++it)
        if (stringAt(*it, "kind") == "user" && !it->find("steered"))
            if (const Json *turn = it->find("turn"); turn && turn->isInt())
                return static_cast<std::uint64_t>(turn->asInt());
    return 0;
}

}

int agentModeRank(AgentMode mode)
{
    switch (mode) {
    case AgentMode::Manual:
        return 0;
    case AgentMode::Auto:
        return 1;
    case AgentMode::FullAccess:
        return 2;
    }
    return 0;
}

bool scopesOverlap(const TaskScope &a, const TaskScope &b)
{
    if (a.document != b.document)
        return false;
    if (a.lines.empty() || b.lines.empty())
        return true;
    return std::any_of(a.lines.begin(), a.lines.end(), [&](std::int64_t id) {
        return std::find(b.lines.begin(), b.lines.end(), id) != b.lines.end();
    });
}

std::optional<std::string> outsideScope(const TaskScope &scope, const std::string &operation, const Json &params,
                                        std::optional<application::DocumentId> editingTarget)
{
    if (!asksPermission(operation))
        return std::nullopt;
    if (operation != "document.apply")
        return "A sub-agent changes subtitles only with document_apply, inside its scope.";
    std::optional<application::DocumentId> document = editingTarget;
    if (const Json *d = params.find("document"); d && d->isInt())
        document = application::DocumentId{static_cast<std::uint64_t>(d->asInt())};
    if (document != scope.document)
        return "A sub-agent may change only the Document its task names.";
    if (scope.lines.empty())
        return std::nullopt;
    const std::string outside = "That edit is outside this sub-agent's Lines (" + linesText(scope.lines) + ").";
    const Json *edits = params.find("edits");
    if (!edits || !edits->isArray())
        return std::nullopt;
    for (const Json &edit : edits->asArray()) {
        const std::string op = stringAt(edit, "op");
        if (op == "set_line" || op == "delete_line") {
            if (!contains(scope.lines, edit.find("line")))
                return outside;
        } else if (op == "move_line") {
            if (!contains(scope.lines, edit.find("line")) || !contains(scope.lines, edit.find("before")))
                return outside;
        } else if (op == "insert_line") {
            if (!contains(scope.lines, edit.find("after")) && !contains(scope.lines, edit.find("before")))
                return outside;
        } else {
            return "A sub-agent with a set of Lines can't change Styles or Script Info.";
        }
    }
    return std::nullopt;
}

std::string_view taskRefusalId(TaskRefusal refusal)
{
    switch (refusal) {
    case TaskRefusal::Depth:
        return "depth";
    case TaskRefusal::TooManyChildren:
        return "too_many_children";
    case TaskRefusal::ModeEscalation:
        return "mode_escalation_denied";
    case TaskRefusal::CostCap:
        return "cost_cap";
    case TaskRefusal::DailyCap:
        return "daily_cap";
    case TaskRefusal::ScopeOverlap:
        return "scope_overlap";
    case TaskRefusal::InvalidScope:
        return "invalid_scope";
    case TaskRefusal::NoTarget:
        return "no_target";
    case TaskRefusal::Declined:
        return "declined";
    case TaskRefusal::NotFound:
        return "not_found";
    case TaskRefusal::Stopped:
        return "stopped";
    }
    return "invalid";
}

std::expected<AgentMode, TaskRefusal> checkTask(const TaskAsk &ask)
{
    if (ask.fromChild)
        return std::unexpected(TaskRefusal::Depth);
    if (ask.running >= kMaxRunningChildren)
        return std::unexpected(TaskRefusal::TooManyChildren);
    const AgentMode mode = ask.mode.value_or(ask.parentMode);
    if (agentModeRank(mode) > agentModeRank(ask.parentMode))
        return std::unexpected(TaskRefusal::ModeEscalation);
    if (ask.overCap)
        return std::unexpected(TaskRefusal::CostCap);
    if (ask.overDailyCap)
        return std::unexpected(TaskRefusal::DailyCap);
    for (const TaskScope &running : ask.runningScopes)
        if (scopesOverlap(running, ask.scope))
            return std::unexpected(TaskRefusal::ScopeOverlap);
    return mode;
}

std::string_view taskStatusId(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Running:
        return "running";
    case TaskStatus::Done:
        return "done";
    case TaskStatus::Failed:
        return "failed";
    case TaskStatus::Stopped:
        return "stopped";
    case TaskStatus::Interrupted:
        return "interrupted";
    }
    return "failed";
}

std::optional<TaskStatus> taskStatusFromId(std::string_view id)
{
    for (const TaskStatus s : {TaskStatus::Running, TaskStatus::Done, TaskStatus::Failed, TaskStatus::Stopped,
                               TaskStatus::Interrupted})
        if (taskStatusId(s) == id)
            return s;
    return std::nullopt;
}

namespace detail {

Json taskToJson(const AgentTask &task)
{
    Json lines = Json::array();
    for (const std::int64_t id : task.lines)
        lines.push(id);
    return Json::object({{"parent", task.parent},
                         {"id", task.id},
                         {"brief", task.brief},
                         {"role", task.role},
                         {"clientRequestId", task.clientRequestId},
                         {"parentTurn", static_cast<std::int64_t>(task.parentTurn)},
                         {"documentTitle", task.documentTitle},
                         {"lines", std::move(lines)},
                         {"status", std::string(taskStatusId(task.status))},
                         {"stoppedFor", task.stoppedFor},
                         {"summary", task.summary},
                         {"target", task.target}});
}

std::optional<AgentTask> taskFromJson(const Json &json)
{
    if (!json.isObject() || stringAt(json, "parent").empty() || stringAt(json, "id").empty())
        return std::nullopt;
    AgentTask task;
    task.parent = stringAt(json, "parent");
    task.id = stringAt(json, "id");
    task.brief = stringAt(json, "brief");
    task.role = stringAt(json, "role");
    task.clientRequestId = stringAt(json, "clientRequestId");
    if (const Json *turn = json.find("parentTurn"); turn && turn->isInt())
        task.parentTurn = static_cast<std::uint64_t>(turn->asInt());
    task.documentTitle = stringAt(json, "documentTitle");
    task.lines = intsAt(json, "lines");
    task.status = taskStatusFromId(stringAt(json, "status")).value_or(TaskStatus::Interrupted);
    if (task.status == TaskStatus::Running)
        task.status = TaskStatus::Interrupted;
    task.stoppedFor = stringAt(json, "stoppedFor");
    task.summary = stringAt(json, "summary");
    task.target = stringAt(json, "target");
    return task;
}

std::string childSystem(const AgentTask &task, std::string_view documentTitle)
{
    std::string text =
        "You are a sub-agent. Another agent gave you the task in the next message; the user didn't write it.";
    if (!task.role.empty())
        text += " Act as the " + task.role + " sub-agent for this task.";
    text += " Change only " + std::string(documentTitle.empty() ? "the Document the task names" : documentTitle);
    if (task.scope)
        text += " (Document " + std::to_string(task.scope->document.value) + ")";
    if (!task.lines.empty())
        text += ", Lines " + linesText(task.lines);
    text += ": HikariSub refuses any other write. Do the work yourself; you can't hand it to another agent. End "
            "with a short report for that agent: what you changed, and anything left open.";
    return text;
}

}

std::vector<const AgentThread *> AgentThreads::children(const std::string &parent) const
{
    std::vector<const AgentThread *> out;
    for (const auto &[id, t] : m_threads)
        if (t.task && t.task->parent == parent)
            out.push_back(&t);
    std::sort(out.begin(), out.end(), [](const AgentThread *a, const AgentThread *b) {
        return a->created != b->created ? a->created < b->created : a->id < b->id;
    });
    return out;
}

AgentThread *AgentThreads::childOf(const AgentThread &parent, const std::string &task)
{
    for (auto &[id, t] : m_threads)
        if (t.task && t.task->parent == parent.id && t.task->id == task)
            return &t;
    return nullptr;
}

bool AgentThreads::needsYou(const AgentThread &child) const
{
    return std::any_of(m_asks.begin(), m_asks.end(), [&](const auto &ask) { return ask.second.thread == child.id; });
}

std::vector<std::pair<std::string, std::string>> AgentThreads::targets() const
{
    std::vector<std::pair<std::string, std::string>> out;
    if (const app::Config *config = m_wiring.config ? m_wiring.config() : nullptr)
        for (const app::Provider &p : config->providers)
            for (const app::Model &m : p.models)
                out.emplace_back(p.id + "\n" + m.id, "Hikari · " + (m.name.empty() ? m.id : m.name));
    if (m_wiring.agents)
        for (auto &[id, label] : m_wiring.agents())
            out.emplace_back(std::string(kAgentProviderPrefix) + id + "\n", label);
    return out;
}

Json AgentThreads::taskState(const AgentThread &child) const
{
    const AgentTask &task = *child.task;
    const bool running = task.status == TaskStatus::Running;
    Json out = Json::object({{"taskId", task.id},
                             {"childSessionId", child.id},
                             {"title", child.title},
                             {"status", running && needsYou(child) ? std::string("needs_you")
                                                                   : std::string(taskStatusId(task.status))}});
    if (!task.summary.empty())
        out["summary"] = task.summary;
    out["cost"] = child.costUnknown ? Json() : Json(child.cost);
    return out;
}

void AgentThreads::handleTasks(AiSidecarHost &host)
{
    const auto bind = [this](void (AgentThreads::*method)(const Json &, std::shared_ptr<SidecarRequest>)) {
        return [this, alive = std::weak_ptr(m_alive), method](const Json &params, std::shared_ptr<SidecarRequest> request) {
            if (!alive.lock())
                return request->fail(rpc::kMethodNotFound, "The agent is gone.");
            (this->*method)(params, std::move(request));
        };
    };
    host.handle(std::string(kTaskCapabilities), bind(&AgentThreads::taskCapabilities));
    host.handle(std::string(kTaskRequest), bind(&AgentThreads::taskRequest));
    host.handle(std::string(kTaskStatus), bind(&AgentThreads::taskStatus));
    host.handle(std::string(kTaskWait), bind(&AgentThreads::taskWait));
    host.handle(std::string(kTaskCancel), bind(&AgentThreads::taskCancel));
}

void AgentThreads::taskCapabilities(const Json &params, std::shared_ptr<SidecarRequest> request)
{
    const AgentThread *parent = find(stringAt(params, "sessionId"));
    if (!parent)
        return refuse(*request, TaskRefusal::NotFound);
    if (parent->task)
        return refuse(*request, TaskRefusal::Depth);
    Json list = Json::array();
    for (const auto &[key, label] : targets()) {
        const auto cut = key.find('\n');
        const std::string provider = key.substr(0, cut);
        const std::string model = key.substr(cut + 1);
        Json target = provider.starts_with(kAgentProviderPrefix)
                          ? Json::object({{"agent", provider.substr(kAgentProviderPrefix.size())}})
                          : Json::object({{"providerId", provider}, {"model", model}});
        list.push(Json::object({{"target", std::move(target)}, {"label", label}}));
    }
    std::int64_t running = 0;
    for (const AgentThread *child : children(parent->id))
        running += child->task->status == TaskStatus::Running ? 1 : 0;
    request->respond(Json::object({{"targets", std::move(list)},
                                   {"running", running},
                                   {"limit", static_cast<std::int64_t>(kMaxRunningChildren)},
                                   {"mode", std::string(agentModeId(parent->mode))}}));
}

void AgentThreads::taskRequest(const Json &params, std::shared_ptr<SidecarRequest> request)
{
    AgentThread *parent = find(stringAt(params, "sessionId"));
    if (!parent)
        return refuse(*request, TaskRefusal::NotFound);
    const std::string brief = stringAt(params, "task");
    if (brief.empty())
        return request->fail(rpc::kInvalidParams, "agent_delegate needs the task, written for an agent that sees nothing else.");
    const std::string clientRequestId = stringAt(params, "clientRequestId");
    if (!clientRequestId.empty())
        for (const AgentThread *child : children(parent->id))
            if (child->task->clientRequestId == clientRequestId)
                return request->respond(taskState(*child));
    if (!parent->task) {
        const auto run = m_runs.find(parent->id);
        if (run == m_runs.end() || !run->second->stoppedFor.empty()) {
            const std::string why = run == m_runs.end() ? std::string() : run->second->stoppedFor;
            return refuse(*request, why == "cap"        ? TaskRefusal::CostCap
                                    : why == "dailyCap" ? TaskRefusal::DailyCap
                                                        : TaskRefusal::Stopped);
        }
    }

    std::optional<AgentMode> mode;
    if (const std::string asked = stringAt(params, "mode"); !asked.empty()) {
        mode = agentModeFromId(asked);
        if (!mode)
            return request->fail(rpc::kInvalidParams, "mode is manual, auto or full.");
    }

    std::optional<app::ModelRef> model;
    std::string label;
    const Json *target = params.find("target");
    if (target && target->isObject() && (target->find("agent") || target->find("providerId"))) {
        const std::string agent = stringAt(*target, "agent");
        const std::string key = agent.empty()
                                    ? stringAt(*target, "providerId") + "\n" + stringAt(*target, "model")
                                    : std::string(kAgentProviderPrefix) + agent + "\n";
        for (const auto &[k, l] : targets())
            if (k == key) {
                const auto cut = k.find('\n');
                model = app::ModelRef{k.substr(0, cut), k.substr(cut + 1)};
                label = l;
            }
        if (!model)
            return refuse(*request, TaskRefusal::NoTarget);
    } else {
        model = modelOf(*parent);
        if (!model)
            return refuse(*request, TaskRefusal::NoTarget);
        for (const auto &[k, l] : targets())
            if (k == model->providerId + "\n" + model->modelId)
                label = l;
        if (label.empty())
            label = model->modelId.empty() ? model->providerId : model->modelId;
    }

    const Json *scopeJsonIn = params.find("scope");
    const Json *document = scopeJsonIn && scopeJsonIn->isObject() ? scopeJsonIn->find("document") : nullptr;
    if (!document || !document->isInt() || document->asInt() < 1)
        return refuse(*request, TaskRefusal::InvalidScope);
    TaskScope scope{application::DocumentId{static_cast<std::uint64_t>(document->asInt())}, intsAt(*scopeJsonIn, "lines")};
    const api::Caller caller = callerOf(*parent);
    const auto editing = m_wiring.editingTarget ? m_wiring.editingTarget() : std::nullopt;
    if (scope.document != editing &&
        std::find(caller.attached.begin(), caller.attached.end(), scope.document) == caller.attached.end())
        return refuse(*request, TaskRefusal::InvalidScope);
    if (std::any_of(scope.lines.begin(), scope.lines.end(), [](std::int64_t id) { return id < 1; }))
        return refuse(*request, TaskRefusal::InvalidScope);

    const Defaults defaults = m_wiring.defaults ? m_wiring.defaults() : Defaults{};
    TaskAsk ask{.fromChild = parent->task.has_value(),
                .parentMode = parent->mode,
                .mode = mode,
                .overCap = parent->cap > 0 && parent->cost >= parent->cap,
                .overDailyCap = defaults.dailyCap > 0 && spentToday() >= defaults.dailyCap,
                .scope = scope};
    const std::uint64_t turn = currentTurn(*parent);
    std::vector<const AgentThread *> inTurn;
    for (const AgentThread *child : children(parent->id)) {
        if (child->task->status == TaskStatus::Running) {
            ++ask.running;
            if (child->task->scope)
                ask.runningScopes.push_back(*child->task->scope);
        }
        if (child->task->parentTurn == turn)
            inTurn.push_back(child);
    }
    const auto checked = checkTask(ask);
    if (!checked)
        return refuse(*request, checked.error());
    const std::string turnKey = parent->id + ":" + std::to_string(turn);
    if (m_declinedTurns.contains(turnKey))
        return refuse(*request, TaskRefusal::Declined);

    auto go = [this, id = parent->id, params, mode = *checked, model = *model, label, scope,
                     request](bool run) mutable {
        AgentThread *p = find(id);
        if (!p)
            return refuse(*request, TaskRefusal::NotFound);
        if (!run)
            return refuse(*request, TaskRefusal::Declined);
        startChild(*p, params, mode, std::move(model), std::move(label), std::move(scope), request);
    };
    const auto estimated = m_estimatedTurn.find(parent->id);
    if (inTurn.size() + 1 < kEstimateFrom || (estimated != m_estimatedTurn.end() && estimated->second == turn))
        return go(true);

    std::size_t tokens = 0;
    std::optional<double> cost = 0.0;
    const auto add = [&](std::size_t chars, const app::ModelRef &ref) {
        const auto t = static_cast<std::int64_t>(chars / 4 + 1);
        tokens += static_cast<std::size_t>(t * 9 / 2);
        const auto price = priceOf(m_wiring.config ? m_wiring.config() : nullptr, ref);
        const auto c = app::costOf(app::Usage{.inputTokens = t * 3, .outputTokens = t * 3 / 2}, price);
        if (cost && c)
            *cost += *c;
        else
            cost.reset();
    };
    for (const AgentThread *child : inTurn)
        if (const auto m = modelOf(*child); m && child->task->scope)
            add(child->task->brief.size() + (m_wiring.scopeChars ? m_wiring.scopeChars(*child->task->scope) : 0), *m);
    add(brief.size() + (m_wiring.scopeChars ? m_wiring.scopeChars(scope) : 0), *model);

    const std::uint64_t askId = m_nextAsk++;
    auto estimate = std::make_unique<Estimate>();
    estimate->parent = parent->id;
    estimate->turn = turn;
    estimate->answered = [this, go = std::move(go), turnKey, id = parent->id, turn](bool run) mutable {
        if (run)
            m_estimatedTurn[id] = turn;
        else
            m_declinedTurns.insert(turnKey);
        go(run);
    };
    m_estimates[askId] = std::move(estimate);
    Json item = Json::object({{"kind", "estimate"},
                              {"ask", askId},
                              {"children", static_cast<std::int64_t>(inTurn.size() + 1)},
                              {"tokens", static_cast<std::int64_t>(tokens)},
                              {"status", "open"}});
    if (cost)
        item["cost"] = *cost;
    push(*parent, std::move(item));
    request->onCancel([this, alive = std::weak_ptr(m_alive), askId] {
        if (!alive.lock())
            return;
        if (const auto it = m_estimates.find(askId); it != m_estimates.end()) {
            const std::string parentId = it->second->parent;
            m_estimates.erase(it);
            if (AgentThread *p = find(parentId)) {
                if (Json *open = itemWith(*p, "ask", Json(askId)))
                    (*open)["status"] = "withdrawn";
                touch(*p);
            }
        }
    });
    touch(*parent);
    emit asked(askId);
}

void AgentThreads::startChild(AgentThread &parent, const Json &params, AgentMode mode, app::ModelRef model,
                              std::string target, TaskScope scope, std::shared_ptr<SidecarRequest> request)
{
    const std::size_t number = children(parent.id).size() + 1;
    AgentThread child;
    do
        child.id = randomId();
    while (m_threads.contains(child.id));
    child.created = child.updated = nowMs();
    child.mode = mode;
    child.model = std::move(model);
    child.project = parent.project;
    child.webSearch = parent.webSearch;
    child.renamed = true;
    const std::string brief = stringAt(params, "task");
    const std::string title = stringAt(params, "title");
    child.title = clipped(title.empty() ? firstLine(brief) : title, 200);
    AgentTask task;
    task.parent = parent.id;
    task.id = "t" + std::to_string(number);
    task.brief = brief;
    task.role = stringAt(params, "role");
    task.clientRequestId = stringAt(params, "clientRequestId");
    task.parentTurn = currentTurn(parent);
    task.documentTitle = m_wiring.documentTitle ? m_wiring.documentTitle(scope.document) : std::string();
    task.lines = scope.lines;
    task.scope = std::move(scope);
    task.target = std::move(target);
    child.task = std::move(task);
    const std::string childId = child.id;
    const std::string taskId = child.task->id;
    m_attached[childId] = {child.task->scope->document};
    m_threads.emplace(childId, std::move(child));
    push(parent, Json::object({{"kind", "task"}, {"task", taskId}, {"child", childId}}));
    touch(parent);
    AgentThread &c = m_threads.at(childId);
    start(c, brief, Json::array());
    if (!running(childId) && c.task->status == TaskStatus::Running) {
        const auto &items = c.items.asArray();
        childEnded(c, items.empty() ? std::string("failed") : stringAt(items.back(), "notice"), {});
    }
    request->respond(taskState(c));
}

void AgentThreads::taskStatus(const Json &params, std::shared_ptr<SidecarRequest> request)
{
    AgentThread *parent = find(stringAt(params, "sessionId"));
    AgentThread *child = parent ? childOf(*parent, stringAt(params, "taskId")) : nullptr;
    if (!child)
        return refuse(*request, TaskRefusal::NotFound);
    request->respond(taskState(*child));
}

void AgentThreads::taskWait(const Json &params, std::shared_ptr<SidecarRequest> request)
{
    AgentThread *parent = find(stringAt(params, "sessionId"));
    AgentThread *child = parent ? childOf(*parent, stringAt(params, "taskId")) : nullptr;
    if (!child)
        return refuse(*request, TaskRefusal::NotFound);
    if (child->task->status != TaskStatus::Running)
        return request->respond(taskState(*child));
    int ms = kDefaultWaitMs;
    if (const Json *t = params.find("timeoutMs"); t && t->isNumber())
        ms = static_cast<int>(std::clamp<double>(t->asNumber(), 1, kMaxWaitMs));
    auto wait = std::make_unique<TaskWait>();
    wait->parent = parent->id;
    wait->task = child->task->id;
    wait->request = request;
    wait->timer = std::make_unique<QTimer>();
    wait->timer->setSingleShot(true);
    TaskWait *raw = wait.get();
    const auto drop = [this, raw] {
        const auto it = std::find_if(m_taskWaits.begin(), m_taskWaits.end(), [raw](const auto &w) { return w.get() == raw; });
        if (it == m_taskWaits.end())
            return std::unique_ptr<TaskWait>();
        std::unique_ptr<TaskWait> out = std::move(*it);
        m_taskWaits.erase(it);
        return out;
    };
    QObject::connect(wait->timer.get(), &QTimer::timeout, this, [this, drop] {
        const std::unique_ptr<TaskWait> done = drop();
        if (!done)
            return;
        AgentThread *p = find(done->parent);
        AgentThread *c = p ? childOf(*p, done->task) : nullptr;
        if (!c)
            return refuse(*done->request, TaskRefusal::NotFound);
        Json state = taskState(*c);
        state["waitTimedOut"] = true;
        done->request->respond(std::move(state));
    });
    request->onCancel([this, alive = std::weak_ptr(m_alive), drop] {
        if (!alive.lock())
            return;
        if (const std::unique_ptr<TaskWait> done = drop())
            done->request->fail(rpc::kRequestCancelled, "The wait was cancelled; the task goes on.");
    });
    wait->timer->start(ms);
    m_taskWaits.push_back(std::move(wait));
}

void AgentThreads::answerWaits(const AgentThread &parent)
{
    std::vector<std::unique_ptr<TaskWait>> open;
    for (auto it = m_taskWaits.begin(); it != m_taskWaits.end();) {
        if ((*it)->parent == parent.id) {
            open.push_back(std::move(*it));
            it = m_taskWaits.erase(it);
        } else {
            ++it;
        }
    }
    for (const std::unique_ptr<TaskWait> &wait : open) {
        const AgentThread *child = childOf(parent, wait->task);
        if (!child)
            continue;
        Json state = taskState(*child);
        if (child->task->status == TaskStatus::Running)
            state["status"] = std::string(taskStatusId(TaskStatus::Stopped));
        wait->request->respond(std::move(state));
    }
}

void AgentThreads::taskCancel(const Json &params, std::shared_ptr<SidecarRequest> request)
{
    AgentThread *parent = find(stringAt(params, "sessionId"));
    AgentThread *child = parent ? childOf(*parent, stringAt(params, "taskId")) : nullptr;
    if (!child)
        return refuse(*request, TaskRefusal::NotFound);
    if (child->task->status != TaskStatus::Running)
        return request->respond(taskState(*child));
    Json wait = params;
    wait["timeoutMs"] = kCancelWaitMs;
    taskWait(wait, request);
    stopFor(*child, "stopped");
}

void AgentThreads::childEnded(AgentThread &child, const std::string &notice, const std::string &detail)
{
    AgentTask &task = *child.task;
    if (task.status != TaskStatus::Running)
        return;
    if (notice.empty() || notice == "maxTokens" || notice == "refusal" || notice == "maxRequests")
        task.status = TaskStatus::Done;
    else if (notice == "lost")
        task.status = TaskStatus::Interrupted;
    else if (notice == "stopped" || notice == "parent" || notice == "cap" || notice == "dailyCap") {
        task.status = TaskStatus::Stopped;
        task.stoppedFor = notice;
    } else
        task.status = TaskStatus::Failed;

    std::string answer;
    const auto &items = child.items.asArray();
    for (auto it = items.rbegin(); it != items.rend() && stringAt(*it, "kind") != "user"; ++it)
        if (stringAt(*it, "kind") == "text")
            answer = stringAt(*it, "text") + (answer.empty() ? "" : "\n\n" + answer);
    std::string lead;
    switch (task.status) {
    case TaskStatus::Failed:
        lead = "It failed" + (detail.empty() ? std::string(".") : ": " + detail) + "\n\n";
        break;
    case TaskStatus::Interrupted:
        lead = "It was interrupted when the AI helper stopped; it won't run again by itself. Its edits so far stay in "
               "the review.\n\n";
        break;
    case TaskStatus::Stopped:
        lead = task.stoppedFor == "cap" || task.stoppedFor == "dailyCap"
                   ? "It stopped at the cost cap. Its edits so far stay in the review.\n\n"
                   : "It was stopped. Its edits so far stay in the review.\n\n";
        break;
    default:
        break;
    }
    task.summary = clipped(lead + answer, kTaskResultChars);
    touch(child);

    const std::string parentId = task.parent;
    AgentThread *parent = find(parentId);
    if (!parent)
        return;
    bool waited = false;
    for (auto it = m_taskWaits.begin(); it != m_taskWaits.end();) {
        if ((*it)->parent == parentId && (*it)->task == task.id) {
            std::unique_ptr<TaskWait> done = std::move(*it);
            it = m_taskWaits.erase(it);
            done->request->respond(taskState(child));
            waited = true;
        } else {
            ++it;
        }
    }
    touch(*parent);
    if (waited)
        return;
    m_results[parentId].push_back("Task " + task.id + " (" + child.title + ") " +
                                  std::string(taskStatusId(task.status)) + ":\n" + task.summary);
    if (task.status == TaskStatus::Done || task.status == TaskStatus::Failed)
        deliverResults(*parent);
}

void AgentThreads::deliverResults(AgentThread &parent)
{
    const auto it = m_results.find(parent.id);
    if (running(parent.id) || it == m_results.end() || it->second.empty())
        return;
    for (const AgentThread *child : children(parent.id))
        if (child->task->status == TaskStatus::Running && needsYou(*child))
            return;
    start(parent, std::string(), Json::array());
}

void AgentThreads::chargeParent(AgentThread &child, double cost)
{
    AgentThread *parent = child.task ? find(child.task->parent) : nullptr;
    if (!parent)
        return;
    parent->cost += cost;
    if (parent->cap > 0 && parent->cost >= parent->cap) {
        stopFor(*parent, "cap");
        if (!running(parent->id)) {
            push(*parent, Json::object({{"kind", "notice"}, {"notice", "cap"}}));
            touch(*parent);
        }
    }
}

}
