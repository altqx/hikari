#pragma once

// KA2 (#277) tap-to-time karaoke timing (KA2-tap-to-time). Qt-free: the tap
// state machine over one Line's syllables, the song time a key stands for,
// and the latency calibration's maths. While the audio plays, Hold mode
// starts the next syllable at key-down and ends it at key-up, so the time
// between a release and the next press becomes a gap (an empty syllable);
// Press mode starts the next syllable at each press and ends the one before,
// with no gaps. The first press sets the Line's start and the end of its last
// syllable the Line's end. Times are song times in ms (the audio's own, not
// the wall clock's, so slowed playback changes nothing here).

#include "hikari/application/audio_karaoke.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hikari::application {

// The song time a key stands for: the playback position at the key event
// less the calibrated latency. The latency is wall-clock time, so at a slowed
// `speed` it is that much less of the song. Rounded to the nearest 10 ms, not
// before 0.
int tapSongMs(double positionMs, int latencyMs, double speed);

// Latency calibration: beeps every kCalibrationIntervalMs, the first
// kCalibrationPractice of them not counted. Each press counts for the beep
// nearest it (within half the interval, one press per beep). The latency is
// the median of the counted delays left after dropping those further than
// three times their median absolute deviation (at least 5 ms) from their
// median; the spread is the median absolute deviation of what is left.
inline constexpr int kCalibrationBeats = 16;
inline constexpr int kCalibrationPractice = 4;
inline constexpr int kCalibrationIntervalMs = 600; // 100 BPM
inline constexpr int kCalibrationMinCounted = 6;

struct LatencyCalibration {
    int latencyMs = 0;
    int spreadMs = 0;
    int counted = 0;     // presses left after the outliers
    bool enough = false; // at least kCalibrationMinCounted of them
    bool operator==(const LatencyCalibration &) const = default;
};
LatencyCalibration calibrateLatency(std::span<const double> beatsMs, std::span<const double> pressesMs,
                                    int practice = kCalibrationPractice);

class KaraokeTap {
public:
    enum class Mode { Hold, Press };
    // A syllable to tap: its text and tag as the karaoke model holds them.
    struct Syllable {
        std::u16string text, tag;
        bool operator==(const Syllable &) const = default;
    };
    // The tapped Line: its new times and syllables (gaps with empty text).
    struct Timed {
        std::u16string text, tag;
        int endMs = 0;
        bool operator==(const Timed &) const = default;
    };
    struct Result {
        int startMs = 0, endMs = 0;
        std::vector<Timed> syllables;
        bool operator==(const Result &) const = default;
    };
    // A tapped syllable's span; the held one ends where the playback is.
    struct Span {
        int startMs = 0, endMs = 0;
        bool operator==(const Span &) const = default;
    };
    enum class SyllableState { Tapped, Held, Next, Waiting };
    enum class Event { None, Started, Ended, LineDone };

    static constexpr int kGapMergeMs = 30; // a shorter gap joins the syllable before it
    static constexpr int kRewindMs = 2000; // undo and Resume play from this far before
    static constexpr int kPrerollMs = 1000; // a Line plays from this far before its start
    static constexpr int kMinSyllableMs = 10;

    // The syllables to tap from the karaoke model: those with something to
    // sing. Empty ones (gaps an earlier tap made) are dropped, and a blank
    // one's text joins the syllable before it (the first one's the next).
    static std::vector<Syllable> targets(const AudioKaraoke &karaoke);

    // A Line to tap, starting at `lineStartMs` before any tap.
    void begin(std::vector<Syllable> syllables, int lineStartMs);
    void setMode(Mode mode) { m_mode = mode; }
    Mode mode() const { return m_mode; }

    // Key events at song times (tapSongMs). A tap before the last boundary
    // kept lands on it, so a rewound replay never runs time backwards.
    // `key` tells a release from another key's; the caller drops a key's
    // auto-repeat.
    Event press(int key, int songMs);
    Event release(int key, int songMs);
    // Backspace: the held syllable and the last tapped one before it are taken
    // back; answers the song time to play again from (kRewindMs before the
    // earliest taken back, or before the Line when nothing was tapped).
    int undo();
    // Shift+Backspace: every tap on the Line; answers where to play from.
    int undoLine();
    // Esc: a held syllable is dropped, the tapped ones kept.
    void stop();
    // Where Resume plays from: kRewindMs before the end of the last tapped
    // syllable, or kPrerollMs before the Line.
    int resumeMs() const;
    // Revert Line: no taps.
    void revert();

    int count() const { return static_cast<int>(m_syllables.size()); }
    int tapped() const { return static_cast<int>(m_spans.size()); }
    bool holding() const { return m_held.has_value(); }
    bool done() const { return m_done; }
    // The Line needs splitting first: with one syllable a tap would only set
    // its start and end.
    bool needsSplit() const { return m_syllables.size() < 2; }
    const std::vector<Syllable> &syllables() const { return m_syllables; }
    std::vector<SyllableState> states() const;
    // The tapped spans, and the held one up to `nowMs`.
    std::vector<Span> spans(int nowMs) const;
    // The finished Line (done()).
    Result result() const;

private:
    void startNext(int songMs);
    void finishHeld(int songMs);
    int lastBoundary() const;

    Mode m_mode = Mode::Hold;
    std::vector<Syllable> m_syllables;
    std::vector<Span> m_spans;
    std::optional<int> m_held; // the held syllable's start
    int m_heldKey = 0;
    int m_lineStartMs = 0;
    bool m_done = false;
};

} // namespace hikari::application
