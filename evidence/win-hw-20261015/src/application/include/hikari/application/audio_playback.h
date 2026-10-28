#pragma once

// The audio box's playback (A4; legacy AudioBox's play handlers and
// AudioDisplay::Play, Stop and UpdateTimer at 20d647c4). Qt-free: the range
// each play command asks for, the source frames that range hands the player,
// Stop's replay of the last range, and the playback cursor the display's
// timer draws, scrolling the view to keep it in sight. The player itself
// (legacy DirectSoundPlayer2) is a port: a backend plays the box's cached
// audio through the editor output.

#include "hikari/application/audio_display.h"

#include <cstdint>
#include <optional>

namespace hikari::application {

// Legacy PlaybackVolumeFromSlider (AUDIO_VOLUME): the display's cubic curve
// up to 50 (100%), then a straight ramp to 150% at 100.
float playbackVolumeFromSlider(int position);

// Legacy ProviderFFMS2::ApplyVolume on the cached 16-bit samples: each
// sample times the volume, + 0.5 and truncated, clipped to 16 bits; nothing
// at exactly 1.0.
void applyLegacyVolume(std::int16_t *samples, std::int64_t count, double volume);

// The player legacy AudioDisplay drove (DirectSoundPlayer2): frames of the
// box's audio at its own rate. Its owner thread only.
class AudioPlayerPort {
public:
    virtual ~AudioPlayerPort() = default;
    // Frames [start, start + count), then silence until stop() (legacy keeps
    // its buffer looping on silence past the end).
    virtual void play(std::int64_t start, std::int64_t count) = 0;
    virtual void stop() = 0;
    virtual bool playing() const = 0;
    // The frame the output is at (legacy GetCurrentPosition): 0 while not
    // playing, the start until the output reports, past the end in the
    // silence after it.
    virtual std::int64_t position() const = 0;
    virtual std::int64_t startPosition() const = 0;
    virtual std::int64_t endPosition() const = 0;
    // Legacy SetEndPosition (a boundary dragged while playing, A3): the
    // player stops when it has already read past the new end.
    virtual void setEndPosition(std::int64_t end) = 0;
    virtual void setVolume(double volume) = 0;
    // Legacy CloseStream: the box's audio closed or was replaced.
    virtual void close() {}
};

// The audio box's play commands.
enum class PlayMode {
    Selection,  // AUDIO_PLAY / AUDIO_PLAY_ALT and the middle double click
    Line,       // AUDIO_PLAY_LINE / AUDIO_PLAY_LINE_ALT
    Before500,  // AUDIO_PLAY_500MS_BEFORE
    After500,   // AUDIO_PLAY_500MS_AFTER
    First500,   // AUDIO_PLAY_500MS_FIRST
    Last500,    // AUDIO_PLAY_500MS_LAST
    BeforeMark, // AUDIO_PLAY_BEFORE_MARK
    AfterMark,  // AUDIO_PLAY_AFTER_MARK
    ToEnd,      // AUDIO_PLAY_TO_END
};

// What legacy's AudioBox handler for `mode` passes to AudioDisplay::Play,
// in ms, from the selection (GetTimesSelection: curStartMS, curEndMS) and
// the ruler's mark; -1 as the end plays to the end. Nothing for a mark play
// without a mark. Karaoke's syllables (A5) are not part of this.
struct PlayRequest {
    int startMs = 0, endMs = 0;
    bool operator==(const PlayRequest &) const = default;
};
std::optional<PlayRequest> legacyPlayRequest(PlayMode mode, int selectionStartMs, int selectionEndMs,
                                             std::optional<int> markMs, int markPlayTimeMs);

// AudioDisplay::Play's frames: ms to samples (ms * rate / 1000, truncated,
// kept in an int), the start clamped into the audio, an end of exactly -1
// at the last frame (which is never played), the end clamped to the last
// frame and to the start; the player gets [start, end).
struct PlayRange {
    std::int64_t start = 0, count = 0;
    bool operator==(const PlayRange &) const = default;
};
PlayRange legacyPlayRange(int sampleRate, std::int64_t sampleCount, int startMs, int endMs);

// Legacy AudioDisplay's playback state over a player.
class AudioPlayback {
public:
    static constexpr std::int64_t kStopAfterEnd = 8192; // UpdateTimer stops the player this far past the end
    static constexpr int kEdge = 50;                    // UpdateTimer keeps the cursor this far inside the view

    explicit AudioPlayback(AudioPlayerPort &player) : m_player(player) {}

    // Legacy Play without the video's pause: the range for the audio's
    // rate and length handed to the player, the last end remembered and
    // the timer started. Answers the range handed over.
    PlayRange play(int sampleRate, std::int64_t sampleCount, int startMs, int endMs);
    // Legacy Stop without the video's pause: while playing, where it was
    // is remembered (in ms) and the player stops; otherwise the remembered
    // position plays to the last end again (0 to 5000 ms before any play).
    // Answers the range handed over when it played.
    std::optional<PlayRange> stop(int sampleRate, std::int64_t sampleCount);

    bool playing() const { return m_player.playing(); }
    // Legacy playingToEnd: the last Play asked for the end (a negative end).
    bool playingToEnd() const { return m_toEnd; }
    int lastPositionMs() const { return m_lastPositionMs; }
    int lastEndMs() const { return m_lastEndMs; }
    // The timer runs (legacy !stopPlayThread).
    bool timerRunning() const { return m_timer; }

    // Legacy UpdateTimer, every 16 ms while the timer runs: the cursor at the
    // player's position while it is inside the played range and the view;
    // the view moved (to 50 columns before the position) when the position
    // is within 50 columns of either edge; no cursor outside the range; the
    // player stopped once 8192 frames past the end. While the player is not
    // playing the cursor is cleared without a redraw.
    enum class Redraw { None, Cursor, Image };
    Redraw tick(AudioView &view, int scrollbarThickness);
    // Legacy cursorPaint and curpos.
    bool cursorPainted() const { return m_paint; }
    float cursorX() const { return m_x; }
    // Legacy Stop's cursorPaint = false.
    void hideCursor() { m_paint = false; }

private:
    AudioPlayerPort &m_player;
    bool m_toEnd = false;
    int m_lastPositionMs = 0; // legacy audioLastPosition
    int m_lastEndMs = 5000;   // legacy audioLastEndPosition
    bool m_timer = false;
    bool m_paint = false;
    float m_x = -1;
};

} // namespace hikari::application
