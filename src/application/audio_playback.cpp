#include "hikari/application/audio_playback.h"

#include <algorithm>

namespace hikari::application {

// Legacy PlaybackVolumeFromSlider (MAX_PLAYBACK_VOLUME 1.5).
float playbackVolumeFromSlider(int position)
{
    if (position <= 50)
        return audioScaleFromSlider(position);
    return 1.0f + ((1.5f - 1.0f) * (float(position - 50) / 50.0f));
}

// Legacy ApplyVolume.
void applyLegacyVolume(std::int16_t *samples, std::int64_t count, double volume)
{
    if (volume == 1.0)
        return;
    for (std::int64_t i = 0; i < count; i++) {
        int value = static_cast<int>(samples[i] * volume + 0.5);
        if (value < -0x8000)
            value = -0x8000;
        if (value > 0x7FFF)
            value = 0x7FFF;
        samples[i] = static_cast<std::int16_t>(value);
    }
}

// Legacy AudioBox::OnPlaySelection, OnPlay500Before/After/First/Last,
// OnPlayBeforeMark/AfterMark and OnPlayToEnd.
std::optional<PlayRequest> legacyPlayRequest(PlayMode mode, int start, int end, std::optional<int> mark, int markPlayTime)
{
    switch (mode) {
    case PlayMode::Selection:
    case PlayMode::Line:
        return PlayRequest{start, end};
    case PlayMode::Before500:
        return PlayRequest{start - 500, start};
    case PlayMode::After500:
        return PlayRequest{end, end + 500};
    case PlayMode::First500: {
        int endp = start + 500;
        if (endp > end)
            endp = end;
        return PlayRequest{start, endp};
    }
    case PlayMode::Last500: {
        int startp = end - 500;
        if (startp < start)
            startp = start;
        return PlayRequest{startp, end};
    }
    case PlayMode::BeforeMark:
        if (!mark)
            return std::nullopt;
        return PlayRequest{*mark - markPlayTime, *mark};
    case PlayMode::AfterMark:
        if (!mark)
            return std::nullopt;
        return PlayRequest{*mark, *mark + markPlayTime};
    case PlayMode::ToEnd:
        return PlayRequest{start, -1};
    }
    return std::nullopt;
}

// Legacy AudioDisplay::Play: start and end are ints, GetSampleAtMS works in
// long long.
PlayRange legacyPlayRange(int sampleRate, std::int64_t numSamples, int startMs, int endMs)
{
    int start = static_cast<int>(static_cast<std::int64_t>(startMs) * sampleRate / 1000);
    int end = endMs != -1 ? static_cast<int>(static_cast<std::int64_t>(endMs) * sampleRate / 1000)
                          : static_cast<int>(numSamples - 1);
    if (start < 0)
        start = 0;
    if (start >= numSamples)
        start = static_cast<int>(numSamples - 1);
    if (end >= numSamples)
        end = static_cast<int>(numSamples - 1);
    if (end < start)
        end = start;
    return {start, static_cast<std::int64_t>(end) - start};
}

PlayRange AudioPlayback::play(int sampleRate, std::int64_t sampleCount, int startMs, int endMs)
{
    m_toEnd = endMs < 0;
    m_lastEndMs = endMs;
    const PlayRange range = legacyPlayRange(sampleRate, sampleCount, startMs, endMs);
    m_player.play(range.start, range.count);
    m_timer = true;
    return range;
}

std::optional<PlayRange> AudioPlayback::stop(int sampleRate, std::int64_t sampleCount)
{
    if (!m_player.playing())
        return play(sampleRate, sampleCount, m_lastPositionMs, m_lastEndMs);
    // legacy GetMSAtSample
    m_lastPositionMs = sampleRate > 0 ? static_cast<int>(m_player.position() * 1000 / sampleRate) : 0;
    m_player.stop();
    m_timer = false;
    m_paint = false;
    return std::nullopt;
}

// Legacy AudioDisplay::UpdateTimer (its centre lock is always off).
AudioPlayback::Redraw AudioPlayback::tick(AudioView &view, int scrollbarThickness)
{
    if (!m_timer)
        return Redraw::None;
    m_x = -1;
    if (!m_player.playing()) {
        m_paint = false;
        return Redraw::None;
    }
    m_paint = true;
    const std::int64_t position = m_player.position();
    if (position > m_player.startPosition() && position < m_player.endPosition()) {
        const int posX = static_cast<int>(view.xAtSample(position));
        if (posX < kEdge || posX > view.width() - kEdge) {
            const int goTo = static_cast<int>(std::max<std::int64_t>(0, position - kEdge * view.samples()));
            view.updatePosition(goTo, true, scrollbarThickness);
            return Redraw::Image; // drawn with the cursor still at -1
        }
        m_x = view.xAtSample(position);
        if (!(m_x >= 0.f && m_x < view.width()))
            m_paint = false;
        return Redraw::Cursor;
    }
    m_paint = false;
    if (position > m_player.endPosition() + kStopAfterEnd) {
        m_player.stop();
        m_timer = false;
    }
    return Redraw::Cursor;
}

} // namespace hikari::application
