#pragma once

#include "hikari/application/ai_providers.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <chrono>
#include <map>
#include <memory>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;

/// Streams requests over Qt Network. A key travels only over HTTPS, or plain HTTP to this machine.
class NetworkProviderPort final : public QObject, public app::ProviderPort {
public:
    explicit NetworkProviderPort(QObject *parent = nullptr);
    ~NetworkProviderPort() override;

    /// A stream or a model list that receives nothing for this long fails with Timeout.
    void setIdleTimeout(std::chrono::milliseconds timeout) { m_idleTimeout = timeout; }

    app::StreamId stream(const app::Provider &provider, const std::string &key, const app::Request &request,
                         app::EventSink sink) override;
    void cancel(app::StreamId id) override;
    void listModels(const app::Provider &provider, const std::string &key,
                    std::function<void(ModelsResult)> done) override;

private:
    struct Run;
    void end(app::StreamId id, const std::vector<app::Event> &events);

    QNetworkAccessManager m_network;
    std::chrono::milliseconds m_idleTimeout{std::chrono::minutes(2)};
    std::map<app::StreamId, std::unique_ptr<Run>> m_runs;
    app::StreamId m_nextId = 1;
};

/// Keeps the user's providers and role models in a JSON file. Keys are never part of it.
class JsonConfigStore final : public app::ConfigStore {
public:
    explicit JsonConfigStore(QString path) : m_path(std::move(path)) {}
    app::Config load() override;
    bool save(const app::Config &config) override;

private:
    QString m_path;
};

}
