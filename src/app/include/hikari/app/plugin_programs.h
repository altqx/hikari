#pragma once

#include "hikari/api/api.h"
#include "hikari/plugins/manifest.h"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace hikari::backends {
class ProgramProcess;
}

namespace hikari::app {

class PluginPrograms : public QObject, public api::ProgramHost {
    Q_OBJECT
public:
    struct Plugin {
        const plugins::Manifest *manifest = nullptr;
        QString package, data, cache;
        api::Json settings;
    };
    struct Wiring {
        std::function<std::optional<Plugin>(const std::string &pluginId)> plugin;
        std::function<bool()> networkAllowed;
        std::function<void(const std::string &pluginId, const QString &line)> log;
        api::Api *api = nullptr;
        QString lowRightsHelper;
        std::function<QString(const std::string &pluginId, const plugins::ProgramRun &run)> downloaded;
    };

    explicit PluginPrograms(QObject *parent = nullptr);
    ~PluginPrograms() override;
    void bind(Wiring wiring) { m_wiring = std::move(wiring); }

    void run(const api::Caller &caller, api::ProgramCall call, api::Reply reply) override;
    api::Result start(const api::Caller &caller, api::ProgramCall call) override;
    api::Result send(const api::Caller &caller, const std::string &process, const std::string &bytes, bool close) override;
    api::Result stop(const api::Caller &caller, const std::string &process) override;
    void end(const std::string &pluginId);
    std::size_t started(const std::string &pluginId) const;

private:
    struct Running;
    std::expected<std::unique_ptr<backends::ProgramProcess>, api::Error> launch(const std::string &pluginId,
                                                                                const api::ProgramCall &call) const;
    Running *find(const api::Caller &caller, const std::string &process);
    void onOutput(std::uint64_t key, bool error, const QByteArray &bytes);
    void onFinished(std::uint64_t key, int exitCode, bool killed);
    void logLine(const std::string &pluginId, const QString &line);

    Wiring m_wiring;
    std::map<std::uint64_t, std::unique_ptr<Running>> m_running;
    std::uint64_t m_next = 1;
    struct LogBudget {
        QDateTime since;
        int lines = 0;
    };
    std::map<std::string, LogBudget> m_logBudget;
};

}
