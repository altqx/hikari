#include "hikari/backends/plugin_process.h"

#include <algorithm>

namespace hikari::backends {

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
    connect(&m_watchdog, &QTimer::timeout, this, [this] { stopHelper(StopReason::TimeLimit); });
}

PluginProcess::~PluginProcess()
{
    m_watchdog.stop();
    if (m_host)
        disconnect(m_host.get(), nullptr, this, nullptr);
}

qint64 PluginProcess::processId() const
{
    return m_host ? m_host->processId() : 0;
}

QByteArray PluginProcess::diagnostics() const
{
    return m_host ? m_host->diagnostics() : m_lastDiagnostics;
}

void PluginProcess::dropHelper(bool kill)
{
    if (!m_host)
        return;
    HelperHost *host = m_host.release();
    disconnect(host, nullptr, this, nullptr);
    if (kill)
        host->stop();
    m_lastDiagnostics = host->diagnostics();
    host->deleteLater();
}

void PluginProcess::start()
{
    if (m_state == State::Idle || m_state == State::Stopped)
        startHelper();
}

void PluginProcess::startHelper()
{
    m_state = State::Starting;
    m_handled.clear();
    m_host = std::make_unique<HelperHost>(m_helperPath, QStringList(), plugin::kProtocolVersion);
    HelperHost *host = m_host.get();
    auto loadFailure = [this](const QString &message) {
        m_state = State::LoadFailed;
        m_loadError = message;
        m_queue.clear();
        dropHelper(false);
        emit loadFailed(message);
    };
    connect(host, &HelperHost::ready, this, [this] {
        m_queue.push_front(Unit{Unit::Kind::Load, {}, {}});
        next();
    });
    connect(host, &HelperHost::refused, this,
            [loadFailure] { loadFailure(QStringLiteral("hikari-plugin-helper speaks another protocol version")); });
    connect(host, &HelperHost::lost, this, [this, host, loadFailure] {
        if (m_host.get() != host)
            return;
        if (m_state == State::Starting) {
            loadFailure(QStringLiteral("hikari-plugin-helper didn't start"));
            return;
        }
        m_state = State::Stopped;
        m_queue.clear();
        dropHelper(false);
        emit stopped(StopReason::Crashed);
    });
    host->start();
}

bool PluginProcess::run(const std::string &command)
{
    if (m_state == State::LoadFailed || m_queue.size() >= m_limits.maxQueued)
        return false;
    m_queue.push_back(Unit{Unit::Kind::Command, command, {}});
    if (m_state == State::Idle || m_state == State::Stopped)
        startHelper();
    else
        next();
    return true;
}

bool PluginProcess::push(const std::string &event, const std::string &json)
{
    if (m_state != State::Ready && m_state != State::Starting)
        return false;
    const auto queued = std::ranges::find_if(
        m_queue, [&](const Unit &u) { return u.kind == Unit::Kind::Event && u.name == event; });
    if (queued != m_queue.end()) {
        queued->json = json;
        return true;
    }
    if (m_queue.size() >= m_limits.maxQueued)
        return false;
    m_queue.push_back(Unit{Unit::Kind::Event, event, json});
    next();
    return true;
}

void PluginProcess::forceStop()
{
    if (m_host && (m_state == State::Ready || m_state == State::Starting))
        stopHelper(StopReason::ForceStopped);
}

void PluginProcess::next()
{
    if (m_running || !m_host || m_host->state() != HelperHost::State::Ready || m_queue.empty())
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
    } else {
        Writer w;
        w.str(unit.name).str(unit.json);
        payload = w.take();
    }
    HelperHost *host = m_host.get();
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
    resumeWatchdog();
}

void PluginProcess::pauseWatchdog()
{
    m_spentMs += m_clock.elapsed();
    m_watchdog.stop();
}

void PluginProcess::resumeWatchdog()
{
    m_clock.start();
    m_watchdog.start(static_cast<int>(std::max<qint64>(0, m_limits.timeLimitMs - m_spentMs)));
}

void PluginProcess::onEvent(std::expected<helper::Event, helper::HostError> event)
{
    if (!event) {
        m_watchdog.stop();
        const bool loading = m_running && m_running->kind == Unit::Kind::Load;
        m_running.reset();
        m_queue.clear();
        dropHelper(false);
        if (loading) {
            m_state = State::LoadFailed;
            m_loadError = QStringLiteral("hikari-plugin-helper stopped while the plugin loaded");
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
    Reader in(event.payload);
    ServiceRequest request;
    request.service = static_cast<plugin::Service>(in.i32());
    switch (request.service) {
    case plugin::Service::Call:
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
    default:
        m_host->answer(m_request, event.call, Outcome::Unsupported);
        return;
    }
    if (!in.ok() || !in.atEnd()) {
        m_host->answer(m_request, event.call, Outcome::InvalidInput);
        return;
    }
    if (m_openServices++ == 0)
        pauseWatchdog();
    HelperHost *host = m_host.get();
    const std::uint64_t requestId = m_request;
    const std::uint64_t call = event.call;
    const bool carriesText = request.service != plugin::Service::Notify;
    ServiceReply reply = [this, host, requestId, call, carriesText](std::string text) {
        if (m_host.get() != host || m_request != requestId)
            return;
        std::vector<std::byte> payload;
        if (carriesText) {
            Writer w;
            w.str(text);
            payload = w.take();
        }
        m_host->answer(requestId, call, Outcome::Ok, std::move(payload));
        if (--m_openServices == 0 && m_running)
            resumeWatchdog();
    };
    if (m_services)
        m_services(request, std::move(reply));
    else if (request.service == plugin::Service::Call)
        reply(R"({"error": {"code": "unavailable", "message": "HikariSub offers no API here"}})");
    else
        reply({});
}

void PluginProcess::endUnit(Outcome outcome, const std::vector<std::byte> &payload)
{
    m_watchdog.stop();
    if (!m_running)
        return;
    const Unit unit = std::move(*m_running);
    m_running.reset();
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
    } else {
        emit eventFailed(name, message);
    }
    next();
}

void PluginProcess::stopHelper(StopReason reason)
{
    m_watchdog.stop();
    m_running.reset();
    m_queue.clear();
    m_openServices = 0;
    m_state = State::Stopped;
    dropHelper(true);
    emit stopped(reason);
}

}
