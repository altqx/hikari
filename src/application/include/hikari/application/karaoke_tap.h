#pragma once

// Times are song times in ms, not wall-clock ones.

#include "hikari/application/audio_karaoke.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hikari::application {

// `positionMs` less the wall-clock latency scaled by `speed`, rounded to the
// nearest 10 ms, not below 0.
int tapSongMs(double positionMs, int latencyMs, double speed);

// Latency: median press delay after dropping outliers past 3 MADs (at least
// 5 ms); spread: the MAD of the rest.
inline constexpr int kCalibrationBeats = 16;
inline constexpr int kCalibrationPractice = 4;
inline constexpr int kCalibrationIntervalMs = 600;
inline constexpr int kCalibrationMinCounted = 6;

struct LatencyCalibration {
    int latencyMs = 0;
    int spreadMs = 0;
    int counted = 0;     // presses left after the outliers
    bool enough = false;
    bool operator==(const LatencyCalibration &) const = default;
};
LatencyCalibration calibrateLatency(std::span<const double> beatsMs, std::span<const double> pressesMs,
                                    int practice = kCalibrationPractice);

class KaraokeTap {
public:
    // Hold: key-down starts a syllable and key-up ends it, leaving gaps;
    // Press: each press starts the next and ends the one before.
    enum class Mode { Hold, Press };
    struct Syllable {
        std::u16string text, tag;
        bool operator==(const Syllable &) const = default;
    };
    // A gap has empty text.
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

    // Empty syllables are dropped; a blank one's text joins the one before
    // (the first one's the next).
    static std::vector<Syllable> targets(const AudioKaraoke &karaoke);

    void begin(std::vector<Syllable> syllables, int lineStartMs);
    // Drops a held syllable.
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    // A tap before lastBoundary() lands on it. The caller drops auto-repeat.
    Event press(int key, int songMs);
    Event release(int key, int songMs);
    // Takes back the held syllable and the last tapped one; answers the song
    // time to play again from.
    int undo();
    // Takes back every tap; answers where to play from.
    int undoLine();
    // Drops a held syllable, keeps the tapped ones.
    void stop();
    // Playback ended: a held syllable ends at `songMs`.
    Event finish(int songMs);
    // kRewindMs before the last tapped end, or kPrerollMs before the Line.
    int resumeMs() const;
    void revert();

    int count() const { return static_cast<int>(m_syllables.size()); }
    int tapped() const { return static_cast<int>(m_spans.size()); }
    bool holding() const { return m_held.has_value(); }
    bool done() const { return m_done; }
    bool needsSplit() const { return m_syllables.size() < 2; }
    const std::vector<Syllable> &syllables() const { return m_syllables; }
    std::vector<SyllableState> states() const;
    // The tapped spans, and the held one up to `nowMs`.
    std::vector<Span> spans(int nowMs) const;
    // Valid once done().
    Result result() const;
    // The held syllable's start, else the last tapped end; 0 before any.
    int lastBoundary() const;

private:
    void startNext(int songMs);
    void finishHeld(int songMs);

    Mode m_mode = Mode::Hold;
    std::vector<Syllable> m_syllables;
    std::vector<Span> m_spans;
    std::optional<int> m_held; // the held syllable's start
    int m_heldKey = 0;
    int m_lineStartMs = 0;
    bool m_done = false;
};

} // namespace hikari::application
