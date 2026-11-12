#include "hikari/api/json.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
extern char **environ;
#endif

using hikari::api::Json;

namespace {

std::string g_mode = "ok";
std::string g_buildId;
int g_requiredApi = 1;
Json g_initialize;
int g_apiVersion = 0;
std::string g_self;

void send(const Json &message)
{
    const std::string line = message.dump() + "\n";
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

void respond(const Json &id, Json result)
{
    send(Json::object({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}));
}

Json environment()
{
    Json env = Json::object();
#ifdef _WIN32
    LPWCH block = GetEnvironmentStringsW();
    for (LPWCH p = block; *p; p += wcslen(p) + 1) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
        std::string entry(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, p, -1, entry.data(), size, nullptr, nullptr);
        const auto eq = entry.find('=', 1);
        if (eq != std::string::npos)
            env[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    FreeEnvironmentStringsW(block);
#else
    for (char **p = environ; *p; ++p) {
        const std::string entry = *p;
        const auto eq = entry.find('=');
        if (eq != std::string::npos)
            env[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
#endif
    return env;
}

std::int64_t spawnSleeper()
{
#ifdef _WIN32
    std::wstring command = L"\"" + std::wstring(g_self.begin(), g_self.end()) + L"\" --mode sleep";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &info))
        return 0;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return static_cast<std::int64_t>(info.dwProcessId);
#else
    const pid_t pid = fork();
    if (pid == 0) {
        execl(g_self.c_str(), g_self.c_str(), "--mode", "sleep", static_cast<char *>(nullptr));
        _exit(127);
    }
    return pid;
#endif
}

struct FakeTurn {
    Json promptId;
    std::string say;
    std::vector<Json> calls;
    std::size_t next = 0;
    std::string reason = "end_turn";
    bool hang = false;
    bool cancelled = false;
    bool calling = false;
};

void sessionUpdate(const std::string &session, Json update)
{
    send(Json::object({{"jsonrpc", "2.0"}, {"method", "session/update"},
                       {"params", Json::object({{"sessionId", session}, {"update", std::move(update)}})}}));
}

void keep(const std::string &session, Json message)
{
    sessionUpdate(session, Json::object({{"sessionUpdate", "history"}, {"message", std::move(message)}}));
}

std::string toolName(std::string operation)
{
    for (char &c : operation)
        if (c == '.')
            c = '_';
    return operation;
}

void sleepForever()
{
    for (;;)
        std::this_thread::sleep_for(std::chrono::hours(1));
}

}

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    g_self = argv[0];
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 1 && !args[0].starts_with("--")) {
        std::ifstream bundle(args[0]);
        args.assign(std::istream_iterator<std::string>(bundle), std::istream_iterator<std::string>());
    }
    for (std::size_t i = 0; i + 1 < args.size(); i += 2) {
        const std::string &flag = args[i];
        if (flag == "--mode")
            g_mode = args[i + 1];
        else if (flag == "--build-id")
            g_buildId = args[i + 1];
        else if (flag == "--api-version")
            g_apiVersion = std::atoi(args[i + 1].c_str());
        else if (flag == "--required-api")
            g_requiredApi = std::atoi(args[i + 1].c_str());
    }
    if (g_mode == "sleep")
        sleepForever();
    if (g_mode == "exit-at-once")
        return 7;

    std::map<std::int64_t, Json> waiting; // our request to Qt -> Qt's request it answers
    std::vector<Json> hanging;
    Json received = Json::array();
    std::int64_t nextId = 1;
    std::map<std::string, FakeTurn> turns;
    std::map<std::string, std::vector<Json>> compactions; // scripted answers to _hikari/session.compact, by session
    std::map<std::int64_t, std::string> sessionCalls;
    const auto finish = [&](const std::string &session, const std::string &reason) {
        FakeTurn &turn = turns.at(session);
        if (!turn.say.empty())
            keep(session, Json::object({{"author", "assistant"},
                                        {"parts", Json::array({Json::object({{"type", "text"}, {"text", turn.say}})})}}));
        respond(turn.promptId, Json::object({{"stopReason", reason}}));
        turns.erase(session);
    };
    const auto advance = [&](const std::string &session) {
        FakeTurn &turn = turns.at(session);
        if (turn.cancelled)
            return finish(session, "cancelled");
        if (turn.next < turn.calls.size()) {
            const Json call = turn.calls[turn.next];
            const std::string callId = "c" + std::to_string(++turn.next);
            const Json *operation = call.find("operation");
            const Json *params = call.find("params");
            const std::string name = operation && operation->isString() ? operation->asString() : std::string();
            sessionUpdate(session, Json::object({{"sessionUpdate", "tool_call"}, {"toolCallId", callId},
                                                 {"title", toolName(name)},
                                                 {"kind", name == "document.apply" ? "edit" : "other"},
                                                 {"status", "in_progress"},
                                                 {"rawInput", params ? *params : Json::object()}}));
            const std::int64_t mine = nextId++;
            sessionCalls.emplace(mine, session);
            turn.calling = true;
            if (const Json *method = call.find("method"); method && method->isString()) {
                Json p = params ? *params : Json::object();
                p["sessionId"] = session;
                send(Json::object({{"jsonrpc", "2.0"}, {"id", mine}, {"method", *method}, {"params", std::move(p)}}));
                return;
            }
            send(Json::object({{"jsonrpc", "2.0"}, {"id", mine}, {"method", "_hikari/api.call"},
                               {"params", Json::object({{"operation", name},
                                                        {"params", params ? *params : Json::object()},
                                                        {"session", session}})}}));
            return;
        }
        if (!turn.hang)
            finish(session, turn.reason);
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        auto parsed = Json::parse(line);
        if (!parsed)
            return 9;
        const Json &message = *parsed;
        const Json *method = message.find("method");
        const Json *id = message.find("id");
        const Json *params = message.find("params");
        if (!method && id && id->isInt() && sessionCalls.contains(id->asInt())) {
            const std::string session = sessionCalls.at(id->asInt());
            sessionCalls.erase(id->asInt());
            received.push(Json::object({{"method", "answer"}, {"params", message}}));
            if (!turns.contains(session))
                continue;
            FakeTurn &turn = turns.at(session);
            turn.calling = false;
            const Json *error = message.find("error");
            const Json *result = message.find("result");
            sessionUpdate(session, Json::object({{"sessionUpdate", "tool_call_update"},
                                                 {"toolCallId", "c" + std::to_string(turn.next)},
                                                 {"status", error ? "failed" : "completed"},
                                                 {"content", Json::array({Json::object({{"type", "content"},
                                                     {"content", Json::object({{"type", "text"},
                                                         {"text", (error ? *error : result ? *result : Json()).dump()}})}})})}}));
            advance(session);
            continue;
        }
        if (!method) {
            const auto it = id && id->isInt() ? waiting.find(id->asInt()) : waiting.end();
            if (it != waiting.end()) {
                Json answer = Json::object();
                if (const Json *result = message.find("result"))
                    answer["result"] = *result;
                if (const Json *error = message.find("error"))
                    answer["error"] = *error;
                respond(it->second, std::move(answer));
                waiting.erase(it);
            }
            continue;
        }
        const std::string name = method->asString();
        if (name == "$/cancel_request") {
            const Json *target = params ? params->find("requestId") : nullptr;
            for (auto it = hanging.begin(); target && it != hanging.end(); ++it)
                if (*it == *target) {
                    send(Json::object({{"jsonrpc", "2.0"}, {"id", *it},
                                       {"error", Json::object({{"code", -32800}, {"message", "cancelled"}})}}));
                    hanging.erase(it);
                    break;
                }
            continue;
        }
        if (name == "session/cancel") {
            const Json *session = params ? params->find("sessionId") : nullptr;
            received.push(Json::object({{"method", name}, {"params", params ? *params : Json()}}));
            if (session && session->isString() && turns.contains(session->asString())) {
                FakeTurn &turn = turns.at(session->asString());
                turn.cancelled = true;
                if (!turn.calling)
                    finish(session->asString(), "cancelled");
            }
            if (id)
                respond(*id, nullptr);
            continue;
        }
        if (!id)
            continue;
        if (name == "initialize") {
            if (g_mode == "silent")
                continue;
            if (g_mode == "slow-initialize")
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            g_initialize = params ? *params : Json();
            respond(*id, Json::object({{"protocolVersion", g_mode == "old-protocol" ? 0 : 1},
                                       {"hikariApiVersion", g_apiVersion}, {"requiredApiVersion", g_requiredApi},
                                       {"version", "9.9.9"}, {"buildId", g_buildId},
                                       {"versions", Json::object({{"bun", "1.4.4"}, {"openai", "7.45.0"}})},
                                       {"capabilities", Json::object()}}));
            continue;
        }
        if (name == "initialized") {
            respond(*id, g_initialize);
            continue;
        }
        const Json arg = params ? *params : Json();
        if (name != "received")
            received.push(Json::object({{"method", name}, {"params", arg}}));
        if (name == "received") {
            respond(*id, received);
        } else if (name == "_hikari/providers.set" || name == "_hikari/conversation.end") {
            respond(*id, nullptr);
        } else if (name == "session/new") {
            respond(*id, Json::object({{"sessionId", *arg.find("sessionId")}}));
        } else if (name == "session/prompt") {
            const std::string session = arg.find("sessionId")->asString();
            const Json &prompt = *arg.find("prompt");
            const std::string text = prompt.asArray().at(0).find("text")->asString();
            keep(session, Json::object({{"author", "user"}, {"parts", prompt}}));
            FakeTurn turn;
            turn.promptId = *id;
            const auto script = text.starts_with("{") ? Json::parse(text) : std::expected<Json, Json::ParseError>{};
            if (script && script->isObject()) {
                if (const Json *marker = script->find("crashUnless")) {
                    if (!std::filesystem::exists(marker->asString())) {
                        std::ofstream(marker->asString()) << "crashed";
                        std::fputs("fake sidecar: crashing mid-turn\n", stderr);
                        std::fflush(stderr);
                        std::_Exit(3);
                    }
                    turn.say = "recovered";
                }
                if (const Json *say = script->find("say"))
                    turn.say = say->asString();
                if (const Json *calls = script->find("calls"))
                    turn.calls = calls->asArray();
                if (const Json *reason = script->find("reason"))
                    turn.reason = reason->asString();
                turn.hang = script->find("hang") != nullptr;
                if (const Json *compact = script->find("compact"); compact && compact->isArray())
                    for (const Json &answer : compact->asArray())
                        compactions[session].push_back(answer);
                if (const Json *usage = script->find("usage"))
                    sessionUpdate(session, Json::object({{"sessionUpdate", "usage"}, {"usage", *usage}}));
            } else {
                turn.say = "ok: " + text;
            }
            if (!turn.say.empty())
                sessionUpdate(session, Json::object({{"sessionUpdate", "agent_message_chunk"},
                                                     {"content", Json::object({{"type", "text"}, {"text", turn.say}})}}));
            if (const Json *updates = script ? script->find("updates") : nullptr; updates && updates->isArray())
                for (const Json &update : updates->asArray())
                    sessionUpdate(session, update);
            turns[session] = std::move(turn);
            advance(session);
        } else if (name == "_hikari/session.steer") {
            const std::string session = arg.find("sessionId")->asString();
            const auto it = turns.find(session);
            const bool taken = it != turns.end() && !it->second.cancelled &&
                               (it->second.calling || it->second.next < it->second.calls.size());
            respond(*id, Json::object({{"taken", taken}}));
        } else if (name == "_hikari/session.compact") {
            const std::string session = arg.find("sessionId")->asString();
            auto &answers = compactions[session];
            const Json answer = answers.empty() ? Json::object({{"result", Json::object({{"compacted", false}})}})
                                                : answers.front();
            if (!answers.empty())
                answers.erase(answers.begin());
            if (const Json *updates = answer.find("updates"); updates && updates->isArray())
                for (const Json &update : updates->asArray())
                    sessionUpdate(session, update);
            if (const Json *error = answer.find("error"))
                send(Json::object({{"jsonrpc", "2.0"}, {"id", *id}, {"error", *error}}));
            else
                respond(*id, answer.find("result") ? *answer.find("result") : Json());
        } else if (name == "_hikari/session.close") {
            turns.erase(arg.find("sessionId")->asString());
            respond(*id, nullptr);
        } else if (name == "_hikari/complete") {
            const Json *request = arg.find("request");
            const Json *model = request ? request->find("model") : nullptr;
            const std::string modelId = model && model->isString() ? model->asString() : std::string();
            if (modelId == "approve" || modelId == "escalate") {
                const std::string text = modelId == "approve"
                    ? R"(Sure. {"decision": "approve"})"
                    : R"({"decision": "escalate", "reason": "The user didn't ask for that."})";
                respond(*id, Json::object({{"reason", "end"}, {"text", text}, {"toolCalls", Json::array()},
                                           {"usage", Json::object({{"inputTokens", 1000000}, {"outputTokens", 0}})}}));
            } else if (modelId == "inline-upper" || modelId == "inline-drop") {
                const Json *messages = request->find("messages");
                std::string text = messages->asArray()[0].find("parts")->asArray()[0].find("text")->asString();
                const auto open = text.find("<line>") + 6;
                text = text.substr(open, text.find("</line>") - open);
                std::string answer;
                for (std::size_t i = 0; i < text.size(); ++i) {
                    if (modelId == "inline-drop" && text.compare(i, 3, "\xE2\x9F\xA6") == 0) {
                        i = text.find("\xE2\x9F\xA7", i) + 2;
                        continue;
                    }
                    answer += static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
                }
                respond(*id, Json::object({{"reason", "end"}, {"text", answer}, {"toolCalls", Json::array()},
                                           {"usage", Json::object({{"inputTokens", 1000000}, {"outputTokens", 0}})}}));
            } else if (modelId == "hang") {
                hanging.push_back(*id);
            } else if (modelId == "fail") {
                send(Json::object({{"jsonrpc", "2.0"}, {"id", *id},
                                   {"error", Json::object({{"code", -32010}, {"message", "slow down"},
                                                           {"data", Json::object({{"kind", "rateLimited"},
                                                                                  {"httpStatus", 429},
                                                                                  {"detail", "slow down"}})}})}}));
            } else if (modelId == "cites") {
                respond(*id, Json::object({{"reason", "end"}, {"text", "Sung by Aimer [1]."}, {"toolCalls", Json::array()},
                                           {"usage", Json::object({{"inputTokens", 1}, {"outputTokens", 1},
                                                                   {"cachedInputTokens", 0}, {"cacheWriteTokens", 0},
                                                                   {"cacheWriteHourTokens", 0}})},
                                           {"citations", Json::array({Json::object({{"index", 1},
                                                                                    {"url", "https://example.org/a"},
                                                                                    {"title", "A"},
                                                                                    {"quote", "Aimer"}})})},
                                           {"searchSuggestions", "<div>chips</div>"},
                                           {"providerState", Json::object({{"type", "providerState"},
                                                                           {"wire", "gemini"},
                                                                           {"json", "[]"}})}}));
            } else if (modelId == "cites-spans") {
                const auto cited = [](int index, std::optional<std::pair<int, int>> span) {
                    Json c = Json::object({{"index", index}, {"url", "https://example.org/" + std::to_string(index)},
                                           {"title", std::to_string(index)}});
                    if (span) {
                        c["start"] = span->first;
                        c["end"] = span->second;
                    }
                    return c;
                };
                respond(*id, Json::object({{"reason", "end"}, {"text", "ร้องโดย Aimer 🎤 [1] กับ LiSA [2]."},
                                           {"toolCalls", Json::array()},
                                           {"citations", Json::array({cited(1, std::pair(8, 16)),
                                                                      cited(2, std::pair(25, 29)),
                                                                      cited(3, std::pair(30, 99)),
                                                                      cited(4, std::pair(15, 16)),
                                                                      cited(5, std::pair(4, 2)),
                                                                      cited(6, std::nullopt)})}}));
            } else {
                const Json *stream = arg.find("stream");
                send(Json::object({{"jsonrpc", "2.0"}, {"method", "session/update"},
                                   {"params", Json::object({{"stream", stream ? *stream : Json()},
                                                            {"update", Json::object({{"kind", "text"}, {"text", "Hi"}})}})}}));
                respond(*id, Json::object({{"reason", "end"}, {"text", "Hi"}, {"toolCalls", Json::array()},
                                           {"usage", Json::object({{"inputTokens", 3}, {"outputTokens", 1},
                                                                   {"cachedInputTokens", 0}, {"cacheWriteTokens", 2},
                                                                   {"cacheWriteHourTokens", 1}})}}));
            }
        } else if (name == "_hikari/search.test") {
            const Json *service = arg.find("service");
            const std::string chosen = service && service->isString() ? service->asString() : std::string();
            const auto refuse = [&](const char *kind, int status, const char *detail) {
                send(Json::object({{"jsonrpc", "2.0"}, {"id", *id},
                                   {"error", Json::object({{"code", -32010}, {"message", detail},
                                                           {"data", Json::object({{"kind", kind},
                                                                                  {"httpStatus", status},
                                                                                  {"detail", detail}})}})}}));
            };
            if (chosen == "exa")
                refuse("unauthorized", 401, "invalid key");
            else if (chosen == "tavily")
                refuse("rateLimited", 432, "plan limit exceeded");
            else
                respond(*id, nullptr);
        } else if (name == "_hikari/models.list") {
            respond(*id, Json::object({{"models", Json::array({Json::object({{"id", "b/model"}, {"name", "B"}})})}}));
        } else if (name == "echo") {
            respond(*id, arg);
        } else if (name == "environment") {
            Json args = Json::array();
            for (int i = 0; i < argc; ++i)
                args.push(std::string(argv[i]));
            respond(*id, Json::object({{"environment", environment()}, {"arguments", std::move(args)},
                                       {"cwd", std::filesystem::current_path().generic_string()}}));
        } else if (name == "garbage") {
            std::fputs("this is not json\n", stdout);
            std::fflush(stdout);
        } else if (name == "crash") {
            std::fputs("fake sidecar: crashing on request\n", stderr);
            std::fflush(stderr);
            std::_Exit(3);
        } else if (name == "hang") {
            hanging.push_back(*id);
        } else if (name == "stderr") {
            std::fputs((arg.isString() ? arg.asString() : std::string()).c_str(), stderr);
            std::fflush(stderr);
            respond(*id, nullptr);
        } else if (name == "spawn") {
            respond(*id, Json::object({{"pid", spawnSleeper()}}));
        } else if (name == "api") {
            const std::int64_t mine = nextId++;
            waiting.emplace(mine, *id);
            send(Json::object({{"jsonrpc", "2.0"}, {"id", mine}, {"method", "_hikari/api.call"}, {"params", arg}}));
        } else if (name == "notify") {
            send(Json::object({{"jsonrpc", "2.0"}, {"method", "session/update"}, {"params", arg}}));
            respond(*id, nullptr);
        } else {
            send(Json::object({{"jsonrpc", "2.0"}, {"id", *id},
                               {"error", Json::object({{"code", -32601}, {"message", "unknown"}})}}));
        }
    }
    if (g_mode == "ignore-eof")
        sleepForever();
    return 0;
}
