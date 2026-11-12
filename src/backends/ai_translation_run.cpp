#include "hikari/backends/ai_translation_run.h"

#include "hikari/backends/ai_agent.h"
#include "hikari/backends/ai_requests.h"

#include <algorithm>
#include <utility>

namespace hikari::backends::ai {

namespace app = hikari::application::ai;
namespace in = hikari::application::ai_inline;
using api::Json;

namespace {

std::string str(std::u8string_view s)
{
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}

Json modelJson(const app::ModelRef &model)
{
    return Json::object({{"providerId", model.providerId}, {"model", model.modelId}});
}

const Json *member(const Json &object, std::string_view key)
{
    return object.isObject() ? object.find(key) : nullptr;
}

std::string textAt(const Json &object, std::string_view key)
{
    const Json *value = member(object, key);
    return value && value->isString() ? value->asString() : std::string();
}

}

Json translateParams(const std::vector<TranslationRun::Job> &jobs, const TranslationRun::Wiring &wiring)
{
    Json lines = Json::array();
    for (const TranslationRun::Job &job : jobs) {
        Json line = Json::object({{"key", static_cast<std::int64_t>(job.key)},
                                  {"text", str(in::mask(job.prepared.sent).text)},
                                  {"start", job.startMs},
                                  {"end", job.endMs}});
        if (!job.speaker.empty())
            line["speaker"] = job.speaker;
        lines.push(std::move(line));
    }
    Json before = Json::array();
    for (const std::string &text : wiring.before)
        before.push(Json(text));
    Json params = Json::object({{"language", wiring.language},
                                {"models", Json::object({{"translator", modelJson(wiring.translator)},
                                                         {"reviewer", modelJson(wiring.reviewer)}})},
                                {"lines", std::move(lines)},
                                {"before", std::move(before)}});
    if (wiring.document)
        params["document"] = static_cast<std::int64_t>(*wiring.document);
    return params;
}

TranslationRun::TranslationRun(std::vector<Job> jobs, Wiring wiring) : m_jobs(std::move(jobs)), m_wiring(std::move(wiring)) {}

TranslationRun::~TranslationRun()
{
    *m_alive = false;
    if (m_stream)
        if (AiRequests *requests = m_wiring.lines.requests ? m_wiring.lines.requests() : nullptr)
            requests->cancel(*m_stream);
}

void TranslationRun::start()
{
    AiRequests *requests = m_wiring.lines.requests ? m_wiring.lines.requests() : nullptr;
    m_running = true;
    if (!requests || m_wiring.translator.providerId.empty())
        return end(InlineRun::End::NoModel);
    if (m_jobs.empty())
        return end(InlineRun::End::Done);
    if (m_wiring.lines.overDailyCap && m_wiring.lines.overDailyCap())
        return end(InlineRun::End::DailyCap);
    const std::weak_ptr<bool> alive = m_alive;
    AiRequests::TranslateSink sink;
    sink.progress = [this, alive](const Json &params) {
        if (alive.lock())
            progress(params);
    };
    sink.done = [this, alive](std::expected<Json, app::Error> result) {
        if (alive.lock())
            answered(std::move(result));
    };
    const app::StreamId stream = requests->requestTranslation(translateParams(m_jobs, m_wiring), std::move(sink), m_wiring.lines.project);
    if (alive.lock() && m_running)
        m_stream = stream;
}

void TranslationRun::progress(const Json &params)
{
    if (const Json *usage = member(params, "usage")) {
        const app::ModelRef &model = textAt(params, "role") == "reviewer" ? m_wiring.reviewer : m_wiring.translator;
        const auto cost = app::costOf(usageOf(*usage), priceOf(m_wiring.lines.config ? m_wiring.lines.config() : nullptr, model));
        if (cost) {
            m_cost += *cost;
            if (m_wiring.lines.spend)
                m_wiring.lines.spend(*cost);
        } else {
            m_costKnown = false;
        }
        if (!m_stopping && m_wiring.lines.overDailyCap && m_wiring.lines.overDailyCap()) {
            m_stopping = true;
            m_stopReason = InlineRun::End::DailyCap;
            if (AiRequests *requests = m_wiring.lines.requests ? m_wiring.lines.requests() : nullptr; requests && m_stream)
                requests->cancel(*m_stream);
        }
        return;
    }
    const Json *done = member(params, "done");
    const Json *total = member(params, "total");
    if (textAt(params, "stage") == "translate" && done && done->isInt() && total && total->isInt() && m_wiring.progressed)
        m_wiring.progressed(static_cast<std::size_t>(done->asInt()), static_cast<std::size_t>(total->asInt()));
}

void TranslationRun::answered(std::expected<Json, app::Error> result)
{
    m_stream.reset();
    if (!result) {
        if (result.error().kind == app::ErrorKind::Cancelled && m_stopping)
            return end(m_stopReason);
        for (const Job &job : m_jobs)
            if (std::ranges::find(m_dropped, job.key) == m_dropped.end() && m_wiring.lines.lineDone)
                m_wiring.lines.lineDone({job.key, {}, result.error()});
        return end(InlineRun::End::Done);
    }
    const Json *lines = member(*result, "lines");
    const std::weak_ptr<bool> alive = m_alive;
    for (const Job &job : m_jobs) {
        if (std::ranges::find(m_dropped, job.key) != m_dropped.end())
            continue;
        InlineRun::Result line{job.key, {}, std::nullopt};
        const Json *found = nullptr;
        if (lines && lines->isArray())
            for (const Json &l : lines->asArray())
                if (const Json *key = member(l, "key"); key && key->isInt() && static_cast<std::uint64_t>(key->asInt()) == job.key)
                    found = &l;
        if (!found) {
            line.error = app::Error{.kind = app::ErrorKind::Network, .detail = "the pipeline returned no translation"};
        } else {
            line.answer = in::readAnswer(job.prepared, textAt(*found, "text"));
            line.needsReview = textAt(*found, "review") != "approved";
            if (const Json *issues = member(*found, "issues"); issues && issues->isArray())
                for (const Json &issue : issues->asArray())
                    if (issue.isString())
                        line.issues.push_back(issue.asString());
        }
        if (m_wiring.lines.lineDone)
            m_wiring.lines.lineDone(line);
        if (!alive.lock())
            return;
    }
    std::vector<ProjectFile> files;
    if (const Json *changed = member(*result, "files"); changed && changed->isArray())
        for (const Json &file : changed->asArray())
            if (!textAt(file, "name").empty())
                files.push_back({textAt(file, "name"), textAt(file, "data")});
    if (!files.empty() && m_wiring.projectChanged)
        m_wiring.projectChanged(std::move(files));
    if (alive.lock())
        end(m_stopping ? m_stopReason : InlineRun::End::Done);
}

void TranslationRun::stop()
{
    if (!m_running || m_stopping)
        return;
    m_stopping = true;
    m_stopReason = InlineRun::End::Stopped;
    if (!m_stream)
        return end(InlineRun::End::Stopped);
    if (AiRequests *requests = m_wiring.lines.requests ? m_wiring.lines.requests() : nullptr)
        requests->cancel(*m_stream);
}

void TranslationRun::drop(std::uint64_t key)
{
    m_dropped.push_back(key);
}

void TranslationRun::end(InlineRun::End why)
{
    if (!m_running)
        return;
    m_running = false;
    if (m_wiring.lines.finished)
        m_wiring.lines.finished(why);
}

}
