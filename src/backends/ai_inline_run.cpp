#include "hikari/backends/ai_inline_run.h"

#include "hikari/backends/ai_agent.h"
#include "hikari/backends/ai_requests.h"

#include <utility>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;
namespace in = hikari::application::ai_inline;
using api::Json;

namespace {

constexpr int kMaxOutputTokens = 1024;

}

Json inlineRequest(const in::Prepared &prepared)
{
    return Json::object(
        {{"system", prepared.system},
         {"messages", Json::array({Json::object(
                          {{"author", "user"},
                           {"parts", Json::array({Json::object({{"type", "text"}, {"text", prepared.user}})})}})})},
         {"maxOutputTokens", kMaxOutputTokens}});
}

InlineRun::InlineRun(app::Role role, std::vector<Job> jobs, Wiring wiring)
    : m_role(role), m_wiring(std::move(wiring)), m_total(jobs.size())
{
    for (Job &job : jobs)
        m_waiting.push_back(std::move(job));
}

InlineRun::~InlineRun()
{
    *m_alive = false;
    if (AiRequests *requests = m_wiring.requests ? m_wiring.requests() : nullptr)
        for (const auto &[key, stream] : m_running)
            requests->cancel(stream);
}

void InlineRun::start()
{
    const app::Config *config = m_wiring.config ? m_wiring.config() : nullptr;
    m_model = config ? config->modelFor(m_role) : std::nullopt;
    if (!m_model || !m_wiring.requests || !m_wiring.requests())
        return end(End::NoModel);
    pump();
}

void InlineRun::pump()
{
    const std::weak_ptr<bool> alive = m_alive;
    while (!m_ended && !m_stopping && m_running.size() < std::max<std::size_t>(m_wiring.parallel, 1) &&
           !m_waiting.empty()) {
        if (m_wiring.overDailyCap && m_wiring.overDailyCap())
            return stopFor(End::DailyCap);
        AiRequests *requests = m_wiring.requests ? m_wiring.requests() : nullptr;
        if (!requests)
            return end(End::NoModel);
        Job job = std::move(m_waiting.front());
        m_waiting.pop_front();
        const std::uint64_t key = job.key;
        Json request = inlineRequest(job.prepared);
        m_prepared[key] = std::move(job.prepared);
        m_running[key] = 0;
        const app::StreamId stream =
            requests->complete(m_role, std::move(request), [this, alive, key](const app::Event &event) {
                if (alive.lock())
                    received(key, event);
            });
        if (!alive.lock())
            return;
        if (const auto it = m_running.find(key); it != m_running.end())
            it->second = stream;
    }
    if (!m_ended && m_running.empty() && (m_waiting.empty() || m_stopping))
        end(m_stopping ? m_stopReason : End::Done);
}

void InlineRun::received(std::uint64_t key, const app::Event &event)
{
    const auto *finished = std::get_if<app::Finished>(&event);
    const auto *failed = std::get_if<app::Failed>(&event);
    if (!finished && !failed)
        return;
    const auto running = m_running.find(key);
    if (running == m_running.end())
        return;
    m_running.erase(running);
    const auto prepared = m_prepared.find(key);
    if (prepared == m_prepared.end())
        return pump();
    const in::Prepared p = std::move(prepared->second);
    m_prepared.erase(prepared);

    if (finished && m_model) {
        const auto cost = app::costOf(finished->usage, priceOf(m_wiring.config ? m_wiring.config() : nullptr, *m_model));
        if (cost) {
            m_cost += *cost;
            if (m_wiring.spend)
                m_wiring.spend(*cost);
        } else {
            m_costKnown = false;
        }
    }
    if (failed && failed->error.kind == app::ErrorKind::Cancelled && m_stopping)
        return pump();

    ++m_done;
    Result result{key, {}, std::nullopt};
    if (finished)
        result.answer = in::readAnswer(p, finished->text);
    else
        result.error = failed->error;
    const std::weak_ptr<bool> alive = m_alive;
    if (m_wiring.lineDone)
        m_wiring.lineDone(result);
    if (!alive.lock())
        return;
    if (m_wiring.overDailyCap && m_wiring.overDailyCap())
        return stopFor(End::DailyCap);
    pump();
}

void InlineRun::stop()
{
    stopFor(End::Stopped);
}

void InlineRun::stopFor(End why)
{
    if (m_ended || m_stopping)
        return;
    m_stopping = true;
    m_stopReason = why;
    m_waiting.clear();
    if (m_running.empty())
        return end(why);
    std::vector<app::StreamId> streams;
    for (const auto &[key, stream] : m_running)
        streams.push_back(stream);
    if (AiRequests *requests = m_wiring.requests ? m_wiring.requests() : nullptr)
        for (const app::StreamId stream : streams)
            requests->cancel(stream);
}

void InlineRun::drop(std::uint64_t key)
{
    if (std::erase_if(m_waiting, [key](const Job &job) { return job.key == key; }) > 0) {
        --m_total;
        return;
    }
    if (m_prepared.erase(key) > 0)
        --m_total;
}

void InlineRun::end(End why)
{
    if (m_ended)
        return;
    m_ended = true;
    m_waiting.clear();
    if (m_wiring.finished)
        m_wiring.finished(why);
}

}
