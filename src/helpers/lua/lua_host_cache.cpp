#include "lua_host_cache.h"

#include <utility>

namespace hikari::lua_host {

using application::HostService;
using application::HostServiceRequest;
using application::TextExtents;

namespace {

HostServiceRequest measureRequest(HostService service, std::vector<std::string> texts,
                                  const std::vector<std::string> &style)
{
    HostServiceRequest request;
    request.service = service;
    request.strings = std::move(texts);
    request.style = style;
    return request;
}

TextExtents extentsAt(const std::vector<double> &numbers, std::size_t i)
{
    return TextExtents{numbers[i * 4], numbers[i * 4 + 1], numbers[i * 4 + 2], numbers[i * 4 + 3]};
}

struct RunCaches {
    explicit RunCaches(const Ask &ask) : measure(ask), frameTimes(ask) {}
    HostTextMeasure measure;
    application::karaoke_layout::ExtentsCache extents{measure};
    HostFrameTimes frameTimes;
};

Ask g_ask;
std::optional<RunCaches> g_run;

RunCaches &run()
{
    if (!g_run)
        g_run.emplace(g_ask);
    return *g_run;
}

}

void beginRun(Ask ask)
{
    g_ask = std::move(ask);
    g_run.reset();
}

application::karaoke_layout::ExtentsCache &runExtents()
{
    return run().extents;
}

HostFrameTimes &runFrameTimes()
{
    return run().frameTimes;
}

std::optional<TextExtents> HostTextMeasure::measure(const std::vector<std::string> &style, const std::string &text)
{
    if (text.empty())
        return TextExtents{};
    const auto reply = m_ask(measureRequest(HostService::TextExtents, {text}, style));
    if (!reply || reply->numbers.size() != 4)
        return std::nullopt;
    return extentsAt(reply->numbers, 0);
}

std::vector<std::optional<TextExtents>> HostTextMeasure::measureMany(const std::vector<std::string> &style,
                                                                     const std::vector<std::string> &texts)
{
    std::vector<std::string> asked;
    for (const auto &text : texts)
        if (!text.empty())
            asked.push_back(text);
    if (m_batchUnavailable || asked.empty())
        return TextMeasurePort::measureMany(style, texts);
    const auto reply = m_ask(measureRequest(HostService::TextExtentsMany, asked, style));
    if (!reply) {
        m_batchUnavailable = true;
        return TextMeasurePort::measureMany(style, texts);
    }
    if (reply->integers.size() != asked.size() || reply->numbers.size() != asked.size() * 4)
        throw HostFailure{backends::helper::Outcome::InvalidInput};
    std::vector<std::optional<TextExtents>> out;
    out.reserve(texts.size());
    std::size_t next = 0;
    for (const auto &text : texts) {
        if (text.empty()) {
            out.push_back(TextExtents{});
            continue;
        }
        out.push_back(reply->integers[next] != 0 ? std::optional(extentsAt(reply->numbers, next)) : std::nullopt);
        ++next;
    }
    return out;
}

const std::optional<application::AegisubFrameTimes> &HostFrameTimes::times()
{
    if (m_times)
        return *m_times;
    HostServiceRequest request;
    request.service = HostService::FrameTimes;
    const auto reply = m_ask(std::move(request));
    if (!reply)
        return m_times.emplace(std::nullopt);
    const auto &v = reply->integers;
    if (v.size() < 3 || v[0] <= 0)
        throw HostFailure{backends::helper::Outcome::InvalidInput};
    application::Timecodes timecodes;
    timecodes.numerator = v[0];
    timecodes.last = v[1];
    timecodes.startsMs.assign(v.begin() + 2, v.end());
    return m_times.emplace(application::AegisubFrameTimes::fromTimecodes(timecodes));
}

}
