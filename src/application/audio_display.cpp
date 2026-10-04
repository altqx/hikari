#include "hikari/application/audio_display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

namespace hikari::application {

// Legacy WaveformPeaks::Append.
void WaveformPeaks::append(const std::int16_t *samples, std::int64_t count)
{
    for (std::int64_t i = 0; i < count; i++, m_samples++) {
        const std::int16_t sample = samples[i];
        if (m_samples % m_block == 0) {
            m_lo.push_back(sample);
            m_hi.push_back(sample);
        } else {
            m_lo.back() = std::min(m_lo.back(), sample);
            m_hi.back() = std::max(m_hi.back(), sample);
        }
    }
}

// Legacy WaveformPeaks::Range.
bool WaveformPeaks::range(std::int64_t first, std::int64_t count, std::int16_t *lo, std::int16_t *hi) const
{
    if (first < 0)
        first = 0;
    if (first >= m_samples || count <= 0)
        return false;
    const auto begin = static_cast<std::size_t>(first / m_block);
    const auto end = std::min(static_cast<std::size_t>((first + count - 1) / m_block) + 1, m_lo.size());
    *lo = *std::min_element(m_lo.begin() + static_cast<std::ptrdiff_t>(begin), m_lo.begin() + static_cast<std::ptrdiff_t>(end));
    *hi = *std::max_element(m_hi.begin() + static_cast<std::ptrdiff_t>(begin), m_hi.begin() + static_cast<std::ptrdiff_t>(end));
    return true;
}

namespace {

// Legacy RAMCache's blocks: 1 << 22 bytes each.
class RamStore : public AudioStore {
public:
    explicit RamStore(int channels) : m_channels(channels) {}

    bool append(const std::int16_t *interleaved, std::int64_t frames) override
    {
        const std::int64_t frameBytes = 2 * m_channels;
        const std::int64_t perBlock = kBlockBytes / frameBytes;
        try {
            while (frames > 0) {
                const std::int64_t used = m_frames % perBlock;
                if (used == 0)
                    m_blocks.emplace_back(std::make_unique<std::int16_t[]>(static_cast<std::size_t>(perBlock * m_channels)));
                const std::int64_t n = std::min(frames, perBlock - used);
                std::copy_n(interleaved, n * m_channels, m_blocks.back().get() + used * m_channels);
                interleaved += n * m_channels;
                frames -= n;
                m_frames += n;
            }
        } catch (const std::bad_alloc &) {
            return false;
        }
        return true;
    }
    void read(std::int64_t start, std::int64_t count, std::int16_t *out) const override
    {
        const std::int64_t perBlock = kBlockBytes / (2 * m_channels);
        while (count > 0) {
            const std::int64_t block = start / perBlock, offset = start % perBlock;
            const std::int64_t n = std::min(count, perBlock - offset);
            std::copy_n(m_blocks[static_cast<std::size_t>(block)].get() + offset * m_channels, n * m_channels, out);
            out += n * m_channels;
            start += n;
            count -= n;
        }
    }
    std::int64_t frames() const override { return m_frames; }
    int channels() const override { return m_channels; }

private:
    static constexpr std::int64_t kBlockBytes = std::int64_t(1) << 22;
    int m_channels;
    std::int64_t m_frames = 0;
    std::vector<std::unique_ptr<std::int16_t[]>> m_blocks;
};

int seekTo(std::FILE *file, std::int64_t offset)
{
#ifdef _WIN32
    return _fseeki64(file, offset, SEEK_SET);
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET);
#endif
}

std::FILE *openFile(const std::filesystem::path &path, bool write)
{
#ifdef _WIN32
    return _wfopen(path.c_str(), write ? L"w+b" : L"rb");
#else
    return std::fopen(path.c_str(), write ? "w+b" : "rb");
#endif
}

// Legacy DiskCache: raw interleaved frames (no header, despite the .w64 name).
class DiskStore : public AudioStore {
public:
    DiskStore(std::filesystem::path path, std::FILE *file, int channels)
        : m_path(std::move(path)), m_file(file), m_channels(channels)
    {
    }
    // A complete cache file read back as it is (legacy DiskCache opening it "rb").
    DiskStore(std::filesystem::path path, std::FILE *file, int channels, std::int64_t frames)
        : m_path(std::move(path)), m_file(file), m_channels(channels), m_frames(frames), m_reused(true)
    {
    }
    ~DiskStore() override
    {
        std::fclose(m_file);
        if (m_reused)
            return; // neither renamed nor removed
        std::error_code ec;
        std::filesystem::path part = m_path;
        part += ".part";
        if (m_complete)
            std::filesystem::rename(part, m_path, ec);
        else
            std::filesystem::remove(part, ec);
    }

    bool append(const std::int16_t *interleaved, std::int64_t frames) override
    {
        if (m_reused || seekTo(m_file, m_frames * 2 * m_channels) != 0)
            return false;
        const auto n = static_cast<std::size_t>(frames * m_channels);
        if (std::fwrite(interleaved, sizeof(std::int16_t), n, m_file) != n)
            return false;
        m_frames += frames;
        return true;
    }
    void read(std::int64_t start, std::int64_t count, std::int16_t *out) const override
    {
        const auto n = static_cast<std::size_t>(count * m_channels);
        std::size_t got = 0;
        if (seekTo(m_file, start * 2 * m_channels) == 0)
            got = std::fread(out, sizeof(std::int16_t), n, m_file);
        std::fill(out + got, out + n, std::int16_t(0)); // a short read is silence, not stale memory
    }
    std::int64_t frames() const override { return m_frames; }
    int channels() const override { return m_channels; }
    void complete() override
    {
        std::fflush(m_file);
        m_complete = true;
    }

private:
    std::filesystem::path m_path;
    std::FILE *m_file;
    int m_channels;
    std::int64_t m_frames = 0;
    bool m_complete = false;
    bool m_reused = false;
};

} // namespace

std::unique_ptr<AudioStore> ramAudioStore(int channels)
{
    return std::make_unique<RamStore>(channels);
}

std::unique_ptr<AudioStore> diskAudioStore(const std::filesystem::path &path, int channels, std::string *error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::remove(path, ec); // legacy: a new index makes a new cache
    std::filesystem::path part = path;
    part += ".part";
    std::FILE *file = openFile(part, true);
    if (!file) {
        if (error)
        {
            const auto text = part.u8string();
            *error = std::string(text.begin(), text.end());
        }
        return nullptr;
    }
    return std::make_unique<DiskStore>(path, file, channels);
}

std::unique_ptr<AudioStore> cachedAudioStore(const std::filesystem::path &path, int channels)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
        return nullptr;
    std::FILE *file = openFile(path, false);
    if (!file)
        return nullptr;
    return std::make_unique<DiskStore>(path, file, channels, static_cast<std::int64_t>(size) / (2 * channels));
}

DisplayAudio::DisplayAudio(int sampleRate, std::int64_t sampleCount, std::unique_ptr<AudioStore> store)
    : m_rate(sampleRate), m_count(sampleCount), m_store(std::move(store))
{
}

DisplayAudio DisplayAudio::silence(int sampleRate, std::int64_t sampleCount)
{
    DisplayAudio audio(sampleRate, 0);
    audio.m_count = sampleCount;
    audio.m_silence = true;
    audio.m_finished = true;
    return audio;
}

bool DisplayAudio::appendFrames(const std::int16_t *interleaved, std::int64_t frames, int channels)
{
    if (frames <= 0)
        return true;
    if (!m_store)
        m_store = ramAudioStore(channels);
    if (channels != m_store->channels())
        return false;
    const std::int64_t at = m_store->frames();
    if (!m_store->append(interleaved, frames))
        return false;
    // The peak table covers the samples the display reads (legacy BuildPeaks
    // over GetNumSamples), not frames a positive delay pushed past the end.
    const std::int64_t keep = std::clamp<std::int64_t>(m_count - at, 0, frames);
    std::vector<std::int16_t> mono(static_cast<std::size_t>(keep));
    for (std::int64_t i = 0; i < keep; i++) {
        if (channels == 1) {
            mono[static_cast<std::size_t>(i)] = interleaved[i];
            continue;
        }
        int sum = 0;
        for (int c = 0; c < channels; c++)
            sum += interleaved[i * channels + c];
        mono[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(sum / channels);
    }
    m_peaks.append(mono.data(), keep);
    return true;
}

bool DisplayAudio::appendSilence(std::int64_t frames, int channels)
{
    constexpr std::int64_t kChunk = 1 << 16;
    const std::vector<std::int16_t> zero(static_cast<std::size_t>(kChunk * channels), 0);
    while (frames > 0) {
        const std::int64_t n = std::min(frames, kChunk);
        if (!appendFrames(zero.data(), n, channels))
            return false;
        frames -= n;
    }
    return true;
}

bool DisplayAudio::scanStored(std::int64_t frames)
{
    if (!m_store)
        return false;
    const int channels = m_store->channels();
    const std::int64_t keep = std::clamp<std::int64_t>(m_count - m_scanned, 0, frames);
    if (keep > 0) {
        m_frames.resize(static_cast<std::size_t>(keep * channels));
        std::fill(m_frames.begin(), m_frames.end(), std::int16_t(0));
        // past the end of a short file the frames stay zero
        const std::int64_t stored = std::clamp<std::int64_t>(m_store->frames() - m_scanned, 0, keep);
        if (stored > 0)
            m_store->read(m_scanned, stored, m_frames.data());
        std::vector<std::int16_t> mono(static_cast<std::size_t>(keep));
        for (std::int64_t i = 0; i < keep; i++) {
            int sum = 0;
            for (int c = 0; c < channels; c++)
                sum += m_frames[static_cast<std::size_t>(i * channels + c)];
            mono[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(sum / channels);
        }
        m_peaks.append(mono.data(), keep);
        m_scanned += keep;
    }
    return m_scanned < m_count;
}

void DisplayAudio::finish()
{
    if (m_store && m_store->frames() >= m_count)
        m_store->complete();
    m_finished = true;
}

void DisplayAudio::read(std::int64_t start, std::int64_t count, std::int16_t *out) const
{
    if (count <= 0)
        return;
    const std::int64_t have = m_silence || !m_store ? 0 : std::min(m_store->frames(), m_count);
    const std::int64_t first = std::clamp<std::int64_t>(start, 0, have);
    const std::int64_t last = std::clamp<std::int64_t>(start + count, 0, have);
    std::fill(out, out + count, std::int16_t(0));
    if (last <= first)
        return;
    const int channels = m_store->channels();
    std::int16_t *dst = out + (first - start);
    if (channels == 1)
        return m_store->read(first, last - first, dst);
    m_frames.resize(static_cast<std::size_t>((last - first) * channels));
    m_store->read(first, last - first, m_frames.data());
    for (std::int64_t i = 0; i < last - first; i++) {
        int sum = 0;
        for (int c = 0; c < channels; c++)
            sum += m_frames[static_cast<std::size_t>(i * channels + c)];
        dst[i] = static_cast<std::int16_t>(sum / channels);
    }
}

// A4: legacy ReadCache (ProviderDummy's playback is silence). Frames not
// yet decoded read as silence; legacy read whatever its cache held there.
void DisplayAudio::readFrames(std::int64_t start, std::int64_t count, std::int16_t *out) const
{
    if (count <= 0)
        return;
    const int channels = this->channels();
    const std::int64_t have = m_silence || !m_store ? 0 : std::min(m_store->frames(), m_count);
    const std::int64_t first = std::clamp<std::int64_t>(start, 0, have);
    const std::int64_t last = std::clamp<std::int64_t>(start + count, 0, have);
    std::fill(out, out + count * channels, std::int16_t(0));
    if (last > first)
        m_store->read(first, last - first, out + (first - start) * channels);
}

// Legacy Provider::GetWaveForm.
WaveformColumns legacyWaveform(const DisplayAudio &audio, std::int64_t start, int w, int h, int samples, float scale)
{
    WaveformColumns out;
    if (w <= 0)
        return out;
    out.min.assign(static_cast<std::size_t>(w), h);
    out.peak.assign(static_cast<std::size_t>(w), 0);
    const int halfH = h / 2;
    const int halfAmplitude = static_cast<int>(halfH * scale);
    auto toY = [=](int sample) {
        const int y = halfH - (sample * halfAmplitude) / 0x8000;
        return y > h ? h : y < 0 ? 0 : y;
    };

    // zoomed out, whole blocks of the peak table are close enough
    if (const WaveformPeaks *peaks = audio.peaks(); peaks && samples >= peaks->blockSamples() * 4) {
        for (int i = 0; i < w; i++) {
            std::int16_t lo = 0, hi = 0;
            peaks->range(start + static_cast<std::int64_t>(i) * samples, samples, &lo, &hi);
            out.min[static_cast<std::size_t>(i)] = toY(hi);
            out.peak[static_cast<std::size_t>(i)] = toY(lo);
        }
        return out;
    }

    const std::int64_t n = static_cast<std::int64_t>(w) * samples;
    if (n <= 0)
        return out;
    std::vector<std::int16_t> raw(static_cast<std::size_t>(n));
    audio.read(start, n, raw.data());
    for (std::int64_t i = 0; i < n; i++) {
        const auto cur = static_cast<std::size_t>(i / samples);
        int value = halfH - (int(raw[static_cast<std::size_t>(i)]) * halfAmplitude) / 0x8000;
        if (value > h)
            value = h;
        if (value < 0)
            value = 0;
        if (value < out.min[cur])
            out.min[cur] = value;
        if (value > out.peak[cur])
            out.peak[cur] = value;
    }
    return out;
}

float audioScaleFromSlider(int position)
{
    return std::pow(float(position) / 50.0f, 3);
}

void AudioView::setSource(int sampleRate, std::int64_t sampleCount)
{
    m_rate = sampleRate;
    m_count = sampleCount;
    m_hasSource = true;
}

// Legacy OnSize (its early return compares the new width with the height).
void AudioView::resize(int clientWidth, int clientHeight, int timelineHeight, int scrollbarThickness)
{
    if (clientWidth == m_w && clientWidth == m_h && timelineHeight == m_timeline)
        return;
    m_w = clientWidth;
    m_timeline = timelineHeight;
    m_h = clientHeight - timelineHeight;
    updateSamples();
    if (m_samples && m_hasSource)
        updatePosition(m_positionSample / m_samples, false, scrollbarThickness);
    updateSamples(); // legacy UpdateImage
    if (m_hasSource)
        updateScrollbar(scrollbarThickness);
}

// Legacy SetSamplesPercent (the horizontal zoom).
void AudioView::setSamplesPercent(int percent, bool update, float pivot, int scrollbarThickness)
{
    if (percent < 1)
        percent = 1;
    if (percent > 100)
        percent = 100;
    if (m_samplesPercent == percent)
        return;
    m_samplesPercent = percent;
    if (update) {
        const int oldSamples = m_samples;
        updateSamples();
        m_positionSample += static_cast<std::int64_t>((oldSamples - m_samples) * kZoomWidth * pivot);
        if (m_positionSample < 0)
            m_positionSample = 0;
        updateSamples();
        updateScrollbar(scrollbarThickness);
    }
}

// Legacy UpdateSamples.
void AudioView::updateSamples()
{
    if (!m_hasSource)
        return;
    if (m_w) {
        const std::int64_t totalSamples = m_count;
        const int max = (m_rate * 120) / kZoomWidth; // 2 minutes maximum
        m_samples = int(max * std::pow(m_samplesPercent / 100.0, 3));
        if (m_samples <= 0)
            m_samples = 1;
        const int length = kZoomWidth * m_samples;
        if (m_positionSample + length > totalSamples) {
            m_positionSample = totalSamples - length;
            if (m_positionSample < 0)
                m_positionSample = 0;
            if (m_samples)
                m_position = m_positionSample / m_samples;
        }
    }
}

// Legacy UpdatePosition (an int position).
void AudioView::updatePosition(std::int64_t pos64, bool isSample, int scrollbarThickness)
{
    if (!m_hasSource || m_samples <= 0)
        return;
    int pos = static_cast<int>(pos64);
    if (isSample)
        pos /= m_samples;
    const int len = static_cast<int>(m_count / m_samples);
    if (pos < 0)
        pos = 0;
    if (pos >= len)
        pos = len - 1;
    m_position = pos;
    m_positionSample = static_cast<std::int64_t>(pos) * m_samples;
    updateScrollbar(scrollbarThickness);
}

void AudioView::setPosition(int pos)
{
    m_position = pos;
    m_positionSample = static_cast<std::int64_t>(pos) * m_samples;
}

// Legacy UpdateScrollbar.
AudioView::Scrollbar AudioView::updateScrollbar(int scrollbarThickness)
{
    if (!m_hasSource || m_samples <= 0)
        return {};
    int page = m_w / 12;
    const int len = static_cast<int>(m_count / m_samples / 12) + scrollbarThickness;
    if (page > len) {
        page = len;
        m_positionSample = 0;
    }
    m_position = m_positionSample / m_samples;
    m_bar = {static_cast<int>(m_position / 12), page, len};
    return m_bar;
}

// Legacy MakeDialogueVisible, without karaoke.
void AudioView::makeVisible(int startShow, int endShow, bool force, bool moveToEnd, int scrollbarThickness)
{
    if (!m_hasSource || m_samples <= 0)
        return;
    const int startPos = static_cast<int>(sampleAtMs(startShow));
    const int endPos = static_cast<int>(sampleAtMs(endShow));
    const int startX = static_cast<int>(xAtMs(startShow));
    const int endX = static_cast<int>(xAtMs(endShow));
    const int w = m_w, samples = m_samples;
    if (force || (startX < 50 && endX < w) || (endX > w - 50 && startX > 0)) {
        if ((startX < 50) || (endX >= w - 50)) {
            if (moveToEnd && (endX >= w - 50 || endX < 50))
                // the right edge of the selection at least 50 pixels from the edge
                updatePosition(endPos - ((w - 50) * samples), true, scrollbarThickness);
            else if (!moveToEnd)
                // the left edge of the selection at least 50 pixels from the edge
                updatePosition(startPos - 50 * samples, true, scrollbarThickness);
        } else {
            // otherwise the selection centred in the display
            updatePosition((startPos + endPos - w * samples) / 2, true, scrollbarThickness);
        }
    }
    updateSamples(); // legacy UpdateImage
}

float AudioView::xAtMs(std::int64_t ms) const
{
    if (!m_samples)
        return 0;
    return static_cast<float>(((ms * m_rate / 1000.0) - m_positionSample) / double(m_samples));
}

int AudioView::msAtX(std::int64_t x) const
{
    if (!m_rate)
        return 0;
    return static_cast<int>((m_positionSample + (x * m_samples)) * 1000 / m_rate);
}

std::int64_t AudioView::sampleAtMs(std::int64_t ms) const
{
    return ms * m_rate / 1000;
}

float AudioView::xAtSample(std::int64_t n) const
{
    return m_samples ? static_cast<float>((double(n) / double(m_samples)) - m_position) : 0;
}

std::int64_t AudioView::sampleAtX(int x) const
{
    return (x + m_position) * m_samples;
}

namespace {

AudioShape fill(float x1, float y1, float x2, float y2, std::uint32_t colour)
{
    AudioShape s;
    s.kind = AudioShape::Kind::Fill;
    s.colour = colour;
    s.x1 = x1;
    s.y1 = y1;
    s.x2 = x2;
    s.y2 = y2;
    return s;
}

AudioShape line(float x1, float y1, float x2, float y2, std::uint32_t colour, float width = 1)
{
    AudioShape s;
    s.kind = AudioShape::Kind::Line;
    s.colour = colour;
    s.x1 = x1;
    s.y1 = y1;
    s.x2 = x2;
    s.y2 = y2;
    s.width = width;
    return s;
}

AudioShape triangle(float x1, float y1, float x2, float y2, float x3, float y3, std::uint32_t colour)
{
    AudioShape s = line(x1, y1, x2, y2, colour);
    s.kind = AudioShape::Kind::Triangle;
    s.x3 = x3;
    s.y3 = y3;
    return s;
}

AudioShape text(std::string value, float left, float top, float right, float bottom, AudioShape::Font font,
                AudioShape::Align align, std::uint32_t colour, bool outlined)
{
    AudioShape s = fill(left, top, right, bottom, colour);
    s.kind = AudioShape::Kind::Text;
    s.text = std::move(value);
    s.font = font;
    s.align = align;
    s.outlined = outlined;
    return s;
}

// A vertical dashed line (legacy DrawDashedLine on (x, 0) to (x, h)).
void dashedVertical(std::vector<AudioShape> &out, float x, float top, float bottom, std::uint32_t colour, float width,
                    int dash = 3)
{
    for (const auto &[from, to] : legacyDashes(top, bottom, dash))
        out.push_back(line(x, from, x, to, colour, width));
}

std::string format(const char *pattern, int a, int b = 0, int c = 0)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, pattern, a, b, c);
    return buffer;
}

// Legacy DrawTimescale.
void timescale(std::vector<AudioShape> &out, const AudioView &view, const AudioDisplayOptions &options,
               const AudioTextWidth &textWidth)
{
    const int w = view.width(), h = view.height(), timelineHeight = view.timelineHeight();
    const int samples = view.samples();
    out.push_back(fill(0, float(h), float(w), float(h + timelineHeight), options.timescaleBackground));
    out.push_back(line(0, float(h), float(w), float(h), options.timescaleText));

    // Timescale ticks
    const std::int64_t start = view.position() * samples;
    const int rate = view.sampleRate();
    if (rate <= 0 || samples <= 0)
        return;
    int lineStart = 0;
    int otherLinesModulo = 0;
    int linesModulo = 1;
    for (int x = 0;; x++) {
        const std::int64_t pos = (std::int64_t(x) * samples) + start;
        // Second boundary
        if (pos % rate < samples) {
            if (lineStart) {
                // from the second boundary found, the longest time text's extent
                const int lineDist = x - lineStart;
                const int s = static_cast<int>(pos / rate);
                const int hr = s / 3600;
                const int m = s / 60;
                const int textW = textWidth(AudioShape::Font::Scale, hr ? "X0:00:00X" : m ? "X00:00X" : "X00X");
                const float numTextPlaced = float(lineDist) / float(textW);
                if (numTextPlaced > 9.f)
                    otherLinesModulo = 1;
                else if (numTextPlaced > 4.5f)
                    otherLinesModulo = 2;
                else if (numTextPlaced > 2.5f)
                    otherLinesModulo = 5;
                else if (numTextPlaced < 1.2f)
                    linesModulo = static_cast<int>(float(textW + 10) / float(lineDist));
                if (!linesModulo)
                    linesModulo = 10;
                break;
            }
            lineStart = x;
        }
    }
    auto drawTime = [&](int x, std::int64_t pos, bool drawMS) {
        int s = static_cast<int>(pos / rate);
        const int hr = s / 3600;
        int m = s / 60;
        m = m % 60;
        s = s % 60;
        std::string label = hr ? format("%i:%02i:%02i", hr, m, s) : m ? format("%i:%02i", m, s) : format("%i", s);
        if (drawMS) {
            const int ms = static_cast<int>((pos / (rate / 10)) % 10);
            if (ms)
                label += format(".%i", ms);
        }
        out.push_back(text(std::move(label), float(x - 50), float(h + 8), float(x + 50), float(h + 48),
                           AudioShape::Font::Scale, AudioShape::Align::TopCenter, options.timescaleText, false));
    };
    // legacy loops over i = 1, 2, 4... but leaves after the first
    const int i = 1;
    int pixBounds = rate / (samples * 10 / i);
    // a pixBounds of 1 cannot take the else branch, it would make it 0
    if (pixBounds <= 1)
        pixBounds = 1;
    else if (pixBounds > 10)
        pixBounds = 10;
    else
        pixBounds = (pixBounds / 2) * 2;
    for (int x = 0; x < w; x++) {
        const std::int64_t pos = (std::int64_t(x) * samples) + start;
        // Second boundary
        if (pos % rate < samples) {
            out.push_back(line(float(x), float(h + 2), float(x), float(h + 8), options.timescaleText));
            const int s = static_cast<int>(pos / rate);
            if (s % linesModulo == 0)
                drawTime(x, pos, false);
        }
        // Other
        else if (pos % (rate / pixBounds * i) < samples) {
            out.push_back(line(float(x), float(h + 2), float(x), float(h + 5), options.timescaleText));
            const int ms = static_cast<int>((pos / (rate / pixBounds)) % pixBounds);
            if (otherLinesModulo && (ms % otherLinesModulo == 0) && pixBounds == 10)
                drawTime(x, pos, true);
        }
    }
}

// Legacy GetDialoguePos: the selection's x, truncated, optionally kept in the view.
std::pair<std::int64_t, std::int64_t> dialoguePos(const AudioView &view, const AudioMarks &marks, bool cap)
{
    auto selStart = static_cast<std::int64_t>(view.xAtMs(marks.startMs));
    auto selEnd = static_cast<std::int64_t>(view.xAtMs(marks.endMs));
    if (cap) {
        const int w = view.width();
        if (selStart < 0)
            selStart = 0;
        if (selEnd < 0)
            selEnd = 0;
        if (selStart >= w)
            selStart = w - 1;
        if (selEnd >= w)
            selEnd = w - 1;
    }
    return {selStart, selEnd};
}

} // namespace

std::vector<std::pair<float, float>> legacyDashes(float from, float to, int dash)
{
    // DrawDashedLine walks from `from` towards `to` in float steps.
    std::vector<std::pair<float, float>> out;
    const float diff = from - to;
    const float len = std::sqrt(diff * diff);
    if (len == 0)
        return out;
    const float unit = diff / len;
    const float singleMovement = 1 / (len / (dash * 2));
    float p0 = from, p1 = p0;
    for (float j = 0; j <= 1; j += singleMovement) {
        p1 -= unit * dash;
        if (j + singleMovement >= 1)
            p1 = to;
        out.emplace_back(p0, p1);
        p1 -= unit * dash;
        p0 -= (unit * dash) * 2;
    }
    return out;
}

std::string legacyAssTime(int ms)
{
    if (ms < 0)
        ms = 0;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%01i:%02i:%02i.%02i", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60,
                  (ms / 10) % 100);
    return buffer;
}

std::vector<AudioShape> audioScene(const AudioView &view, const WaveformColumns &columns, const AudioMarks &marks,
                                   const AudioDisplayOptions &options, const AudioTextWidth &textWidth)
{
    std::vector<AudioShape> out;
    const int w = view.width(), h = view.height(), displayH = h + view.timelineHeight();
    if (w < 1 || displayH < 1)
        return out;
    // Background
    out.push_back(fill(0, 0, float(w), float(displayH), options.background));

    const auto [lineStart, lineEnd] = dialoguePos(view, marks, false);
    const auto [selStartCap, selEndCap] = dialoguePos(view, marks, true);
    const std::int64_t selStart = lineStart, selEnd = lineEnd;
    const bool hasSel = true; // legacy sets it before every draw

    // Selection background
    if (hasSel && lineStart < lineEnd && options.drawSelectionBackground)
        out.push_back(fill(float(lineStart), 0, float(lineEnd + 1), float(h),
                           marks.modified ? options.selectionModified : options.selectionBackground));

    // Waveform: one line per column, from below its lowest sample to above its highest
    std::uint32_t waveformSel = options.waveform;
    if (hasSel && options.drawSelectionBackground)
        waveformSel = marks.modified ? options.waveformModified : options.waveformSelected;
    const int drawn = std::min<int>(w, static_cast<int>(columns.min.size()));
    for (int i = 0; i < drawn; i++) {
        const bool selected = hasSel && i >= selStartCap && i < selEndCap;
        const float x = i + 0.5f;
        out.push_back(line(x, columns.peak[static_cast<std::size_t>(i)] + 0.5f, x,
                           columns.min[static_cast<std::size_t>(i)] - 0.5f,
                           selected ? waveformSel : options.waveform));
    }

    // Inactive Lines (legacy DrawInactiveLines)
    if (options.inactiveLines != 0) {
        for (const auto &[startMs, endMs] : marks.inactive) {
            const int shadeX1 = static_cast<int>(view.xAtMs(startMs));
            const int shadeX2 = static_cast<int>(view.xAtMs(endMs));
            if (shadeX2 < 0 || shadeX1 > w)
                continue;
            // the selection
            const int selX1 = static_cast<int>(std::max(0.f, view.xAtMs(marks.startMs)));
            const int selX2 = static_cast<int>(std::min(float(w), view.xAtMs(marks.endMs)));
            // the ranges x1..x2 and x3..x4 outside it
            int x1 = std::max(0, shadeX1);
            int x2 = std::min(w, shadeX2);
            const int x3 = std::max(x1, selX2);
            const int x4 = std::max(x2, selX2);
            x1 = std::min(x1, selX1);
            x2 = std::min(x2, selX1);
            out.push_back(fill(float(x1), 0, float(x2 + 1), float(h), options.inactiveBackground));
            out.push_back(fill(float(x3), 0, float(x4 + 1), float(h), options.inactiveBackground));
            auto inactiveColumn = [&](int i) {
                if (i < 0 || i >= drawn)
                    return;
                out.push_back(line(float(i), float(columns.peak[static_cast<std::size_t>(i)]), float(i),
                                   float(columns.min[static_cast<std::size_t>(i)] - 1), options.waveformInactive));
            };
            for (int i = x1; i < x2; i++)
                inactiveColumn(i);
            for (int i = x3; i < x4; i++)
                inactiveColumn(i);
            // their boundaries
            const float half = float(options.lineBoundaryWidth / 2);
            out.push_back(line(shadeX1 + half, 0, shadeX1 + half, float(h), options.inactiveBoundary,
                               float(options.lineBoundaryWidth)));
            out.push_back(line(shadeX2 + half, 0, shadeX2 + half, float(h), options.inactiveBoundary,
                               float(options.lineBoundaryWidth)));
        }
    }

    // Seconds boundaries
    const int samples = view.samples();
    if (options.drawSecondBoundaries && samples > 0 && view.sampleRate() > 0) {
        const std::int64_t start = view.position() * samples;
        const int rate = view.sampleRate();
        const int pixBounds = rate / samples;
        if (pixBounds >= 8)
            for (int x = 0; x < w; x++)
                if (((std::int64_t(x) * samples) + start) % rate < samples)
                    dashedVertical(out, float(x), 0, float(h), options.secondBoundaries, 1);
    }

    if (hasSel) {
        // the start boundary, with its flags at the top and the bottom
        const float boundary = float(options.lineBoundaryWidth);
        int startDraw = static_cast<int>(lineStart + (options.lineBoundaryWidth / 2));
        const std::uint32_t colour = options.lineStart;
        out.push_back(line(float(startDraw), 0, float(startDraw), float(h), options.lineStart, boundary));
        auto flags = [&](float x, float dx) {
            out.push_back(triangle(x, 0, x + dx, 0, x, 10, colour));
            out.push_back(triangle(x, float(h - 10), x + dx, float(h), x, float(h), colour));
            out.push_back(line(x, 0, x + dx, 0, colour));
            out.push_back(line(x, float(h - 10), x + dx, float(h), colour));
        };
        flags(float(startDraw), 10);
        // the end boundary; its flags take the start boundary's colour
        startDraw = static_cast<int>(lineEnd + (options.lineBoundaryWidth / 2));
        out.push_back(line(float(startDraw), 0, float(startDraw), float(h), options.lineEnd, boundary));
        flags(float(startDraw), -10);
    }

    // Keyframes (legacy DrawKeyframes)
    if (options.drawKeyframes && !marks.keyframesMs.empty()) {
        const int mintime = view.msAtX(0);
        const int maxtime = view.msAtX(w);
        for (int cur : marks.keyframesMs) {
            if (cur >= mintime && cur <= maxtime) {
                cur = ((cur - 20) / 10) * 10;
                const int x = static_cast<int>(view.xAtMs(cur));
                out.push_back(line(float(x), 0, float(x), float(h), options.keyframe));
            }
            if (cur > maxtime)
                break;
        }
    }

    // Modified text
    if (marks.modified || selStart > selEnd)
        out.push_back(text(selStart <= selEnd ? "Modified" : "Negative time", 4, 4, 304, 104,
                           AudioShape::Font::Label, AudioShape::Align::TopLeft, 0xFFFF0000, true));

    timescale(out, view, options, textWidth);

    // The paused video's frame
    if (options.drawVideoPosition && marks.videoMs)
        dashedVertical(out, view.xAtMs(*marks.videoMs), 0, float(h), options.cursor, 2);

    // Focus border
    if (marks.focused) {
        const float r = float(w - 1), b = float(h - 1);
        out.push_back(line(0, 0, r, 0, options.waveform));
        out.push_back(line(r, 0, r, b, options.waveform));
        out.push_back(line(r, b, 0, b, options.waveform));
        out.push_back(line(0, b, 0, 0, options.waveform));
    }
    return out;
}

std::vector<AudioShape> audioProgressScene(const AudioView &view, float progress, const AudioDisplayOptions &options)
{
    std::vector<AudioShape> out;
    const int w = view.width(), h = view.height(), displayH = h + view.timelineHeight();
    if (w < 1 || displayH < 1)
        return out;
    out.push_back(fill(0, 0, float(w), float(displayH), options.background));
    const float halfY = float((h + 20) / 2);
    // the outer frame (cyan) and the inner one (white)
    const float outer[] = {20, halfY - 20, float(w - 20), halfY + 20};
    const float inner[] = {21, halfY - 19, float(w - 21), halfY + 19};
    for (const float *f : {outer, inner}) {
        const std::uint32_t colour = f == outer ? 0xFF00FFFF : 0xFFFFFFFF;
        out.push_back(line(f[0], f[1], f[2], f[1], colour));
        out.push_back(line(f[2], f[1], f[2], f[3], colour));
        out.push_back(line(f[2], f[3], f[0], f[3], colour));
        out.push_back(line(f[0], f[3], f[0], f[1], colour));
    }
    // the bar
    const int rw = 22;
    out.push_back(line(float(rw), halfY, (progress / 1.f) * (w - 44) + rw, halfY, 0xFFFFFFFF, 37));
    out.push_back(text(format("%d%%", static_cast<int>(progress * 100.f)), 20, halfY - 20, float(w - 20), halfY + 20,
                       AudioShape::Font::Cursor, AudioShape::Align::Center, 0xFFFFFFFF, true));
    return out;
}

std::vector<AudioShape> audioCursor(const AudioView &view, float x, bool playing, const AudioDisplayOptions &options)
{
    std::vector<AudioShape> out;
    AudioShape cursor = line(x, 0, x, float(view.height()), options.cursor, 2);
    cursor.smooth = true;
    out.push_back(cursor);
    if (!playing) {
        const int ms = view.msAtX(static_cast<std::int64_t>(x));
        const float top = 5; // 20 with karaoke (A5)
        const float left = float(static_cast<long>(x - 150)); // a RECT holds integers
        out.push_back(text(legacyAssTime(ms), left, top, left + 300, top + 100, AudioShape::Font::Cursor,
                           AudioShape::Align::TopCenter, 0xFFFFFFFF, true));
    }
    return out;
}

int legacyKeyFromPosition(std::span<const AudioLineSpan> lines, int position, int delta)
{
    const int count = static_cast<int>(lines.size());
    if (position > count)
        return 0;
    int visibleLines = 0;
    if (delta > 0) {
        for (int i = position + 1; i < count; i++) {
            if (lines[static_cast<std::size_t>(i)].visible)
                visibleLines++;
            if (delta == visibleLines)
                return i;
        }
        return legacyKeyFromPosition(lines, count, -1);
    }
    if (delta < 0 && position > 0) {
        for (int i = position - 1; i >= 0; i--) {
            if (lines[static_cast<std::size_t>(i)].visible)
                visibleLines--;
            if (delta == visibleLines)
                return i;
        }
        // legacy GetElementById(0): the first shown Line (-1 without one)
        for (int i = 0; i < count; i++)
            if (lines[static_cast<std::size_t>(i)].visible)
                return i;
        return -1;
    }
    return position;
}

std::vector<std::pair<int, int>> legacyInactiveLines(int mode, std::span<const AudioLineSpan> lines, int active)
{
    std::vector<std::pair<int, int>> out;
    const int count = static_cast<int>(lines.size());
    if (mode == 0 || active < 0 || active >= count)
        return out;
    int from = 0, to = count - 1;
    if (mode == 1) {
        from = legacyKeyFromPosition(lines, active, -1);
        to = legacyKeyFromPosition(lines, active, 1);
    }
    for (int j = from; j <= to; j++) {
        if (j == active || j < 0 || j >= count)
            continue;
        const auto &line = lines[static_cast<std::size_t>(j)];
        if (!line.visible)
            continue;
        out.emplace_back(line.startMs, line.endMs);
    }
    return out;
}

std::pair<int, int> legacyLineSelection(std::span<const AudioLineSpan> lines, int active, int previousActive)
{
    const auto &line = lines[static_cast<std::size_t>(active)];
    // never for 0:00:00.00 -> 0:00:00.00 Lines
    if (line.startMs != 0 || line.endMs != 0)
        return {line.startMs, line.endMs};
    const bool isNextLine = previousActive + 1 == active;
    const int previous = legacyKeyFromPosition(lines, active, -1);
    const int start = isNextLine && active != previous && previous >= 0 ? lines[static_cast<std::size_t>(previous)].endMs
                                                                       : line.startMs;
    return {start, start + 5000};
}

} // namespace hikari::application
