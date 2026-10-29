#pragma once

// Shape coordinates: pixel centres at integers, lines centred on their
// endpoints. The waveform area is [0, width) x [0, height); the time ruler
// takes the `timelineHeight` rows below it.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// The lowest and highest sample of each block of audio.
class WaveformPeaks {
public:
    explicit WaveformPeaks(int blockSamples) : m_block(blockSamples) {}

    void append(const std::int16_t *samples, std::int64_t count);
    // lo and hi over every block the range touches; false when it starts past the end
    bool range(std::int64_t first, std::int64_t count, std::int16_t *lo, std::int16_t *hi) const;
    std::int64_t samples() const { return m_samples; }
    int blockSamples() const { return m_block; }

private:
    int m_block;
    std::int64_t m_samples = 0;
    std::vector<std::int16_t> m_lo, m_hi;
};

// Interleaved 16-bit frames of one or two channels.
class AudioStore {
public:
    virtual ~AudioStore() = default;
    // False when they cannot be kept (out of memory, a failed write).
    virtual bool append(const std::int16_t *interleaved, std::int64_t frames) = 0;
    // [start, start + count) must lie within what was appended.
    virtual void read(std::int64_t start, std::int64_t count, std::int16_t *out) const = 0;
    virtual std::int64_t frames() const = 0;
    virtual int channels() const = 0;
    // Called once every frame is in.
    virtual void complete() {}
};

std::unique_ptr<AudioStore> ramAudioStore(int channels);
// Writes `path`.part, renamed to `path` when dropped complete, else removed.
// Null with `error` set when it cannot be created.
std::unique_ptr<AudioStore> diskAudioStore(const std::filesystem::path &path, int channels, std::string *error);
// A complete cache file read as is, never renamed or removed; its size gives
// the frames and reads past it are silence. Null when it cannot be opened.
std::unique_ptr<AudioStore> cachedAudioStore(const std::filesystem::path &path, int channels);

// One 16-bit channel at the source rate, the mean of the cached channels.
// The cached frames may be read from another thread (the store is locked per
// call); the peak table is the owner thread's alone.
class DisplayAudio {
public:
    static constexpr int kPeakBlock = 256;

    // Null `store`: in RAM, with the channels of the first append.
    DisplayAudio(int sampleRate, std::int64_t sampleCount, std::unique_ptr<AudioStore> store = nullptr);
    static DisplayAudio silence(int sampleRate, std::int64_t sampleCount);

    // False when the store cannot keep them.
    bool appendFrames(const std::int16_t *interleaved, std::int64_t frames, int channels);
    bool appendSilence(std::int64_t frames, int channels);
    // For a store already holding the audio: the peak table gets its next
    // `frames` frames; false once every frame is in.
    bool scanStored(std::int64_t frames);
    void finish();

    int sampleRate() const { return m_rate; }
    std::int64_t sampleCount() const { return m_count; }
    std::int64_t decoded() const;
    bool finished() const { return m_finished; }
    // Zero past what is decoded or past the end.
    void read(std::int64_t start, std::int64_t count, std::int16_t *out) const;
    const WaveformPeaks *peaks() const { return m_finished && !m_silence ? &m_peaks : nullptr; }
    // 1 for blank audio and before the first frames.
    int channels() const;
    // Interleaved; zero past the end and past what is decoded.
    void readFrames(std::int64_t start, std::int64_t count, std::int16_t *out) const;

private:
    int m_rate = 0;
    std::int64_t m_count = 0;
    bool m_silence = false;
    bool m_finished = false;
    std::unique_ptr<AudioStore> m_store;
    mutable std::vector<std::int16_t> m_frames;
    mutable std::unique_ptr<std::mutex> m_lock = std::make_unique<std::mutex>(); // guards m_store and m_frames
    std::int64_t m_scanned = 0;
    WaveformPeaks m_peaks{kPeakBlock};
};

// Each column's topmost and lowest waveform row: `w` columns of `samples`
// samples from `start`, in an area `h` rows high.
struct WaveformColumns {
    std::vector<int> min, peak;
};
WaveformColumns legacyWaveform(const DisplayAudio &audio, std::int64_t start, int w, int h, int samples, float scale);

// Colours as 0xAARRGGBB.
struct AudioDisplayOptions {
    int lineBoundaryWidth = 2;
    int inactiveLines = 1;       // 0 none, 1 previous and next, 2 all
    bool drawVideoPosition = true;
    bool drawSelectionBackground = true;
    bool drawSecondBoundaries = true;
    bool drawKeyframes = true;
    bool drawTimeCursor = true;
    bool autoScroll = true;
    bool grabTimesOnSelect = true;
    int horizontalZoom = 50;             // samples percent
    int verticalZoom = 50;               // slider position, see audioScaleFromSlider

    std::uint32_t background = 0xFF36393E;
    std::uint32_t lineStart = 0xFF940000;
    std::uint32_t lineEnd = 0xFF940000;
    std::uint32_t inactiveBoundary = 0xFF00D77D;
    std::uint32_t cursor = 0xFF8791FD;
    std::uint32_t secondBoundaries = 0x37F4F4F4;
    std::uint32_t keyframe = 0xFFF4F4F4;
    std::uint32_t selectionBackground = 0x37FFFFFF;
    std::uint32_t selectionModified = 0x37FFFFFF;
    std::uint32_t inactiveBackground = 0x55000000;
    std::uint32_t waveform = 0xFF202225;
    std::uint32_t waveformInactive = 0xFF2F3136;
    std::uint32_t waveformModified = 0xFFFF6968;
    std::uint32_t waveformSelected = 0xFFDCDAFF;
    std::uint32_t timescaleBackground = 0xFF202225;
    std::uint32_t timescaleText = 0xFFAEAFB2;
    std::uint32_t spectrumBackground = 0xFF000000;
    std::uint32_t spectrumEcho = 0xFF674FD7;
    std::uint32_t spectrumInner = 0xFFF4F4F4;
    std::uint32_t lineBoundaryMark = 0xFFFFFFFF;
    std::uint32_t syllableBoundaries = 0xFF202225;
    std::uint32_t syllableText = 0xFF8791FD;
};

// Cubic; 50 is 100%.
float audioScaleFromSlider(int position);

class AudioView {
public:
    static constexpr int kZoomWidth = 500; // zoom does not follow the window width

    AudioView() = default;

    void setSource(int sampleRate, std::int64_t sampleCount);
    void clearSource() { m_rate = 0; m_count = 0; m_hasSource = false; }
    bool hasSource() const { return m_hasSource; }

    // The client size; the time ruler comes off the height.
    void resize(int clientWidth, int clientHeight, int timelineHeight, int scrollbarThickness);
    void setSamplesPercent(int percent, bool update, float pivot = 0.5f, int scrollbarThickness = 0);
    void setScale(float scale) { m_scale = scale; }
    void updateSamples();
    void updatePosition(std::int64_t pos, bool isSample, int scrollbarThickness);
    void setPosition(int pos); // from the scrollbar
    void makeVisible(int startMs, int endMs, bool force, bool moveToEnd, int scrollbarThickness);
    // Centres the syllable when its start is within 50 columns of the left
    // edge or its end within 100 of the right.
    void makeVisibleKaraoke(int startMs, int endMs, int scrollbarThickness);

    struct Scrollbar {
        int position = 0, page = 0, range = 0;
    };
    // Can also move the view to the start.
    Scrollbar updateScrollbar(int scrollbarThickness);
    Scrollbar scrollbar() const { return m_bar; } // as last updated

    float xAtMs(std::int64_t ms) const;
    int msAtX(std::int64_t x) const;
    std::int64_t sampleAtMs(std::int64_t ms) const;
    float xAtSample(std::int64_t sample) const;
    std::int64_t sampleAtX(int x) const;

    int width() const { return m_w; }
    int height() const { return m_h; }
    int timelineHeight() const { return m_timeline; }
    int samples() const { return m_samples; }
    int samplesPercent() const { return m_samplesPercent; }
    float scale() const { return m_scale; }
    std::int64_t position() const { return m_position; }
    std::int64_t positionSample() const { return m_positionSample; }
    int sampleRate() const { return m_rate; }
    std::int64_t sampleCount() const { return m_count; }

private:
    bool m_hasSource = false;
    int m_rate = 0;
    std::int64_t m_count = 0;
    int m_w = 0, m_h = 0, m_timeline = 0;
    int m_samples = 0;
    int m_samplesPercent = 100;
    float m_scale = 1.0f;
    // MUST KILL: two view positions kept separately that can disagree
    std::int64_t m_position = 0, m_positionSample = 0;
    Scrollbar m_bar;
};

struct AudioKaraokeMarks;
// Opaque BGRA, row 0 at the top.
struct AudioImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
};

struct AudioShape {
    enum class Kind { Fill, Line, Triangle, Text, Image };
    enum class Font { Scale, Cursor, Label };
    enum class Align { TopLeft, TopCenter, Center };

    Kind kind = Kind::Fill;
    std::uint32_t colour = 0;
    // Fill: [x1, x2) x [y1, y2). Line: (x1, y1) to (x2, y2). Triangle: the
    // three points. Text: the rect (x1, y1, x2, y2) it is aligned in.
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
    float width = 1;     // Line
    bool smooth = false; // Line: antialiased (the cursor)
    std::string text;
    Font font = Font::Scale;
    Align align = Align::TopLeft;
    bool outlined = false; // a black outline one pixel around
    // Image: drawn pixel for pixel with its top left corner at (x1, y1).
    std::shared_ptr<const AudioImage> image;
};

using AudioTextWidth = std::function<int(AudioShape::Font, std::string_view)>;

struct AudioMarks {
    int startMs = 0, endMs = 0; // the active Line's selection
    bool modified = false;      // not committed yet
    // From legacyInactiveLines.
    std::vector<std::pair<int, int>> inactive;
    std::vector<int> keyframesMs;
    std::optional<int> videoMs;   // the paused video's time
    bool focused = false;
    std::optional<int> markMs;
    int markTextHeight = 0; // the label font's; places the mark's time and the syllables
    std::shared_ptr<const AudioKaraokeMarks> karaoke;
};

// Everything but the cursor. `columns` come from legacyWaveform over the
// view; a `spectrum` is drawn in place of the waveforms.
std::vector<AudioShape> audioScene(const AudioView &view, const WaveformColumns &columns, const AudioMarks &marks,
                                   const AudioDisplayOptions &options, const AudioTextWidth &textWidth,
                                   std::shared_ptr<const AudioImage> spectrum = nullptr);
// `progress` 0..1.
std::vector<AudioShape> audioProgressScene(const AudioView &view, float progress, const AudioDisplayOptions &options);
// The mouse cursor's line and, when not playing, its time unless
// drawTimeCursor is off.
std::vector<AudioShape> audioCursor(const AudioView &view, float x, bool playing, const AudioDisplayOptions &options,
                                    bool karaoke = false);

// `dash` pixels on and off.
std::vector<std::pair<float, float>> legacyDashes(float from, float to, int dash);
// ASS "H:MM:SS.cc", negative times as 0.
std::string legacyAssTime(int ms);

// A Line as the inactive-line shading sees it.
struct AudioLineSpan {
    int startMs = 0, endMs = 0;
    bool visible = true; // shown in the Grid
    bool operator==(const AudioLineSpan &) const = default;
};
int legacyKeyFromPosition(std::span<const AudioLineSpan> lines, int position, int delta);
// Mode 1: the previous and next shown Lines (and hidden ones between);
// mode 2: all. Never the active Line.
std::vector<std::pair<int, int>> legacyInactiveLines(int mode, std::span<const AudioLineSpan> lines, int active);

// The selection a newly active Line gives. A 0:00:00.00 to 0:00:00.00 Line
// starts at the previous shown Line's end when it follows the previously
// active one, else at 0, and lasts 5 s.
std::pair<int, int> legacyLineSelection(std::span<const AudioLineSpan> lines, int active, int previousActive);

}
