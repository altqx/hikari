#include "hikari/api/json.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
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
    std::string line;
    while (std::getline(std::cin, line)) {
        auto parsed = Json::parse(line);
        if (!parsed)
            return 9;
        const Json &message = *parsed;
        const Json *method = message.find("method");
        const Json *id = message.find("id");
        const Json *params = message.find("params");
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
        if (!id)
            continue;
        if (name == "initialize") {
            if (g_mode == "silent")
                continue;
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
        } else if (name == "_hikari/complete") {
            const Json *request = arg.find("request");
            const Json *model = request ? request->find("model") : nullptr;
            const std::string modelId = model && model->isString() ? model->asString() : std::string();
            if (modelId == "hang") {
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
