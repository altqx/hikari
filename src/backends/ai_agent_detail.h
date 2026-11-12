#pragma once

#include "hikari/backends/ai_agent.h"

#include <QTimer>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::backends::ai {

namespace detail {

std::string stringAt(const api::Json &object, std::string_view key);
std::string clipped(std::string text, std::size_t limit);
std::string firstLine(const std::string &text);
std::string randomId();
std::string toolName(std::string operation);
api::Json *itemWith(AgentThread &thread, std::string_view key, const api::Json &value);
api::Json modelJson(const application::ai::ModelRef &ref);
std::string_view systemPrompt();
std::size_t outputChars();
api::Json taskToJson(const AgentTask &task);
std::optional<AgentTask> taskFromJson(const api::Json &json);
std::string childSystem(const AgentTask &task, std::string_view documentTitle);

}

struct AgentThreads::Run {
    std::uint64_t request = 0;
    std::string stoppedFor;
    std::map<std::string, std::string> toolByCallId;
    /// Where the current completion's text starts: the item and the UTF-16 offset in its text.
    std::optional<std::pair<std::size_t, qsizetype>> answerStart;
    std::set<std::string> webCalls;
    bool searchFailureShown = false;
    bool compaction = false;
    std::string compactionFailure;
};

struct AgentThreads::TaskWait {
    std::string parent;
    std::string task;
    std::shared_ptr<SidecarRequest> request;
    std::unique_ptr<QTimer> timer;
};

struct AgentThreads::Estimate {
    std::string parent;
    std::uint64_t turn = 0;
    std::function<void(bool run)> answered;
};

}
