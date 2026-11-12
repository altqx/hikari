#include "hikari/api/json.h"

#include <QProcess>
#include <QProcessEnvironment>

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
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

std::unique_ptr<QProcess> startServer(const Json &server)
{
    auto process = std::make_unique<QProcess>();
    QStringList args;
    if (const Json *a = server.find("args"); a && a->isArray())
        for (const Json &arg : a->asArray())
            args << text(&arg);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (const Json *e = server.find("env"); e && e->isArray())
        for (const Json &variable : e->asArray())
            environment.insert(text(variable.find("name")), text(variable.find("value")));
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process->start(text(server.find("command")), args);
    return process->waitForStarted(10'000) ? std::move(process) : nullptr;
}

std::string ask(QProcess &server, const Json &message)
{
    server.write(QByteArray::fromStdString(message.dump() + "\n"));
    while (!server.canReadLine())
        if (!server.waitForReadyRead(10'000))
            return {};
    return server.readLine().trimmed().toStdString();
}

}

// An ACP agent without HTTP MCP: it runs the stdio server `session/new` names. A prompt "tools" asks that server for
// its tools and says the answer; "close" closes the server's stdin and says how it exited.
int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::unique_ptr<QProcess> server;
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
            server = entry.find("command") ? startServer(entry) : nullptr;
            reply(id, Json::object({{"sessionId", "s1"}}));
        } else if (method == "session/prompt") {
            const std::string say = params->find("prompt")->asArray()[0].find("text")->asString();
            std::string answer = "no server";
            if (server && say == "tools") {
                answer = ask(*server, Json::object({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}}));
            } else if (server && say == "close") {
                server->closeWriteChannel();
                answer = server->waitForFinished(10'000) ? "exited " + std::to_string(server->exitCode()) : "still running";
            }
            note("session/update", Json::object({{"sessionId", "s1"},
                                                 {"update", Json::object({{"sessionUpdate", "agent_message_chunk"},
                                                                          {"content", Json::object({{"type", "text"},
                                                                                                    {"text", answer}})}})}}));
            reply(id, Json::object({{"stopReason", "end_turn"}}));
        }
    }
    return 0;
}
