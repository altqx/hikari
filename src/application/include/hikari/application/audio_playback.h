#pragma once

#include "hikari/application/audio_display.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace hikari::application {

// Cubic up to 50 (100%), then linear to 150% at 100.
float playbackVolumeFromSlider(int position);

// Each sample times `volume`, + 0.5, truncated and clipped; no-op at exactly 1.0.
void applyLegacyVolume(std::int16_t *samples, std::int64_t count, double volume);

// Frames of the box's audio at its own rate. Owner thread only.
class AudioPlayerPort {
public:
    virtual ~AudioPlayerPort() = default;
    // Then silence until stop().
    virtual void play(std::int64_t start, std::int64_t count) = 0;
    virtual void stop() = 0;
    virtual bool playing() const = 0;
    // 0 while not playing, the start until the output reports, past the end
    // in the silence after it.
    virtual std::int64_t position() const = 0;
    virtual std::int64_t startPosition() const = 0;
    virtual std::int64_t endPosition() const = 0;
    // Stops when the player has already read past the new end.
    virtual void setEndPosition(std::int64_t end) = 0;
    virtual void setVolume(double volume) = 0;
    // 0.25 to 1, pitch kept; positions stay in the audio's own frames.
    virtual void setSpeed(double speed) { (void)speed; }
    // Plays `clip` instead of the box's audio, at speed 1; false when nothing plays.
    virtual bool playClip(std::shared_ptr<const DisplayAudio> clip)
    {
        (void)clip;
        return false;
    }
    // The box's audio closed or was replaced.
    virtual void close() {}
};

enum class PlayMode {
    Selection,
    Line,
    Before500,
    After500,
    First500,
    Last500,
    BeforeMark,
    AfterMark,
    ToEnd,
};

// In ms; an end of -1 plays to the end. nullopt for a mark play without a mark.
struct PlayRequest {
    int startMs = 0, endMs = 0;
    bool operator==(const PlayRequest &) const = default;
};
std::optional<PlayRequest> legacyPlayRequest(PlayMode mode, int selectionStartMs, int selectionEndMs,
                                             std::optional<int> markMs, int markPlayTimeMs);

// ms * rate / 1000, truncated to int; an end of -1 is the last frame, which
// is never played.
struct PlayRange {
    std::int64_t start = 0, count = 0;
    bool operator==(const PlayRange &) const = default;
};
PlayRange legacyPlayRange(int sampleRate, std::int64_t sampleCount, int startMs, int endMs);

class AudioPlayback {
public:
    static constexpr std::int64_t kStopAfterEnd = 8192; // frames past the end before the player stops
    static constexpr int kEdge = 50;                    // columns the cursor keeps inside the view

    explicit AudioPlayback(AudioPlayerPort &player) : m_player(player) {}

    // Remembers the end and starts the timer; answers the range handed over.
    PlayRange play(int sampleRate, std::int64_t sampleCount, int startMs, int endMs);
    // While playing, remembers the position and stops; otherwise replays from
    // it to the last end (0 to 5000 ms before any play).
    std::optional<PlayRange> stop(int sampleRate, std::int64_t sampleCount);

    bool playing() const { return m_player.playing(); }
    // The last play had a negative end.
    bool playingToEnd() const { return m_toEnd; }
    int lastPositionMs() const { return m_lastPositionMs; }
    int lastEndMs() const { return m_lastEndMs; }
    bool timerRunning() const { return m_timer; }

    // Every 16 ms while the timer runs. The view scrolls when the cursor
    // comes within kEdge columns of an edge.
    enum class Redraw { None, Cursor, Image };
    Redraw tick(AudioView &view, int scrollbarThickness);
    bool cursorPainted() const { return m_paint; }
    float cursorX() const { return m_x; }
    void hideCursor() { m_paint = false; }

private:
    AudioPlayerPort &m_player;
    bool m_toEnd = false;
    int m_lastPositionMs = 0;
    int m_lastEndMs = 5000;
    bool m_timer = false;
    bool m_paint = false;
    float m_x = -1;
};

// Unlike tick(), a view that cannot move is not redrawn, and the cursor shows
// anywhere in view.
struct FollowedCursor {
    bool moved = false;     // the image is drawn again
    std::optional<float> x; // nullopt out of view
    bool operator==(const FollowedCursor &) const = default;
};
FollowedCursor followCursor(AudioView &view, std::int64_t sample, int scrollbarThickness);

} // namespace hikari::application
