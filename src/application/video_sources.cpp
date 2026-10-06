#include "hikari/application/video_sources.h"

#include "hikari/application/legacy_timebase.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace hikari::application {

namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// wxString::AfterLast('.'): the whole text when there is no dot.
std::string_view afterLastDot(std::string_view path)
{
    const auto dot = path.rfind('.');
    return dot == std::string_view::npos ? path : path.substr(dot + 1);
}

// wxString::ToCDouble: the whole text is one number in the C locale.
std::optional<double> toCDouble(std::string_view text)
{
    if (text.empty())
        return std::nullopt;
    const std::string s(text);
    char *end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0')
        return std::nullopt;
    return v;
}

// wxAtoi: atoi on the text (0 when it does not start with a number).
int wxAtoi(std::string_view text)
{
    return std::atoi(std::string(text).c_str());
}

std::uint8_t clipColour(int v)
{
    return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

// colorspace.cpp's rgb_to_hsl (colorspace.cpp:253-290).
void rgbToHsl(int R, int G, int B, std::uint8_t &H, std::uint8_t &S, std::uint8_t &L)
{
    const float r = R / 255.f, g = G / 255.f, b = B / 255.f;
    float h, s, l;
    const float maxrgb = std::max(r, std::max(g, b)), minrgb = std::min(r, std::min(g, b));
    l = (minrgb + maxrgb) / 2;
    if (minrgb == maxrgb) {
        h = 0;
        s = 0;
    } else {
        if (l < 0.5)
            s = (maxrgb - minrgb) / (maxrgb + minrgb);
        else
            s = (maxrgb - minrgb) / (2.f - maxrgb - minrgb);
        if (r == maxrgb)
            h = (g - b) / (maxrgb - minrgb) + 0;
        else if (g == maxrgb)
            h = (b - r) / (maxrgb - minrgb) + 2;
        else
            h = (r - g) / (maxrgb - minrgb) + 4;
    }
    if (h < 0)
        h += 6;
    if (h >= 6)
        h -= 6;
    H = clipColour(int(h * 256 / 6));
    S = clipColour(int(s * 255));
    L = clipColour(int(l * 255));
}

// colorspace.cpp's hsl_to_rgb (colorspace.cpp:44-142).
void hslToRgb(int H, int S, int L, std::uint8_t &R, std::uint8_t &G, std::uint8_t &B)
{
    if (S == 0) {
        R = G = B = static_cast<std::uint8_t>(L);
        return;
    }
    if (L == 128 && S == 255) {
        switch (H) {
        case 0:
        case 255: R = 255; G = 0; B = 0; return;
        case 43: R = 255; G = 255; B = 0; return;
        case 85: R = 0; G = 255; B = 0; return;
        case 128: R = 0; G = 255; B = 255; return;
        case 171: R = 0; G = 0; B = 255; return;
        case 213: R = 255; G = 0; B = 255; return;
        }
    }
    const float h = H / 255.f, s = S / 255.f, l = L / 255.f;
    float temp2;
    if (l < .5)
        temp2 = static_cast<float>(l * (1. + s));
    else
        temp2 = l + s - l * s;
    const float temp1 = 2.f * l - temp2;
    float temp3[3];
    temp3[0] = h + 1.f / 3.f;
    if (temp3[0] > 1.f)
        temp3[0] -= 1.f;
    temp3[1] = h;
    temp3[2] = h - 1.f / 3.f;
    if (temp3[2] < 0.f)
        temp3[2] += 1.f;
    float out[3];
    for (int i = 0; i < 3; ++i) {
        if (6.f * temp3[i] < 1.f)
            out[i] = temp1 + (temp2 - temp1) * 6.f * temp3[i];
        else if (2.f * temp3[i] < 1.f)
            out[i] = temp2;
        else if (3.f * temp3[i] < 2.f)
            out[i] = temp1 + (temp2 - temp1) * ((2.f / 3.f) - temp3[i]) * 6.f;
        else
            out[i] = temp1;
    }
    R = clipColour((int)(out[0] * 255));
    G = clipColour((int)(out[1] * 255));
    B = clipColour((int)(out[2] * 255));
}

// SaturateToInt (Timebase.cpp:26-29)
int saturate(double v)
{
    return v >= static_cast<double>(INT_MAX) ? INT_MAX : static_cast<int>(v);
}

} // namespace

bool isNextFileVideo(std::string_view path)
{
    const std::string ext = lower(afterLastDot(path));
    static constexpr std::string_view kExtensions[] = {"avi", "mp4", "mkv", "ogm", "wmv", "asf", "rmvb",
                                                        "rm",  "3gp", "ts",  "m2ts", "mpg", "mpeg"};
    return std::find(std::begin(kExtensions), std::end(kExtensions), ext) != std::end(kExtensions);
}

NextFileStep NextFileWalker::step(std::optional<std::vector<std::string>> listing, const std::string &current, bool next)
{
    if (listing)
        m_files = std::move(*listing);
    const std::vector<std::string> &files = m_files;
    const int count = static_cast<int>(files.size());
    for (int j = 0; j < count; ++j)
        if (files[static_cast<std::size_t>(j)] == current) {
            m_actualFile = j;
            break;
        }
    if (next && m_actualFile >= count - 1) {
        m_actualFile = count - 1;
        return {};
    }
    if (!next && m_actualFile <= 0) {
        m_actualFile = 0;
        return {};
    }
    for (int k = next ? m_actualFile + 1 : m_actualFile - 1; next ? k < count : k >= 0; next ? ++k : --k) {
        const std::string &file = files[static_cast<std::size_t>(k)];
        if (isNextFileVideo(file)) {
            // legacy moves actualFile only once the file loaded; a failed
            // load ends the walk with FFMS2 (VideoBox.cpp:726-732)
            m_actualFile = k;
            return {NextFileStep::Kind::Open, file};
        }
    }
    return {};
}

bool isDummyVideo(std::string_view path)
{
    return path.starts_with('?');
}

std::expected<std::string, DummyVideoError> dummyVideoText(std::string_view fpsText, int durationMs, int width,
                                                           int height, std::uint8_t red, std::uint8_t green,
                                                           std::uint8_t blue, bool pattern)
{
    const auto fps = toCDouble(fpsText);
    if (!fps || *fps < 15 || *fps > 120)
        return std::unexpected(DummyVideoError::InvalidFps);
    const float frametime = static_cast<float>(1000.f / *fps);
    const int frames = static_cast<int>(durationMs / frametime);
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, "?dummy:%f:%i:%i:%i:%i:%i:%i:%s", static_cast<double>(static_cast<float>(*fps)),
                  frames, width, height, red, green, blue, pattern ? "c" : "");
    return std::string(buffer);
}

int dummyVideoDialogFrames()
{
    const float frametime = 1000.f / 23.976f;
    return static_cast<int>(1500000.f / frametime);
}

std::optional<DummyVideo> parseDummyVideo(std::string_view text)
{
    // wxStringTokenizer(data, ":", wxTOKEN_RET_EMPTY_ALL): the first token
    // ("?dummy") is skipped; each of the eight fields must exist.
    std::vector<std::string_view> tokens;
    std::size_t from = 0;
    while (true) {
        const auto colon = text.find(':', from);
        tokens.push_back(text.substr(from, colon == std::string_view::npos ? std::string_view::npos : colon - from));
        if (colon == std::string_view::npos)
            break;
        from = colon + 1;
    }
    if (tokens.size() < 9)
        return std::nullopt;
    const auto fps = toCDouble(tokens[1]);
    if (!fps)
        return std::nullopt;
    DummyVideo video;
    video.fps = static_cast<float>(*fps);
    video.frames = wxAtoi(tokens[2]);
    if (video.frames < 1)
        return std::nullopt;
    video.width = wxAtoi(tokens[3]);
    video.height = wxAtoi(tokens[4]);
    // legacy refused 0 only (ProviderDummy.cpp:198-201) and then could not
    // make a frame buffer for a negative size (V3-dummy-negative-size)
    if (video.width <= 0 || video.height <= 0)
        return std::nullopt;
    // wxColour(int, int, int) keeps the low byte of each
    video.red = static_cast<std::uint8_t>(wxAtoi(tokens[5]));
    video.green = static_cast<std::uint8_t>(wxAtoi(tokens[6]));
    video.blue = static_cast<std::uint8_t>(wxAtoi(tokens[7]));
    video.pattern = tokens[8] == "c";
    return video;
}

std::vector<std::byte> dummyVideoFrame(const DummyVideo &video)
{
    if (video.width <= 0 || video.height <= 0)
        return {};
    const std::uint8_t r = video.red, g = video.green, b = video.blue;
    std::vector<std::byte> buffer(static_cast<std::size_t>(video.width) * static_cast<std::size_t>(video.height) * 4);
    auto *buff = reinterpret_cast<std::uint8_t *>(buffer.data());
    if (video.pattern) {
        std::uint8_t h = 0, s = 0, l = 0;
        std::uint8_t r1 = 0, g1 = 0, b1 = 0;
        // legacy passes blue and green swapped both ways, as Aegisub does
        rgbToHsl(r, b, g, h, s, l);
        l += 24;
        if (l < 24)
            l -= 48;
        hslToRgb(h, s, l, r1, b1, g1);
        bool ch = false;
        bool ch1 = false;
        for (int i = 0; i < video.height; ++i) {
            if ((i % 10) == 0)
                ch1 = !ch1;
            ch = ch1;
            for (int j = 0; j < video.width; ++j) {
                const std::size_t k = (static_cast<std::size_t>(i) * static_cast<std::size_t>(video.width) + j) * 4;
                if ((j % 10) == 0 && j > 0)
                    ch = !ch;
                buff[k] = ch ? b : b1;
                buff[k + 1] = ch ? g : g1;
                buff[k + 2] = ch ? r : r1;
                buff[k + 3] = 0xFF;
            }
        }
    } else {
        for (std::size_t i = 0; i < buffer.size(); i += 4) {
            buff[i] = b;
            buff[i + 1] = g;
            buff[i + 2] = r;
            buff[i + 3] = 0xFF;
        }
    }
    return buffer;
}

std::vector<int> dummyVideoTimecodes(const DummyVideo &video)
{
    // Timebase::MsAt without timecodes: frame * (double)FrameDuration(),
    // FrameDuration a float 1000.f / fps
    const float duration = video.fps > 0.f ? 1000.f / video.fps : 0.f;
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(std::max(video.frames, 0)));
    for (int i = 0; i < video.frames; ++i)
        out.push_back(saturate(i * static_cast<double>(duration)));
    return out;
}

// DummyVideoSource

std::uint64_t DummyVideoSource::open(const std::string &path, Progress progress, Opened done)
{
    return openIndexed(path, {}, std::move(progress), std::move(done));
}

std::uint64_t DummyVideoSource::openIndexed(const std::string &path, const IndexRequest &request, Progress progress,
                                            Opened done)
{
    m_dummyText = isDummyVideo(path);
    if (!m_dummyText) {
        m_dummy.reset();
        m_frame.clear();
        return m_media.openIndexed(path, request, std::move(progress), std::move(done));
    }
    m_media.cancelOpen();
    m_media.cancelReads();
    m_dummy = parseDummyVideo(path);
    m_frame.clear();
    m_generation = std::max(m_generation, m_media.generation()) + 1;
    if (!m_dummy) {
        // legacy: the provider's success false fails the open (no message)
        done(std::unexpected(SourceError::InvalidInput));
        return m_generation;
    }
    SourceTimeline t;
    t.generation = m_generation;
    t.track = 0;
    // the float frame rate as legacy keeps it (Provider::m_FPS)
    t.fpsNumerator = static_cast<std::int64_t>(static_cast<double>(m_dummy->fps) * 1'000'000.0 + 0.5);
    t.fpsDenominator = 1'000'000;
    t.timeBaseNumerator = 1;
    t.timeBaseDenominator = 1000; // pts in ms
    for (const int ms : dummyVideoTimecodes(*m_dummy))
        t.pts.push_back(ms);
    t.width = m_dummy->width;
    t.height = m_dummy->height;
    t.newIndex = false;
    done(std::move(t));
    return m_generation;
}

void DummyVideoSource::cancelOpen()
{
    if (!m_dummy)
        m_media.cancelOpen();
}

void DummyVideoSource::frame(int index, FrameReady done)
{
    if (!m_dummy)
        return m_media.frame(index, std::move(done));
    if (index < 0 || index >= m_dummy->frames)
        return done(std::unexpected(SourceError::EndOfStream));
    if (m_frame.empty())
        m_frame = dummyVideoFrame(*m_dummy);
    if (m_frame.empty())
        return done(std::unexpected(SourceError::InvalidInput));
    IndexedFrame f;
    f.generation = m_generation;
    f.index = index;
    // dummyVideoTimecodes' frame `index`
    f.pts = saturate(index * static_cast<double>(m_dummy->fps > 0.f ? 1000.f / m_dummy->fps : 0.f));
    f.width = m_dummy->width;
    f.height = m_dummy->height;
    f.stride = m_dummy->width * 4;
    f.bgra = m_frame;
    done(std::move(f));
}

void DummyVideoSource::openAudio(int track, AudioOpened done)
{
    if (!m_dummy)
        return m_media.openAudio(track, std::move(done));
    done(std::unexpected(SourceError::Unsupported));
}

void DummyVideoSource::audio(std::int64_t start, std::int64_t count, AudioReady done)
{
    if (!m_dummy)
        return m_media.audio(start, count, std::move(done));
    done(std::unexpected(SourceError::NotOpen));
}

void DummyVideoSource::beginPcm(std::int64_t start, std::int64_t count, int outRate, int outChannels, PcmBegun done)
{
    if (!m_dummy)
        return m_media.beginPcm(start, count, outRate, outChannels, std::move(done));
    done(std::unexpected(SourceError::NotOpen));
}

void DummyVideoSource::nextPcm(std::int64_t maxFrames, PcmReady done)
{
    if (!m_dummy)
        return m_media.nextPcm(maxFrames, std::move(done));
    done(std::unexpected(SourceError::NotOpen));
}

void DummyVideoSource::setInputMatrix(int colorSpace, int colorRange, MatrixSet done)
{
    if (!m_dummy)
        return m_media.setInputMatrix(colorSpace, colorRange, std::move(done));
    done({});
}

void DummyVideoSource::cancelReads()
{
    m_media.cancelReads();
}

std::uint64_t DummyVideoSource::generation() const
{
    return m_dummy ? m_generation : m_media.generation();
}

std::optional<OpenFailure> DummyVideoSource::openFailure() const
{
    return m_dummyText ? std::nullopt : m_media.openFailure();
}

std::optional<int> nextChapter(const std::vector<int> &chapters, int vrtime, int &prevchap)
{
    const int size = static_cast<int>(chapters.size());
    if (size < 1)
        return std::nullopt;
    for (int j = 0; j < size; ++j) {
        const int ntime = j >= size - 1 ? INT_MAX : chapters[static_cast<std::size_t>(j + 1)];
        if (ntime > vrtime) {
            int jj = (j >= size - 1 || (j == 0 && chapters[0] >= vrtime)) ? 0 : j + 1;
            if (jj == prevchap) {
                if (jj >= size - 1)
                    jj = 0;
                else
                    ++jj;
            }
            prevchap = jj;
            return jj;
        }
    }
    return std::nullopt;
}

std::optional<int> previousChapter(const std::vector<int> &chapters, int vrtime, int &prevchap)
{
    const int size = static_cast<int>(chapters.size());
    if (size < 1)
        return std::nullopt;
    for (int j = 0; j < size; ++j) {
        const int ntime = j >= size - 1 ? INT_MAX : chapters[static_cast<std::size_t>(j + 1)];
        if (ntime > vrtime) {
            int jj = j < 1 ? 0 : j;
            if (jj == prevchap) {
                if (jj < 1)
                    jj = size - 1;
                else
                    --jj;
            }
            prevchap = jj;
            return jj;
        }
    }
    return std::nullopt;
}

int currentChapter(const std::vector<int> &chapters, int nowMs)
{
    const int size = static_cast<int>(chapters.size());
    for (int j = 0; j < size; ++j) {
        const int ntime = j >= size - 1 ? INT_MAX : chapters[static_cast<std::size_t>(j + 1)];
        if (ntime > nowMs)
            return j;
    }
    return -1;
}

std::string audioStreamLabel(const AudioTrack &track)
{
    std::string name = track.hasName ? track.name : std::string();
    std::string language = track.hasLanguage ? track.language : std::string();
    if (track.hasLanguage) {
        // "Name [code]" in the language field (ProviderFFMS2.cpp:218-226)
        const auto start = language.rfind('[');
        const auto end = language.rfind(']');
        if (start != std::string::npos && end != std::string::npos) {
            if (name.empty() && start > 1)
                name = language.substr(0, start);
            language = language.substr(start + 1, end - (start + 1));
        }
    }
    std::string description;
    if (track.hasName)
        description = name;
    if (track.hasLanguage) {
        if (track.hasName)
            description += " [";
        description += language;
        if (track.hasName)
            description += "]";
    }
    if (description.empty())
        description = "Untitled";
    return "A: " + description + " (" + track.codec + ")";
}

std::vector<int> keyframesWithoutVideo(const std::vector<int> &frames)
{
    const LegacyTimebase filmRate({}, 24000.f / 1001.f);
    std::vector<int> ms;
    ms.reserve(frames.size());
    for (const int frame : frames)
        ms.push_back(filmRate.msAt(frame));
    std::sort(ms.begin(), ms.end());
    ms.erase(std::unique(ms.begin(), ms.end()), ms.end());
    return ms;
}

int keyframeSnapWithoutVideo(int keyMs)
{
    const int frameTime = keyMs - 21;
    return (frameTime / 10) * 10; // ZEROIT (SubsDialogue.h:20)
}

} // namespace hikari::application
