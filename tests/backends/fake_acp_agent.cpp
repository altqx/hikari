#include "hikari/api/json.h"

#include <QProcess>
#include <QProcessEnvironment>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

using hikari::api::Json;

namespace {

void print(const Json &message)
{
    std::fputs((message.dump() + "\n").c_str(), stdout);
    std::fflush(stdout);
}

void reply(const Json &id, Json result)
{
    print(Json::object({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}));
}

void note(const std::string &method, Json params)
{
    print(Json::object({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}));
}

QString text(const Json *value)
{
    return value && value->isString() ? QString::fromStdString(value->asString()) : QString();
}

QStringList arguments(const Json &server)
{
    QStringList args;
    if (const Json *a = server.find("args"); a && a->isArray())
        for (const Json &arg : a->asArray())
            args << text(&arg);
    return args;
}

QProcessEnvironment environment(const Json &server)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (const Json *e = server.find("env"); e && e->isArray())
        for (const Json &variable : e->asArray())
            environment.insert(text(variable.find("name")), text(variable.find("value")));
    return environment;
}

class Server
{
public:
    explicit Server(const Json &entry)
    {
        process.setProcessEnvironment(environment(entry));
#ifdef _WIN32
        // QProcess's stdio pipes are named pipes, which a process in an AppContainer can't open; anonymous ones work.
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        CreatePipe(&childIn, &toChild, &inherit, 0);
        CreatePipe(&fromChild, &childOut, &inherit, 0);
        SetHandleInformation(toChild, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(fromChild, HANDLE_FLAG_INHERIT, 0);
        process.setInputChannelMode(QProcess::ForwardedInputChannel);
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args) {
            args->startupInfo->dwFlags |= STARTF_USESTDHANDLES;
            args->startupInfo->hStdInput = childIn;
            args->startupInfo->hStdOutput = childOut;
        });
#else
        process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
#endif
        process.start(text(entry.find("command")), arguments(entry));
        if (!process.waitForStarted(10'000))
            startError = process.errorString().toStdString();
#ifdef _WIN32
        CloseHandle(childIn);
        CloseHandle(childOut);
#endif
    }

#ifdef _WIN32
    ~Server()
    {
        closeInput();
        CloseHandle(fromChild);
    }
#endif

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    std::string startError;

    std::string ask(const Json &message)
    {
        const std::string out = message.dump() + "\n";
#ifdef _WIN32
        DWORD written = 0;
        WriteFile(toChild, out.data(), DWORD(out.size()), &written, nullptr);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::string read;
        while (read.find('\n') == std::string::npos) {
            DWORD available = 0;
            if (!PeekNamedPipe(fromChild, nullptr, 0, nullptr, &available, nullptr))
                return {};
            if (!available) {
                if (std::chrono::steady_clock::now() > deadline)
                    return {};
                Sleep(20);
                continue;
            }
            std::string chunk(available, '\0');
            DWORD got = 0;
            if (!ReadFile(fromChild, chunk.data(), available, &got, nullptr))
                return {};
            read.append(chunk, 0, got);
        }
        return QByteArray::fromStdString(read.substr(0, read.find('\n'))).trimmed().toStdString();
#else
        process.write(QByteArray::fromStdString(out));
        while (!process.canReadLine())
            if (!process.waitForReadyRead(10'000))
                return {};
        return process.readLine().trimmed().toStdString();
#endif
    }

    std::string close()
    {
        closeInput();
        return process.waitForFinished(10'000) ? "exited " + std::to_string(process.exitCode()) : "still running";
    }

private:
    void closeInput()
    {
#ifdef _WIN32
        if (toChild)
            CloseHandle(std::exchange(toChild, nullptr));
#else
        process.closeWriteChannel();
#endif
    }

    QProcess process;
#ifdef _WIN32
    HANDLE childIn = nullptr, toChild = nullptr, fromChild = nullptr, childOut = nullptr;
#endif
};

}

// An ACP agent without HTTP MCP: it runs the stdio server `session/new` names. A prompt "tools" asks that server for
// its tools and says the answer; "close" closes the server's stdin and says how it exited.
int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::unique_ptr<Server> server;
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto parsed = Json::parse(line);
        if (!parsed || !parsed->find("method") || !parsed->find("id"))
            continue;
        const Json &id = *parsed->find("id");
        const std::string method = parsed->find("method")->asString();
        const Json *params = parsed->find("params");
        if (method == "initialize") {
            reply(id, Json::object({{"protocolVersion", 1},
                                    {"agentCapabilities", Json::object({{"loadSession", false}})}}));
        } else if (method == "session/new") {
            const Json *servers = params ? params->find("mcpServers") : nullptr;
            const Json entry = servers && servers->isArray() && !servers->asArray().empty() ? servers->asArray()[0] : Json();
            note("fake/session", Json::object({{"cwd", params->find("cwd") ? *params->find("cwd") : Json()},
                                               {"server", entry}}));
            server = entry.find("command") ? std::make_unique<Server>(entry) : nullptr;
            reply(id, Json::object({{"sessionId", "s1"}}));
        } else if (method == "session/prompt") {
            const std::string say = params->find("prompt")->asArray()[0].find("text")->asString();
            std::string answer = !server                      ? "no server"
                                 : !server->startError.empty() ? "the server didn't start: " + server->startError
                                 : say == "tools" ? server->ask(Json::object({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}}))
                                 : say == "close" ? server->close()
                                                  : "no server";
            note("session/update", Json::object({{"sessionId", "s1"},
                                                 {"update", Json::object({{"sessionUpdate", "agent_message_chunk"},
                                                                          {"content", Json::object({{"type", "text"},
                                                                                                    {"text", answer}})}})}}));
            reply(id, Json::object({{"stopReason", "end_turn"}}));
        }
    }
    return 0;
}
