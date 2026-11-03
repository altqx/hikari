#pragma once

// One plugin in its own hikari-plugin-helper. Work runs one unit at a time: the load, a command
// or an event. A unit that runs longer than the time limit, not counting time HikariSub spends
// answering it, is force stopped. After a stop or a crash the next command starts a new helper.

#include "hikari/backends/helper_host.h"
#include "hikari/backends/plugin_protocol.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace hikari::backends {

class PluginProcess : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Starting, Ready, LoadFailed, Stopped };
    enum class StopReason { TimeLimit, ForceStopped, Crashed };
    Q_ENUM(StopReason)

    struct Limits {
        int timeLimitMs = 10'000;
        std::size_t maxQueued = 64;
    };

    struct ServiceRequest {
        plugin::Service service = plugin::Service::Notify;
        plugin::Level level = plugin::Level::Info; // Notify
        bool toast = false;                        // Notify
        std::string name;                          // Call, Ui: operation; SetSetting: id; Subscribe: event
        std::string json;                          // Call, Ui: params; SetSetting: value
        std::string text;                          // Notify
    };
    // Call and Ui answer the JSON envelope, SetSetting, Subscribe and Lock a refusal (empty: done), Notify and
    // Unlock anything.
    using ServiceReply = std::function<void(std::string)>;
    using ServiceHandler = std::function<void(const ServiceRequest &, ServiceReply)>;
    // Asked at each start, so a restart sees the current settings and language.
    using LoadSource = std::function<plugin::LoadRequest()>;

    PluginProcess(QString helperPath, LoadSource load, QObject *parent = nullptr);
    PluginProcess(QString helperPath, LoadSource load, Limits limits, QObject *parent = nullptr);
    ~PluginProcess() override;

    void setServiceHandler(ServiceHandler handler) { m_services = std::move(handler); }

    void start();
    // Queued behind running work; starts the helper again after a stop. False when the plugin
    // failed to load or the queue is full.
    bool run(const std::string &command);
    // Dropped unless the plugin runs. A queued event of the same name takes the newer data.
    bool push(const std::string &event, const std::string &json) { return deliver(event, json, event); }
    // As push, but a queued event takes the newer data only when both carry the same non-empty key.
    bool deliver(const std::string &event, const std::string &json, const std::string &coalesceKey);
    void forceStop();

    State state() const { return m_state; }
    QString loadError() const { return m_loadError; }
    const std::vector<std::string> &handledCommands() const { return m_handled; }
    bool busy() const { return m_running.has_value(); }
    qint64 processId() const;
    // The helper's stderr, or the last helper's after a stop.
    QByteArray diagnostics() const;

signals:
    void loaded();
    void loadFailed(const QString &message);
    void commandFailed(const QString &command, const QString &message);
    void eventFailed(const QString &event, const QString &message);
    void finished(const QString &command);
    // Before the outcome's own signal, whenever the load, a command or an event ends. A stop or a
    // crash gives stopped or loadFailed instead.
    void unitEnded();
    void stopped(hikari::backends::PluginProcess::StopReason reason);

private:
    struct Unit {
        enum class Kind { Load, Command, Event } kind = Kind::Command;
        std::string name;
        std::string json;
        std::string key;
    };
    void startHelper();
    void next();
    void onEvent(std::expected<helper::Event, helper::HostError> event);
    void onService(const helper::Event &event);
    void endUnit(helper::Outcome outcome, const std::vector<std::byte> &payload);
    void stopHelper(StopReason reason);
    void dropHelper(bool kill);
    void pauseWatchdog();
    void resumeWatchdog();

    QString m_helperPath;
    LoadSource m_load;
    Limits m_limits;
    ServiceHandler m_services;
    std::unique_ptr<helper::HelperHost> m_host;
    State m_state = State::Idle;
    QString m_loadError;
    QByteArray m_lastDiagnostics;
    std::vector<std::string> m_handled;
    std::deque<Unit> m_queue;
    std::optional<Unit> m_running;
    std::uint64_t m_request = 0;
    std::uint64_t m_nextRun = 1;
    int m_openServices = 0;
    qint64 m_spentMs = 0;
    QElapsedTimer m_clock;
    QTimer m_watchdog;
};

}
