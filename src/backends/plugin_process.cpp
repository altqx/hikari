#include "hikari/backends/plugin_process.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>

#include <algorithm>

namespace hikari::backends {

sandbox::Profile pluginProfile(const QString &helperPath, const QString &packageFolder, const std::string &id)
{
    const QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(id), QCryptographicHash::Sha256).toHex();
    return sandbox::Profile{
        .name = QStringLiteral("HikariSub.Plugin.") + QString::fromLatin1(hash.left(32)),
        .readExecute = {QFileInfo(helperPath).absolutePath()},
        .read = {packageFolder},
        .network = sandbox::Network::None,
    };
}

using helper::HelperHost;
using helper::Outcome;
using helper::Reader;
using helper::Writer;

PluginProcess::PluginProcess(QString helperPath, LoadSource load, QObject *parent)
    : PluginProcess(std::move(helperPath), std::move(load), Limits{}, parent)
{
}

PluginProcess::PluginProcess(QString helperPath, LoadSource load, Limits limits, QObject *parent)
    : QObject(parent), m_helperPath(std::move(helperPath)), m_load(std::move(load)), m_limits(limits)
{
    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        m_stillWorking = true;
        emit workChanged();
    });
}

PluginProcess::~PluginProcess()
{
    m_watchdog.stop();
    m_timers.clear();
    if (m_host)
        disconnect(m_host.get(), nullptr, this, nullptr);
}

qint64 PluginProcess::processId() const
{
    return m_host ? m_host->processId() : 0;
}

QString PluginProcess::work() const
{
    if (!m_running)
        return {};
    switch (m_running->kind) {
    case Unit::Kind::Load: return QStringLiteral("load");
    case Unit::Kind::Timer: return QStringLiteral("timer");
    case Unit::Kind::Command:
    case Unit::Kind::Event: break;
    }
    return QString::fromStdString(m_running->name);
}

QByteArray PluginProcess::diagnostics() const
{
    return m_host ? m_host->diagnostics() : m_lastDiagnostics;
}

void PluginProcess::dropHelper(bool kill)
{
    m_timers.clear();
    if (!m_host)
        return;
    PluginLink *host = m_host.release();
    disconnect(host, nullptr, this, nullptr);
    if (kill)
        host->stop(true);
    m_lastDiagnostics = host->diagnostics();
    host->deleteLater();
}

void PluginProcess::start()
{
    if (m_state == State::Idle || m_state == State::Stopped || m_state == State::LoadFailed)
        startHelper();
}

void PluginProcess::startHelper()
{
    m_state = State::Starting;
    m_handled.clear();
    auto loadFailure = [this](const QString &message) {
        m_state = State::LoadFailed;
        m_loadError = message;
        m_queue.clear();
        dropHelper(false);
        emit loadFailed(message);
    };
    if (m_linkFactory) {
        auto link = m_linkFactory();
        if (!link) {
            loadFailure(link.error());
            return;
        }
        m_host = std::move(*link);
    } else {
        auto helper = std::make_unique<HelperHost>(m_helperPath, QStringList(), plugin::kProtocolVersion);
        // Windows grants a Launcher's access entries when it is made, and an update replaces the package folder.
        m_launcher = m_sandbox ? std::make_unique<sandbox::Launcher>(m_sandbox) : nullptr;
        if (m_launcher)
            helper->setPrepare([launcher = m_launcher.get()](QProcess &process) { launcher->prepare(process); });
        m_host = std::make_unique<HelperPluginLink>(std::move(helper));
    }
    PluginLink *host = m_host.get();
    connect(host, &PluginLink::started, this, [this] {
        m_queue.push_front(Unit{Unit::Kind::Load, {}, {}, {}});
        next();
    });
    connect(host, &PluginLink::refused, this, [loadFailure](const QString &message) { loadFailure(message); });
    connect(host, &PluginLink::outsideService, this,
            [this](std::uint64_t call, const std::vector<std::byte> &payload) { serve(0, call, payload); });
    connect(host, &PluginLink::lost, this, [this, host, loadFailure] {
        if (m_host.get() != host)
            return;
        if (m_state == State::Starting) {
            loadFailure(host->name() + QStringLiteral(" didn't start"));
            return;
        }
        m_state = State::Stopped;
        m_queue.clear();
        endJob();
        dropHelper(false);
        emit stopped(StopReason::Crashed);
    });
    host->start();
}

bool PluginProcess::run(const std::string &command)
{
    if (m_state == State::LoadFailed || m_queue.size() >= m_limits.maxQueued)
        return false;
    m_queue.push_back(Unit{Unit::Kind::Command, command, {}, {}});
    if (m_state == State::Idle || m_state == State::Stopped)
        startHelper();
    else
        next();
    return true;
}

bool PluginProcess::deliver(const std::string &event, const std::string &json, const std::string &coalesceKey)
{
    if (m_state != State::Ready && m_state != State::Starting)
        return false;
    const auto queued = coalesceKey.empty() ? m_queue.end() : std::ranges::find_if(m_queue, [&](const Unit &u) {
        return u.kind == Unit::Kind::Event && u.key == coalesceKey;
    });
    if (queued != m_queue.end()) {
        queued->json = json;
        return true;
    }
    if (m_queue.size() >= m_limits.maxQueued)
        return false;
    m_queue.push_back(Unit{Unit::Kind::Event, event, json, coalesceKey});
    next();
    return true;
}

void PluginProcess::forceStop()
{
    if (m_host && (m_state == State::Ready || m_state == State::Starting))
        stopHelper(StopReason::ForceStopped);
}

void PluginProcess::end()
{
    m_queue.clear();
    m_running.reset();
    m_watchdog.stop();
    m_timers.clear();
    if (!m_host)
        return;
    PluginLink *host = m_host.release();
    disconnect(host, nullptr, this, nullptr);
    host->stop(false);
    host->deleteLater();
    m_state = State::Stopped;
}

void PluginProcess::next()
{
    if (m_running || !m_host || !m_host->ready() || m_queue.empty())
        return;
    if (m_state != State::Ready && m_queue.front().kind != Unit::Kind::Load)
        return;
    Unit unit = std::move(m_queue.front());
    m_queue.pop_front();
    std::vector<std::byte> payload;
    if (unit.kind == Unit::Kind::Load) {
        payload = plugin::encodeLoad(m_load());
    } else if (unit.kind == Unit::Kind::Command) {
        Writer w;
        w.i32(static_cast<std::int32_t>(plugin::Command::Run)).str(unit.name);
        payload = w.take();
    } else if (unit.kind == Unit::Kind::Timer) {
        Writer w;
        w.i32(static_cast<std::int32_t>(plugin::Command::Timer)).i64(unit.timer).u8(unit.last ? 1 : 0);
        payload = w.take();
    } else {
        Writer w;
        w.str(unit.name).str(unit.json);
        payload = w.take();
    }
    PluginLink *host = m_host.get();
    auto handler = [this, host](std::expected<helper::Event, helper::HostError> event) {
        if (m_host.get() == host)
            onEvent(std::move(event));
    };
    const auto id = unit.kind == Unit::Kind::Event ? host->push(m_nextRun++, std::move(payload), handler)
                                                   : host->request(m_nextRun++, std::move(payload), handler);
    if (!id)
        return;
    m_request = *id;
    m_running = std::move(unit);
    m_spentMs = 0;
    m_openServices = 0;
    m_stillWorking = false;
    resumeWatchdog();
    emit workChanged();
}

void PluginProcess::pauseWatchdog()
{
    m_spentMs += m_clock.elapsed();
    m_watchdog.stop();
}

void PluginProcess::resumeWatchdog()
{
    m_clock.start();
    if (m_job)
        return;
    m_watchdog.start(static_cast<int>(std::max<qint64>(0, m_limits.stillWorkingMs - m_spentMs)));
}

void PluginProcess::onEvent(std::expected<helper::Event, helper::HostError> event)
{
    if (!event) {
        m_watchdog.stop();
        m_stillWorking = false;
        endJob();
        const bool loading = m_running && m_running->kind == Unit::Kind::Load;
        m_running.reset();
        m_queue.clear();
        dropHelper(false);
        if (loading) {
            m_state = State::LoadFailed;
            m_loadError = (m_host ? m_host->name() : QStringLiteral("The plugin")) + QStringLiteral(" stopped while the plugin loaded");
            emit loadFailed(m_loadError);
        } else {
            m_state = State::Stopped;
            emit stopped(StopReason::Crashed);
        }
        return;
    }
    if (event->kind == helper::Kind::Service)
        onService(*event);
    else if (event->kind == helper::Kind::Terminal)
        endUnit(event->outcome, event->payload);
}

void PluginProcess::onService(const helper::Event &event)
{
    serve(m_request, event.call, event.payload);
}

void PluginProcess::serve(std::uint64_t requestId, std::uint64_t call, const std::vector<std::byte> &payload)
{
    Reader in(payload);
    ServiceRequest request;
    request.service = static_cast<plugin::Service>(in.i32());
    switch (request.service) {
    case plugin::Service::Call:
    case plugin::Service::Ui:
    case plugin::Service::Files:
    case plugin::Service::Net:
    case plugin::Service::Job:
    case plugin::Service::Timer:
    case plugin::Service::SetSetting:
        request.name = in.str();
        request.json = in.str();
        break;
    case plugin::Service::Notify: {
        const std::int32_t level = in.i32();
        request.level = static_cast<plugin::Level>(std::clamp(level, 0, 2));
        request.toast = in.u8() != 0;
        request.text = in.str();
        break;
    }
    case plugin::Service::Subscribe:
        request.name = in.str();
        break;
    case plugin::Service::Lock:
    case plugin::Service::Unlock:
        break;
    default:
        m_host->answer(requestId, call, Outcome::Unsupported);
        return;
    }
    if (!in.ok() || !in.atEnd()) {
        m_host->answer(requestId, call, Outcome::InvalidInput);
        return;
    }
    if (request.service == plugin::Service::Job || request.service == plugin::Service::Timer) {
        Writer w;
        w.str(request.service == plugin::Service::Job ? job(request.name, request.json) : timer(request.name, request.json));
        m_host->answer(requestId, call, Outcome::Ok, w.take());
        return;
    }
    const bool inUnit = requestId != 0;
    if (inUnit && m_openServices++ == 0)
        pauseWatchdog();
    PluginLink *host = m_host.get();
    const bool carriesText = request.service != plugin::Service::Notify && request.service != plugin::Service::Unlock;
    ServiceReply reply = [this, host, requestId, call, carriesText, inUnit](std::string text) {
        if (m_host.get() != host || (inUnit && m_request != requestId))
            return;
        std::vector<std::byte> payload;
        if (carriesText) {
            Writer w;
            w.str(text);
            payload = w.take();
        }
        m_host->answer(requestId, call, Outcome::Ok, std::move(payload));
        if (inUnit && --m_openServices == 0 && m_running)
            resumeWatchdog();
    };
    if (m_services)
        m_services(request, std::move(reply));
    else if (request.service == plugin::Service::Call || request.service == plugin::Service::Ui ||
             request.service == plugin::Service::Files || request.service == plugin::Service::Net)
        reply(R"({"error": {"code": "unavailable", "message": "HikariSub offers no API here"}})");
    else
        reply({});
}

std::string PluginProcess::job(const std::string &operation, const std::string &json)
{
    const QJsonObject params = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
    const auto refused = [](const char *message) {
        return QJsonDocument(QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("refused")},
                                                                                {QStringLiteral("message"), QString::fromUtf8(message)}}}})
            .toJson(QJsonDocument::Compact)
            .toStdString();
    };
    const auto result = [](QJsonObject value) {
        return QJsonDocument(QJsonObject{{QStringLiteral("result"), value}}).toJson(QJsonDocument::Compact).toStdString();
    };
    const auto userClick = [](const Unit &unit) {
        const bool dialog = unit.name.starts_with("dialog:");
        if (unit.kind != Unit::Kind::Event || !(dialog || unit.name.starts_with("panel:")))
            return false;
        const QString kind =
            QJsonDocument::fromJson(QByteArray::fromStdString(unit.json)).object().value(QStringLiteral("kind")).toString();
        return kind == QStringLiteral("click") || (dialog && kind == QStringLiteral("button"));
    };
    if (operation == "job.cancelled")
        return result({{QStringLiteral("cancelled"), m_jobCancelled}});
    if (operation == "job.begin") {
        if (!m_running ||
            !(m_running->kind == Unit::Kind::Command || m_running->kind == Unit::Kind::Timer || userClick(*m_running)))
            return refused("Only a command, a timer callback or a click in the plugin's own panel or dialog can become a job.");
        if (m_job)
            return refused("This is a job already.");
        m_job = true;
        m_jobCancelled = false;
        m_stillWorking = false;
        m_watchdog.stop();
        emit jobBegan(QString::fromStdString(m_running->name), params.value(QStringLiteral("title")).toString());
        emit workChanged();
        return result({});
    }
    if (operation == "job.progress") {
        if (!m_job)
            return refused("Call hikari.job.begin first.");
        const QJsonValue fraction = params.value(QStringLiteral("fraction"));
        emit jobProgress(fraction.isDouble() ? std::clamp(fraction.toDouble(), 0.0, 1.0) : -1.0,
                         params.value(QStringLiteral("text")).toString());
        return result({});
    }
    return refused("No such job operation.");
}

void PluginProcess::cancelJob()
{
    if (m_job)
        m_jobCancelled = true;
}

void PluginProcess::endJob()
{
    if (!std::exchange(m_job, false))
        return;
    m_jobCancelled = false;
    emit jobEnded();
}

std::string PluginProcess::timer(const std::string &operation, const std::string &json)
{
    const QJsonObject params = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
    const auto envelope = [](QJsonObject body) { return QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString(); };
    const auto error = [&](const char *code, const char *message) {
        return envelope({{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QString::fromLatin1(code)},
                                                               {QStringLiteral("message"), QString::fromUtf8(message)}}}});
    };
    if (!m_background)
        return error("not_granted", "Timers need the background permission in hikari-plugin.json.");
    if (operation == "timer.cancel") {
        const auto id = static_cast<std::int64_t>(params.value(QStringLiteral("id")).toDouble());
        m_timers.erase(id);
        std::erase_if(m_queue, [id](const Unit &u) { return u.kind == Unit::Kind::Timer && u.timer == id; });
        return envelope({{QStringLiteral("result"), QJsonObject{}}});
    }
    const bool repeat = operation == "timer.every";
    if (!repeat && operation != "timer.after")
        return error("invalid_params", "No such timer operation.");
    const QJsonValue ms = params.value(QStringLiteral("ms"));
    constexpr double kLongest = 2'147'483'647.0;
    if (!ms.isDouble() || !(ms.toDouble() >= 0) || ms.toDouble() > kLongest)
        return error("invalid_params", "A timer takes 0 to 2147483647 milliseconds.");
    const int interval = static_cast<int>(repeat ? std::max(ms.toDouble(), 100.0) : ms.toDouble());
    const std::int64_t id = m_nextTimer++;
    Timer &entry = m_timers[id];
    entry.repeat = repeat;
    entry.clock = std::make_unique<QTimer>();
    entry.clock->setSingleShot(!repeat);
    entry.clock->setTimerType(Qt::PreciseTimer);
    connect(entry.clock.get(), &QTimer::timeout, this, [this, id] { fire(id); });
    entry.clock->start(interval);
    return envelope({{QStringLiteral("result"), QJsonObject{{QStringLiteral("id"), double(id)}}}});
}

void PluginProcess::fire(std::int64_t id)
{
    const auto it = m_timers.find(id);
    if (it == m_timers.end())
        return;
    const bool last = !it->second.repeat;
    if (last)
        m_timers.erase(it);
    const auto same = [id](const Unit &u) { return u.kind == Unit::Kind::Timer && u.timer == id; };
    if (!last && ((m_running && same(*m_running)) || std::ranges::any_of(m_queue, same)))
        return;
    if (m_state != State::Ready || m_queue.size() >= m_limits.maxQueued)
        return;
    m_queue.push_back(Unit{Unit::Kind::Timer, {}, {}, {}, id, last});
    next();
}

void PluginProcess::endUnit(Outcome outcome, const std::vector<std::byte> &payload)
{
    m_watchdog.stop();
    m_stillWorking = false;
    endJob();
    if (!m_running)
        return;
    const Unit unit = std::move(*m_running);
    m_running.reset();
    emit unitEnded();
    emit workChanged();
    const QString name = QString::fromStdString(unit.name);
    QString message = QString::fromStdString(helper::textOf(payload));
    if (outcome != Outcome::Ok && outcome != Outcome::Failed)
        message = QStringLiteral("hikari-plugin-helper refused the request");
    if (unit.kind == Unit::Kind::Load) {
        auto handled = outcome == Outcome::Ok ? plugin::decodeStrings(payload) : std::nullopt;
        if (!handled) {
            m_state = State::LoadFailed;
            m_loadError = message;
            m_queue.clear();
            dropHelper(true);
            emit loadFailed(message);
            return;
        }
        m_handled = std::move(*handled);
        m_state = State::Ready;
        emit loaded();
    } else if (outcome == Outcome::Ok) {
        if (unit.kind == Unit::Kind::Command)
            emit finished(name);
    } else if (unit.kind == Unit::Kind::Command) {
        emit commandFailed(name, message);
    } else if (unit.kind == Unit::Kind::Timer) {
        emit eventFailed(QStringLiteral("timer"), message);
    } else {
        emit eventFailed(name, message);
    }
    next();
}

void PluginProcess::stopHelper(StopReason reason)
{
    m_watchdog.stop();
    m_stillWorking = false;
    endJob();
    m_running.reset();
    m_queue.clear();
    m_openServices = 0;
    m_state = State::Stopped;
    dropHelper(true);
    emit stopped(reason);
}

}
