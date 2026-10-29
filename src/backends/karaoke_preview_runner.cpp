#include "hikari/backends/karaoke_preview_runner.h"

namespace hikari::backends {

namespace {

// kara-templater.lua's second macro, "Apply karaoke template to selected lines"; the macro
// names are translated, their order is not.
constexpr int kSelectedLinesMacro = 1;

}

KaraokePreviewRunner::KaraokePreviewRunner(QString helperPath, QString sharedInclude, QObject *parent)
    : QObject(parent), m_helper(std::move(helperPath)), m_include(std::move(sharedInclude))
{
}

KaraokePreviewRunner::~KaraokePreviewRunner()
{
    if (m_host)
        m_host->terminate();
}

void KaraokePreviewRunner::setServiceHandler(LuaScriptHost::ServiceHandler handler)
{
    m_services = std::move(handler);
}

void KaraokePreviewRunner::setGracePeriod(int ms)
{
    m_graceMs = ms;
    if (m_host)
        m_host->setGracePeriod(ms);
}

void KaraokePreviewRunner::setScript(const QString &path)
{
    if (path == m_script && (m_host || path.isEmpty()))
        return;
    m_script = path;
    m_running = nullptr;
    m_busy = false;
    if (m_host) {
        m_host->disconnect(this);
        m_host->terminate();
        m_host.reset();
    }
    if (!path.isEmpty()) {
        m_host = std::make_unique<LuaScriptHost>(m_helper, path, m_include);
        m_host->setGracePeriod(m_graceMs);
        m_host->setServiceHandler(
            [this](const application::HostServiceRequest &request, LuaScriptHost::ServiceReply reply) {
                if (!m_services || !application::isPreviewService(request.service))
                    reply(application::HostServiceReply::unavailable());
                else
                    m_services(request, std::move(reply));
            });
        connect(m_host.get(), &LuaScriptHost::loaded, this, [this] {
            emit readyChanged();
            startNext();
        });
        connect(m_host.get(), &LuaScriptHost::loadFailed, this, &KaraokePreviewRunner::readyChanged);
        connect(m_host.get(), &LuaScriptHost::unavailable, this, &KaraokePreviewRunner::readyChanged);
        connect(m_host.get(), &LuaScriptHost::finished, this, &KaraokePreviewRunner::finished);
        connect(m_host.get(), &LuaScriptHost::forceStopOffered, this, [this] { m_host->forceStop(); });
        m_host->load();
    }
    emit readyChanged();
}

bool KaraokePreviewRunner::ready() const
{
    return m_host &&
           (m_host->state() == LuaScriptHost::State::Ready || m_host->state() == LuaScriptHost::State::Running) &&
           m_host->info().macros.size() > kSelectedLinesMacro;
}

void KaraokePreviewRunner::run(const application::MacroSnapshot &snapshot, Done done)
{
    m_pending = Pending{snapshot, std::move(done)};
    if (m_busy) {
        m_running = nullptr;
        m_host->cancel();
        return;
    }
    startNext();
}

void KaraokePreviewRunner::cancel()
{
    m_pending.reset();
    if (m_busy) {
        m_running = nullptr;
        m_host->cancel();
    }
}

void KaraokePreviewRunner::startNext()
{
    if (!m_pending || m_busy || !m_host || m_host->state() != LuaScriptHost::State::Ready ||
        m_host->info().macros.size() <= kSelectedLinesMacro)
        return;
    Pending next = std::move(*m_pending);
    m_pending.reset();
    if (!m_host->run(kSelectedLinesMacro, next.snapshot, /*validateFirst*/ true)) {
        if (next.done)
            next.done({std::nullopt, m_host->lastError().toStdString()});
        return;
    }
    m_busy = true;
    m_running = std::move(next.done);
}

void KaraokePreviewRunner::finished(LuaScriptHost::RunOutcome outcome, const QString &message)
{
    m_busy = false;
    Done done = std::move(m_running);
    m_running = nullptr;
    const bool lost =
        outcome == LuaScriptHost::RunOutcome::ForceStopped || outcome == LuaScriptHost::RunOutcome::HelperLost;
    if (lost) {
        QMetaObject::invokeMethod(
            this, [this] { if (m_host) m_host->restart(); }, Qt::QueuedConnection);
    }
    if (done) {
        application::KaraokeTemplaterPort::Preview preview;
        if (outcome == LuaScriptHost::RunOutcome::Ok && m_host->lastResult())
            preview.result = *m_host->lastResult();
        else
            preview.error = message.toStdString();
        done(std::move(preview));
    }
    if (!lost)
        startNext();
}

}
