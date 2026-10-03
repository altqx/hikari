#pragma once

// The audio box's display (A1; legacy HikariSub/AudioDisplay.cpp,
// Provider.cpp and WaveformPeaks.cpp at 20d647c4). Qt-free: the samples the
// display reads, the waveform columns, the view position and zoom, and the
// shapes of one drawn image (the waveform with its time ruler and the line,
// keyframe, video and cursor marks) in legacy's own coordinates. The scene
// presenter only rasterizes the shapes.
//
// Coordinates are Direct3D 9 screen coordinates, as legacy passed them to
// the device: a pixel's centre is at integer coordinates, a line is centred
// on its endpoints and is `width` pixels wide. The waveform area is
// [0, width) x [0, height); the time ruler takes the `timelineHeight` rows
// below it. Spectrum and zoom controls (A2), timing (A3), playback (A4) and
// karaoke (A5) are not part of this module.

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// The lowest and highest sample of each block of audio, so a zoomed out
// waveform reads one entry per block instead of every sample (legacy
// WaveformPeaks).
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

// The samples the display reads: one 16-bit channel at the source rate
// (legacy Provider::GetBuffer, the mixdown of the cached audio). Decoded
// audio is appended in legacy's decode format; finish() builds nothing more
// but marks the peak table ready (legacy BuildPeaks, block 256), so zoomed
// out views read it. Blank audio (legacy ProviderDummy) is silence and never
// has a peak table.
class DisplayAudio {
public:
    static constexpr int kPeakBlock = 256;

    DisplayAudio(int sampleRate, std::int64_t sampleCount);
    static DisplayAudio silence(int sampleRate, std::int64_t sampleCount);

    // Interleaved 16-bit frames of `channels` channels, mixed down as legacy
    // ProviderFFMS2::GetBuffer does: the channels' sum divided by their count.
    void appendFrames(const std::int16_t *interleaved, std::int64_t frames, int channels);
    void finish();

    int sampleRate() const { return m_rate; }
    std::int64_t sampleCount() const { return m_count; }
    std::int64_t decoded() const { return m_silence ? m_count : static_cast<std::int64_t>(m_samples.size()); }
    bool finished() const { return m_finished; }
    // [start, start + count) into `out`; zero past the end (legacy ReadCache).
    void read(std::int64_t start, std::int64_t count, std::int16_t *out) const;
    const WaveformPeaks *peaks() const { return m_finished && !m_silence ? &m_peaks : nullptr; }

private:
    int m_rate = 0;
    std::int64_t m_count = 0;
    bool m_silence = false;
    bool m_finished = false;
    std::vector<std::int16_t> m_samples;
    WaveformPeaks m_peaks{kPeakBlock};
};

// Each column's topmost and lowest waveform row (legacy Provider::GetWaveForm
// min and peak): `w` columns of `samples` samples from `start`, in an area
// `h` rows high at vertical scale `scale`. Zoomed out (four peak blocks or
// more per column, once the peak table is ready) a column covers every whole
// block its samples touch.
struct WaveformColumns {
    std::vector<int> min, peak;
};
WaveformColumns legacyWaveform(const DisplayAudio &audio, std::int64_t start, int w, int h, int samples, float scale);

// Legacy display options (AudioDisplay::ChangeOptions) with their defaults,
// and the dark theme's colours as 0xAARRGGBB.
struct AudioDisplayOptions {
    int lineBoundaryWidth = 2;   // AUDIO_LINE_BOUNDARIES_THICKNESS
    int inactiveLines = 1;       // AUDIO_INACTIVE_LINES_DISPLAY_MODE: 0 none, 1 previous and next, 2 all
    bool drawVideoPosition = true;       // AUDIO_DRAW_VIDEO_POSITION
    bool drawSelectionBackground = true; // AUDIO_DRAW_SELECTION_BACKGROUND
    bool drawSecondBoundaries = true;    // AUDIO_DRAW_SECONDARY_LINES
    bool drawKeyframes = true;           // AUDIO_DRAW_KEYFRAMES
    bool autoScroll = true;              // AUDIO_AUTO_SCROLL
    bool grabTimesOnSelect = true;       // AUDIO_GRAB_TIMES_ON_SELECT
    int horizontalZoom = 50;             // AUDIO_HORIZONTAL_ZOOM (samples percent)
    int verticalZoom = 50;               // AUDIO_VERTICAL_ZOOM (AudioDisplayScaleFromSlider)

    std::uint32_t background = 0xFF36393E;            // AUDIO_BACKGROUND
    std::uint32_t lineStart = 0xFF940000;             // AUDIO_LINE_BOUNDARY_START
    std::uint32_t lineEnd = 0xFF940000;               // AUDIO_LINE_BOUNDARY_END
    std::uint32_t inactiveBoundary = 0xFF00D77D;      // AUDIO_LINE_BOUNDARY_INACTIVE_LINE
    std::uint32_t cursor = 0xFF8791FD;                // AUDIO_PLAY_CURSOR
    std::uint32_t secondBoundaries = 0x37F4F4F4;      // AUDIO_SECONDS_BOUNDARIES
    std::uint32_t keyframe = 0xFFF4F4F4;              // AUDIO_KEYFRAMES
    std::uint32_t selectionBackground = 0x37FFFFFF;   // AUDIO_SELECTION_BACKGROUND
    std::uint32_t selectionModified = 0x37FFFFFF;     // AUDIO_SELECTION_BACKGROUND_MODIFIED
    std::uint32_t inactiveBackground = 0x55000000;    // AUDIO_INACTIVE_LINES_BACKGROUND
    std::uint32_t waveform = 0xFF202225;              // AUDIO_WAVEFORM
    std::uint32_t waveformInactive = 0xFF2F3136;      // AUDIO_WAVEFORM_INACTIVE
    std::uint32_t waveformModified = 0xFFFF6968;      // AUDIO_WAVEFORM_MODIFIED
    std::uint32_t waveformSelected = 0xFFDCDAFF;      // AUDIO_WAVEFORM_SELECTED
    std::uint32_t timescaleBackground = 0xFF202225;   // WINDOW_BACKGROUND
    std::uint32_t timescaleText = 0xFFAEAFB2;         // WINDOW_TEXT
};

// Legacy AudioDisplayScaleFromSlider: a cubic response, 50 is 100%.
float audioScaleFromSlider(int position);

// The view: which samples one column holds and where the view starts
// (legacy AudioDisplay's w, h, samples, samplesPercent, Position and
// PositionSample, which legacy keeps separately and does not always agree).
// Positions follow legacy's integer arithmetic.
class AudioView {
public:
    static constexpr int kZoomWidth = 500; // legacy w1: zoom does not follow the window width

    // A new box (legacy AudioBox): zoom 100% until the options' zoom is set.
    AudioView() = default;

    void setSource(int sampleRate, std::int64_t sampleCount);
    void clearSource() { m_rate = 0; m_count = 0; m_hasSource = false; }
    bool hasSource() const { return m_hasSource; }

    // Legacy OnSize: the client size, less the time ruler.
    void resize(int clientWidth, int clientHeight, int timelineHeight, int scrollbarThickness);
    void setSamplesPercent(int percent, bool update, float pivot = 0.5f, int scrollbarThickness = 0);
    void setScale(float scale) { m_scale = scale; }
    void updateSamples();
    void updatePosition(std::int64_t pos, bool isSample, int scrollbarThickness);
    void setPosition(int pos); // the scrollbar (legacy SetPosition)
    // Legacy MakeDialogueVisible without karaoke, for the selection [startMs, endMs].
    void makeVisible(int startMs, int endMs, bool force, bool moveToEnd, int scrollbarThickness);

    struct Scrollbar {
        int position = 0, page = 0, range = 0;
    };
    // Legacy UpdateScrollbar: it can also move the view to the start.
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
    std::int64_t m_position = 0, m_positionSample = 0;
    Scrollbar m_bar;
};

// One shape of the drawn image, in legacy draw order.
struct AudioShape {
    enum class Kind { Fill, Line, Triangle, Text };
    enum class Font { Scale, Cursor, Label }; // legacy tahoma8, tahoma13, verdana11
    enum class Align { TopLeft, TopCenter, Center };

    Kind kind = Kind::Fill;
    std::uint32_t colour = 0;
    // Fill: [x1, x2) x [y1, y2). Line: (x1, y1) to (x2, y2). Triangle: the
    // three points. Text: the legacy RECT (x1, y1, x2, y2) it is aligned in.
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
    float width = 1;     // Line
    bool smooth = false; // Line: antialiased (the cursor)
    std::string text;
    Font font = Font::Scale;
    Align align = Align::TopLeft;
    bool outlined = false; // legacy DRAWOUTTEXT: a black outline one pixel around
};

// Text widths in a font, for the ruler's label spacing (legacy GetTextExtent).
using AudioTextWidth = std::function<int(AudioShape::Font, std::string_view)>;

// What the display marks besides the waveform.
struct AudioMarks {
    int startMs = 0, endMs = 0; // the active Line's selection (legacy curStartMS, curEndMS)
    bool modified = false;      // legacy NeedCommit (A3)
    // The Lines legacy DrawInactiveLines shades, in its order (see legacyInactiveLines).
    std::vector<std::pair<int, int>> inactive;
    std::vector<int> keyframesMs; // the video's keyframes (legacy Timebase::Keyframes)
    std::optional<int> videoMs;   // the paused video's time (legacy VideoBox::Tell)
    bool focused = false;
};

// Legacy DoUpdateImage: everything but the cursor. `columns` are the
// waveform's (legacyWaveform over the view).
std::vector<AudioShape> audioScene(const AudioView &view, const WaveformColumns &columns, const AudioMarks &marks,
                                   const AudioDisplayOptions &options, const AudioTextWidth &textWidth);
// Legacy DrawProgress while the audio is still loading (`progress` 0..1).
std::vector<AudioShape> audioProgressScene(const AudioView &view, float progress, const AudioDisplayOptions &options);
// Legacy DrawCursor: the mouse cursor's line and, when not playing, its time.
std::vector<AudioShape> audioCursor(const AudioView &view, float x, bool playing, const AudioDisplayOptions &options);

// Legacy DrawDashedLine: the dashes of a line, `dash` pixels on and off.
std::vector<std::pair<float, float>> legacyDashes(float from, float to, int dash);
// Legacy SubsTime::raw for ASS: "H:MM:SS.cc", negative times as 0.
std::string legacyAssTime(int ms);

// A Line as the inactive-line shading sees it.
struct AudioLineSpan {
    int startMs = 0, endMs = 0;
    bool visible = true; // shown in the Grid (legacy isVisible)
};
// Legacy SubsGrid::GetKeyFromPosition over the Lines' visibility.
int legacyKeyFromPosition(std::span<const AudioLineSpan> lines, int position, int delta);
// Legacy DrawInactiveLines' choice: mode 1 the previous and next shown Lines
// (and any hidden ones between), mode 2 all, without the active Line.
std::vector<std::pair<int, int>> legacyInactiveLines(int mode, std::span<const AudioLineSpan> lines, int active);

// Legacy SetDialogue with AUDIO_GRAB_TIMES_ON_SELECT: the selection a newly
// active Line gives. A 0:00:00.00 to 0:00:00.00 Line starts at the previous
// shown Line's end when it is the Line after the previously active one, else
// at 0, and lasts 5 s.
std::pair<int, int> legacyLineSelection(std::span<const AudioLineSpan> lines, int active, int previousActive);

} // namespace hikari::application
