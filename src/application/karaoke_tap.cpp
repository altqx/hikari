#include "hikari/application/karaoke_tap.h"

#include <algorithm>
#include <cmath>

namespace hikari::application {

namespace {

double median(std::vector<double> v)
{
    std::ranges::sort(v);
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

double medianDeviation(const std::vector<double> &v, double centre)
{
    std::vector<double> d;
    d.reserve(v.size());
    for (const double x : v)
        d.push_back(std::fabs(x - centre));
    return median(std::move(d));
}

bool blank(std::u16string_view text)
{
    return std::ranges::all_of(text, [](char16_t c) { return c == u' ' || c == u'\t' || c == u'　'; });
}

std::u16string withoutEmptyBlocks(std::u16string text)
{
    for (std::size_t at; (at = text.find(u"{}")) != std::u16string::npos;)
        text.erase(at, 2);
    return text;
}

}

int tapSongMs(double positionMs, int latencyMs, double speed)
{
    const double ms = positionMs - double(latencyMs) * speed;
    return std::max(0, static_cast<int>(std::lround(ms / 10.0)) * 10);
}

LatencyCalibration calibrateLatency(std::span<const double> beatsMs, std::span<const double> pressesMs, int practice)
{
    LatencyCalibration result;
    if (beatsMs.empty())
        return result;
    double interval = kCalibrationIntervalMs;
    for (std::size_t i = 1; i < beatsMs.size(); i++)
        interval = std::min(interval, beatsMs[i] - beatsMs[i - 1]);
    std::vector<bool> used(beatsMs.size(), false);
    std::vector<double> delays;
    for (const double press : pressesMs) {
        std::size_t nearest = 0;
        for (std::size_t i = 1; i < beatsMs.size(); i++)
            if (std::fabs(press - beatsMs[i]) < std::fabs(press - beatsMs[nearest]))
                nearest = i;
        const double delay = press - beatsMs[nearest];
        if (static_cast<int>(nearest) < practice || used[nearest] || std::fabs(delay) > interval / 2)
            continue;
        used[nearest] = true;
        delays.push_back(delay);
    }
    if (delays.empty())
        return result;
    const double centre = median(delays);
    const double limit = 3 * std::max(5.0, medianDeviation(delays, centre));
    std::vector<double> kept;
    for (const double d : delays)
        if (std::fabs(d - centre) <= limit)
            kept.push_back(d);
    const double latency = median(kept);
    result.latencyMs = static_cast<int>(std::lround(latency));
    result.spreadMs = static_cast<int>(std::lround(medianDeviation(kept, latency)));
    result.counted = static_cast<int>(kept.size());
    result.enough = result.counted >= kCalibrationMinCounted;
    return result;
}

std::vector<KaraokeTap::Syllable> KaraokeTap::targets(const AudioKaraoke &karaoke)
{
    std::vector<Syllable> out;
    std::u16string leading;
    for (int i = 0; i < karaoke.count(); i++) {
        const auto text = withoutEmptyBlocks(karaoke.syllables()[static_cast<std::size_t>(i)]);
        const auto &tag = karaoke.tags()[static_cast<std::size_t>(i)];
        const auto stripped = karaoke.stripped(i);
        if (stripped.empty() && text.empty())
            continue;
        if (blank(stripped)) {
            if (out.empty())
                leading += text;
            else
                out.back().text += text;
            continue;
        }
        out.push_back({leading + text, tag});
        leading.clear();
    }
    if (!leading.empty() && !out.empty())
        out.back().text += leading;
    return out;
}

void KaraokeTap::begin(std::vector<Syllable> syllables, int lineStartMs)
{
    m_syllables = std::move(syllables);
    m_lineStartMs = lineStartMs;
    revert();
}

void KaraokeTap::setMode(Mode mode)
{
    if (mode != m_mode)
        m_held.reset();
    m_mode = mode;
}

void KaraokeTap::revert()
{
    m_spans.clear();
    m_held.reset();
    m_done = false;
}

int KaraokeTap::lastBoundary() const
{
    if (m_held)
        return *m_held;
    return m_spans.empty() ? 0 : m_spans.back().endMs;
}

void KaraokeTap::startNext(int songMs)
{
    if (!m_spans.empty() && songMs - m_spans.back().endMs < kGapMergeMs)
        m_spans.back().endMs = songMs;
    m_held = songMs;
}

void KaraokeTap::finishHeld(int songMs)
{
    const int start = *m_held;
    m_spans.push_back({start, std::max(songMs, start + kMinSyllableMs)});
    m_held.reset();
    if (tapped() == count())
        m_done = true;
}

KaraokeTap::Event KaraokeTap::press(int key, int songMs)
{
    if (m_done || m_syllables.empty())
        return Event::None;
    songMs = std::max(songMs, lastBoundary());
    if (m_held) {
        if (m_mode == Mode::Hold && key == m_heldKey)
            return Event::None;
        finishHeld(songMs);
        if (m_done)
            return Event::LineDone;
    }
    startNext(songMs);
    m_heldKey = key;
    return Event::Started;
}

KaraokeTap::Event KaraokeTap::release(int key, int songMs)
{
    if (m_mode != Mode::Hold || !m_held || key != m_heldKey)
        return Event::None;
    finishHeld(std::max(songMs, *m_held));
    return m_done ? Event::LineDone : Event::Ended;
}

int KaraokeTap::undo()
{
    if (m_done)
        return std::max(0, m_spans.back().startMs - kRewindMs);
    std::optional<int> from;
    if (m_held) {
        from = *m_held;
        m_held.reset();
    }
    if (!m_spans.empty()) {
        from = m_spans.back().startMs;
        m_spans.pop_back();
    }
    if (!from)
        return std::max(0, m_lineStartMs - kRewindMs);
    return std::max(0, *from - kRewindMs);
}

int KaraokeTap::undoLine()
{
    int from = m_lineStartMs;
    if (!m_spans.empty())
        from = std::min(from, m_spans.front().startMs);
    else if (m_held)
        from = std::min(from, *m_held);
    revert();
    return std::max(0, from - kRewindMs);
}

void KaraokeTap::stop()
{
    m_held.reset();
}

KaraokeTap::Event KaraokeTap::finish(int songMs)
{
    if (!m_held)
        return Event::None;
    finishHeld(std::max(songMs, *m_held));
    return m_done ? Event::LineDone : Event::Ended;
}

int KaraokeTap::resumeMs() const
{
    if (m_spans.empty())
        return std::max(0, m_lineStartMs - kPrerollMs);
    return std::max(0, m_spans.back().endMs - kRewindMs);
}

std::vector<KaraokeTap::SyllableState> KaraokeTap::states() const
{
    std::vector<SyllableState> out;
    for (int i = 0; i < count(); i++) {
        if (i < tapped())
            out.push_back(SyllableState::Tapped);
        else if (i == tapped())
            out.push_back(m_held ? SyllableState::Held : SyllableState::Next);
        else
            out.push_back(SyllableState::Waiting);
    }
    return out;
}

std::vector<KaraokeTap::Span> KaraokeTap::spans(int nowMs) const
{
    auto out = m_spans;
    if (m_held)
        out.push_back({*m_held, std::max(nowMs, *m_held)});
    return out;
}

KaraokeTap::Result KaraokeTap::result() const
{
    Result r;
    if (m_spans.empty())
        return r;
    r.startMs = m_spans.front().startMs;
    r.endMs = m_spans.back().endMs;
    for (std::size_t i = 0; i < m_spans.size(); i++) {
        if (i > 0 && m_spans[i].startMs > m_spans[i - 1].endMs)
            r.syllables.push_back({u"", m_syllables[i - 1].tag, m_spans[i].startMs});
        r.syllables.push_back({m_syllables[i].text, m_syllables[i].tag, m_spans[i].endMs});
    }
    return r;
}

}
