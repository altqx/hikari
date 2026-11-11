#include "hikari/backends/lua_node_helper.h"

#include "hikari/backends/helper_host.h"
#include "hikari/backends/lua_protocol.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <future>
#include <mutex>

Q_LOGGING_CATEGORY(lcNodesSandbox, "hikari.nodes.sandbox")

namespace hikari::backends {

using Result = std::expected<std::vector<std::vector<application::NodeValue>>, application::LuaNodePort::Failure>;
using Failure = application::LuaNodePort::Failure;

sandbox::Profile luaNodeProfile(const QString &helperPath)
{
    return sandbox::Profile{
        .name = QStringLiteral("HikariSub.Lua.Nodes"),
        .readExecute = {QFileInfo(helperPath).absolutePath()},
        .network = sandbox::Network::None,
    };
}

namespace {

std::string secondsText(int ms)
{
    if (ms % 1000 == 0)
        return std::to_string(ms / 1000) + (ms == 1000 ? " second" : " seconds");
    return QString::number(ms / 1000.0).toStdString() + " seconds";
}

struct Job {
    application::LuaNodePort::Call call;
    std::promise<Result> done;
};

}

struct LuaNodeHelper::Worker : QObject {
    Worker(LuaNodeHelper::Options o) : options(std::move(o)), launcher(options.sandbox) {}

    void begin(std::shared_ptr<Job> next)
    {
        job = std::move(next);
        timedOut = false;
        if (host && host->state() == helper::HelperHost::State::Ready)
            return send();
        if (!host || host->state() != helper::HelperHost::State::Starting)
            startHelper();
    }

    void startHelper()
    {
        launcher.killTree();
        host = std::make_unique<helper::HelperHost>(
            options.helper, QStringList{}, lua::kProtocolVersion,
            helper::HelperHost::Limits{.maxOutstandingBytes = helper::kMaxPayload});
        host->setPrepare([this](QProcess &process) { launcher.prepare(process); });
        connect(host.get(), &helper::HelperHost::ready, this, [this] {
            if (job)
                send();
        });
        connect(host.get(), &helper::HelperHost::refused, this,
                [this] { finish(std::unexpected(Failure{"the Lua helper is from another version", true})); });
        connect(host.get(), &helper::HelperHost::lost, this,
                [this] { finish(std::unexpected(Failure{"the Lua helper stopped", true})); });
        ++starts;
        host->start();
    }

    void send()
    {
        auto sent = host->request(nextRun++, encode(job->call),
                                  [this](std::expected<helper::Event, helper::HostError> event) { onEvent(event); });
        if (!sent)
            return finish(std::unexpected(Failure{"the Lua helper could not take the batch", true}));
        request = *sent;
        limit.start(options.timeLimitMs);
    }

    static std::vector<std::byte> encode(const application::LuaNodePort::Call &call)
    {
        helper::Writer w;
        w.i32(static_cast<std::int32_t>(lua::Command::RunNodes));
        auto payload = w.take();
        const auto body = lua::encodeNodeCall(call);
        payload.insert(payload.end(), body.begin(), body.end());
        return payload;
    }

    void onEvent(const std::expected<helper::Event, helper::HostError> &event)
    {
        if (!event) {
            return finish(std::unexpected(Failure{
                timedOut ? "took longer than " + secondsText(options.timeLimitMs) + " and was stopped"
                         : "the Lua helper stopped",
                true}));
        }
        if (event->kind == helper::Kind::Service) {
            host->answer(request, event->call, helper::Outcome::Unsupported);
            return;
        }
        if (event->kind != helper::Kind::Terminal)
            return;
        if (event->outcome != helper::Outcome::Ok) {
            const std::string text = helper::textOf(event->payload);
            return finish(std::unexpected(Failure{text.empty() ? "the Lua code failed" : text, false}));
        }
        auto rows = lua::decodeNodeRows(event->payload, job->call.outputs);
        if (!rows)
            return finish(std::unexpected(Failure{"the Lua helper gave rows it could not read", false}));
        finish(std::move(*rows));
    }

    void finish(Result result)
    {
        limit.stop();
        if (!job)
            return;
        auto done = std::move(job);
        done->done.set_value(std::move(result));
    }

    LuaNodeHelper::Options options;
    sandbox::Launcher launcher;
    std::unique_ptr<helper::HelperHost> host;
    std::shared_ptr<Job> job;
    std::uint64_t request = 0;
    std::uint64_t nextRun = 1;
    bool timedOut = false;
    QTimer limit{this};
    std::atomic<int> starts = 0;
    QThread thread;
    std::mutex running;
};

LuaNodeHelper::LuaNodeHelper(Options options) : m(std::make_unique<Worker>(std::move(options)))
{
    if (m->options.sandbox && !m->launcher.coverage().missing.isEmpty())
        qCWarning(lcNodesSandbox) << "Lua nodes run without a complete sandbox:" << m->launcher.coverage().missing;
    m->limit.setSingleShot(true);
    QObject::connect(&m->limit, &QTimer::timeout, m.get(), [w = m.get()] {
        if (!w->host)
            return;
        w->host->readPending();
        if (!w->job)
            return;
        w->timedOut = true;
        w->host->stop();
    });
    m->thread.setObjectName(QStringLiteral("hikari.lua.nodes"));
    m->moveToThread(&m->thread);
    m->thread.start();
}

LuaNodeHelper::~LuaNodeHelper()
{
    QMetaObject::invokeMethod(
        m.get(),
        [w = m.get(), back = QThread::currentThread()] {
            w->limit.stop();
            w->host.reset();
            w->moveToThread(back);
        },
        Qt::BlockingQueuedConnection);
    m->thread.quit();
    m->thread.wait();
}

Result LuaNodeHelper::run(const Call &call)
{
    std::scoped_lock lock(m->running);
    auto job = std::make_shared<Job>(Job{call, {}});
    auto done = job->done.get_future();
    QMetaObject::invokeMethod(m.get(), [w = m.get(), job] { w->begin(job); }, Qt::QueuedConnection);
    return done.get();
}

const sandbox::Coverage &LuaNodeHelper::sandboxCoverage() const
{
    return m->launcher.coverage();
}

int LuaNodeHelper::starts() const
{
    return m->starts;
}

}
