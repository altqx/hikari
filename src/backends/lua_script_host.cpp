#include "hikari/backends/lua_script_host.h"

#include "hikari/backends/lua_protocol.h"

#include <QPointer>

#include <chrono>

namespace hikari::backends {

using helper::Event;
using helper::HelperHost;
using helper::HostError;
using helper::Kind;
using helper::Outcome;
using helper::Reader;
using helper::Writer;

LuaScriptHost::LuaScriptHost(QString helperPath, QString scriptPath, QString sharedInclude, int traceLevel,
                             QObject *parent)
    : QObject(parent), m_helperPath(std::move(helperPath)), m_scriptPath(std::move(scriptPath)),
      m_sharedInclude(std::move(sharedInclude)), m_traceLevel(traceLevel)
{
    m_grace.setSingleShot(true);
    connect(&m_grace, &QTimer::timeout, this, [this] {
        if (m_state == State::Running && m_cancelRequested) {
            m_forceStopOffered = true;
            emit forceStopOffered();
        }
    });
}

LuaScriptHost::~LuaScriptHost() = default;

QByteArray LuaScriptHost::diagnostics() const
{
    return m_host ? m_host->diagnostics() : QByteArray();
}

qint64 LuaScriptHost::processId() const
{
    return m_host ? m_host->processId() : 0;
}

std::uint64_t LuaScriptHost::session() const
{
    return m_host ? m_host->session() : 0;
}

void LuaScriptHost::load()
{
    if (m_state != State::Idle)
        return;
    startHelper();
}

void LuaScriptHost::restart()
{
    if (m_state == State::Running)
        endRun(RunOutcome::HelperLost, QStringLiteral("restarted"));
    if (m_host) {
        disconnect(m_host.get(), nullptr, this, nullptr);
        m_host->stop();
        m_host.reset();
    }
    m_info = {};
    m_lastError.clear();
    startHelper();
}

void LuaScriptHost::startHelper()
{
    m_state = State::Loading;
    m_loadStartedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
    m_host = std::make_unique<HelperHost>(m_helperPath, QStringList{}, lua::kProtocolVersion);
    HelperHost *host = m_host.get();
    connect(host, &HelperHost::ready, this, &LuaScriptHost::sendLoad);
    connect(host, &HelperHost::refused, this, [this](quint32 version) {
        m_state = State::LoadFailed;
        m_lastError = QStringLiteral("incompatible Lua helper (protocol %1, expected %2)")
                          .arg(version)
                          .arg(lua::kProtocolVersion);
        emit loadFailed(m_lastError);
    });
    connect(host, &HelperHost::lost, this, [this] {
        const State was = m_state;
        if (was == State::Running)
            endRun(RunOutcome::HelperLost, QStringLiteral("the script's helper process ended"));
        if (was == State::Loading) {
            m_state = State::LoadFailed;
            m_lastError = QStringLiteral("the Lua helper ended while loading the script");
            emit loadFailed(m_lastError);
            return;
        }
        if (was == State::Ready || was == State::Running) {
            // Its Lua state is gone; nothing reruns on its own.
            m_state = State::Unavailable;
            m_lastError = QStringLiteral("the script's helper process ended; restart to load it again");
            emit unavailable(m_lastError);
        }
    });
    host->start();
}

void LuaScriptHost::sendLoad()
{
    Writer w;
    w.i32(static_cast<std::int32_t>(lua::Command::Load))
        .str(m_scriptPath.toStdString())
        .str(m_sharedInclude.toStdString())
        .i32(m_traceLevel);
    const auto sent = m_host->request(0, w.take(), [this](std::expected<Event, HostError> e) {
        if (!e || e->kind != Kind::Terminal)
            return; // loss is reported through HelperHost::lost
        if (e->outcome == Outcome::Ok) {
            if (auto info = lua::decodeInfo(e->payload)) {
                m_info = std::move(*info);
                m_loadMs = (std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count() -
                            m_loadStartedNs) /
                           1e6;
                m_state = State::Ready;
                emit loaded();
                return;
            }
            m_lastError = QStringLiteral("the Lua helper sent malformed script information");
        } else {
            m_lastError = QString::fromStdString(helper::textOf(e->payload));
        }
        m_state = State::LoadFailed;
        emit loadFailed(m_lastError);
    });
    if (!sent) {
        m_state = State::LoadFailed;
        m_lastError = QStringLiteral("the Lua helper did not accept the script");
        emit loadFailed(m_lastError);
    }
}

bool LuaScriptHost::run(int macroIndex)
{
    return run(macroIndex, application::MacroSnapshot{});
}

bool LuaScriptHost::run(int macroIndex, const application::MacroSnapshot &snapshot)
{
    if (m_state != State::Ready || macroIndex < 0 || static_cast<std::size_t>(macroIndex) >= m_info.macros.size())
        return false;
    m_lastResult.reset();
    Writer w;
    w.i32(static_cast<std::int32_t>(lua::Command::Run)).i32(macroIndex);
    const auto body = lua::encodeSnapshot(snapshot);
    auto payload = w.take();
    payload.insert(payload.end(), body.begin(), body.end());
    // Each handler belongs to one request; it learns its ID once request() returns.
    auto id = std::make_shared<std::uint64_t>(0);
    const auto sent = m_host->request(m_nextRun++, std::move(payload), [this, id](std::expected<Event, HostError> e) {
        onEvent(*id, std::move(e));
    });
    if (!sent)
        return false;
    *id = *sent;
    m_runRequest = *sent;
    m_cancelRequested = false;
    m_forceStopOffered = false;
    m_state = State::Running;
    return true;
}

void LuaScriptHost::cancel()
{
    if (m_state != State::Running || !m_host)
        return;
    if (m_cancelRequested)
        return;
    m_cancelRequested = true;
    m_host->cancel(m_runRequest);
    m_grace.start(m_graceMs);
}

bool LuaScriptHost::forceStop()
{
    if (m_state != State::Running || !m_forceStopOffered)
        return false;
    endRun(RunOutcome::ForceStopped, QStringLiteral("force stopped"));
    terminate();
    return true;
}

void LuaScriptHost::terminate()
{
    if (m_state == State::Running)
        endRun(RunOutcome::ForceStopped, QStringLiteral("terminated"));
    if (m_host) {
        disconnect(m_host.get(), nullptr, this, nullptr);
        m_host->stop();
    }
    // The script's state is gone; only an explicit restart brings it back.
    m_state = State::Unavailable;
    m_lastError = QStringLiteral("the script's helper was stopped; restart to load it again");
    emit unavailable(m_lastError);
}

void LuaScriptHost::onEvent(std::uint64_t request, std::expected<Event, HostError> event)
{
    if (!event)
        return; // HelperLost: reported through HelperHost::lost
    if (request != m_runRequest || m_state != State::Running)
        return;
    switch (event->kind) {
    case Kind::Progress: {
        Reader r(event->payload);
        const auto type = static_cast<lua::ProgressEvent>(r.i32());
        if (type == lua::ProgressEvent::Set) {
            const double percent = r.f64();
            if (r.ok())
                emit progressChanged(percent);
            return;
        }
        const QString text = QString::fromStdString(r.str());
        if (!r.ok())
            return;
        if (type == lua::ProgressEvent::Log)
            emit logged(text);
        else if (type == lua::ProgressEvent::Task)
            emit taskChanged(text);
        else if (type == lua::ProgressEvent::Title)
            emit titleChanged(text);
        return;
    }
    case Kind::Service: {
        Reader r(event->payload);
        const auto service = static_cast<lua::Service>(r.i32());
        const std::uint64_t call = event->call;
        const std::uint64_t session = m_host->session();
        std::optional<application::DialogRequest> dialog;
        if (r.ok() && service == lua::Service::Dialog)
            dialog = lua::decodeDialogRequest({event->payload.begin() + 4, event->payload.end()});
        if (!dialog || !m_dialogHandler) {
            m_host->answer(request, call, dialog ? Outcome::Unsupported : Outcome::InvalidInput);
            return;
        }
        m_dialogOpen = true;
        QPointer<LuaScriptHost> self(this);
        m_dialogHandler(*dialog, [self, session, request, call](application::DialogResult result) {
            // Resolve once, and only for the run and helper that asked.
            if (!self || !self->m_host || self->m_host->session() != session || self->m_runRequest != request ||
                !self->m_dialogOpen)
                return;
            self->m_dialogOpen = false;
            self->m_host->answer(request, call, Outcome::Ok, lua::encodeDialogResult(result));
        });
        return;
    }
    case Kind::Terminal: {
        const QString message = QString::fromStdString(helper::textOf(event->payload));
        RunOutcome outcome = event->outcome == Outcome::Ok          ? RunOutcome::Ok
                             : event->outcome == Outcome::Cancelled ? RunOutcome::Cancelled
                                                                    : RunOutcome::Failed;
        if (outcome == RunOutcome::Ok && m_cancelRequested)
            outcome = RunOutcome::Cancelled; // the latch wins over a late success
        if (outcome == RunOutcome::Ok) {
            m_lastResult = lua::decodeMacroResult(event->payload);
            if (!m_lastResult)
                outcome = RunOutcome::Failed; // a malformed result applies nothing
        }
        endRun(outcome, message);
        return;
    }
    default:
        return;
    }
}

void LuaScriptHost::endRun(RunOutcome outcome, const QString &message)
{
    if (m_state != State::Running)
        return;
    m_state = State::Ready;
    m_runRequest = 0;
    m_grace.stop();
    m_forceStopOffered = false;
    if (m_dialogOpen) {
        m_dialogOpen = false;
        emit dialogWithdrawn();
    }
    emit finished(outcome, message);
}

} // namespace hikari::backends
