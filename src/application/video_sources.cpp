#include "hikari/application/video_sources.h"

#include "hikari/application/legacy_timebase.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
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

// The whole text when there is no dot.
std::string_view afterLastDot(std::string_view path)
{
    const auto dot = path.rfind('.');
    return dot == std::string_view::npos ? path : path.substr(dot + 1);
}

// The whole text must be one C-locale number.
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

int wxAtoi(std::string_view text)
{
    return std::atoi(std::string(text).c_str());
}

std::uint8_t clipColour(int v)
{
    return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

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

int saturate(double v)
{
    return v >= static_cast<double>(INT_MAX) ? INT_MAX : static_cast<int>(v);
}

}

bool isNextFileVideo(std::string_view path)
{
    const std::string ext = lower(afterLastDot(path));
    static constexpr std::string_view kExtensions[] = {"avi", "mp4", "mkv", "ogm", "wmv", "asf", "rmvb",
                                                        "rm",  "3gp", "ts",  "m2ts", "mpg", "mpeg"};
    return std::find(std::begin(kExtensions), std::end(kExtensions), ext) != std::end(kExtensions);
}

std::string nextFileFolder(std::string_view path, PathFormat format)
{
    const bool dos = format == PathFormat::Dos;
    const auto isSeparator = [dos](char c) { return c == '/' || (dos && c == '\\'); };
    const char separator = dos ? '\\' : '/';
    // Windows volumes: "\\?\" up to the next backslash, "\\server", a drive letter
    std::string_view volume;
    if (dos) {
        if (path.starts_with("\\\\?\\"))
            volume = path.substr(0, path.find('\\', 4));
        else if (path.size() > 2 && isSeparator(path[0]) && isSeparator(path[1]) && !isSeparator(path[2])) {
            std::size_t end = 3;
            while (end < path.size() && !isSeparator(path[end]))
                ++end;
            volume = path.substr(0, end);
        } else if (path.size() >= 2 && path[1] == ':'
                   && ((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')))
            volume = path.substr(0, 2);
    }
    std::string_view rest = path.substr(volume.size());
    std::size_t last = std::string_view::npos;
    for (std::size_t i = 0; i < rest.size(); ++i)
        if (isSeparator(rest[i]))
            last = i;
    std::string out(volume);
    if (last == std::string_view::npos)
        return out;
    const std::string_view folder = rest.substr(0, last + 1);
    if (isSeparator(folder.front()))
        out += separator;
    bool first = true;
    for (std::size_t i = 0; i < folder.size();) {
        std::size_t end = i;
        while (end < folder.size() && !isSeparator(folder[end]))
            ++end;
        if (end > i) {
            if (!first)
                out += separator;
            out.append(folder.substr(i, end - i));
            first = false;
        }
        i = end + 1;
    }
    return out;
}

namespace {

// A byte that does not start a well-formed sequence is taken alone, as its own value.
char32_t nextCodePoint(std::string_view text, std::size_t &i)
{
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
    const unsigned char lead = byte(i);
    const int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    if (length > 1 && i + length <= text.size()) {
        char32_t c = lead & (0x7F >> length);
        bool valid = true;
        for (int k = 1; k < length; ++k) {
            const unsigned char next = byte(i + k);
            valid = valid && (next & 0xC0) == 0x80;
            c = (c << 6) | (next & 0x3F);
        }
        if (valid) {
            i += length;
            return c;
        }
    }
    ++i;
    return lead;
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

}

int naturalNameCompare(std::string_view a, std::string_view b, const NameLower &lower)
{
    const auto lowered = [&](char32_t c) -> char32_t {
        if (lower)
            return lower(c);
        return c >= U'A' && c <= U'Z' ? c + (U'a' - U'A') : c;
    };
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            std::size_t endA = i, endB = j;
            while (endA < a.size() && isDigit(a[endA]))
                ++endA;
            while (endB < b.size() && isDigit(b[endB]))
                ++endB;
            while (i < endA && a[i] == '0')
                ++i;
            while (j < endB && b[j] == '0')
                ++j;
            if (endA - i != endB - j)
                return endA - i < endB - j ? -1 : 1;
            const int digits = a.substr(i, endA - i).compare(b.substr(j, endB - j));
            if (digits != 0)
                return digits < 0 ? -1 : 1;
            i = endA;
            j = endB;
            continue;
        }
        const char32_t ca = lowered(nextCodePoint(a, i));
        const char32_t cb = lowered(nextCodePoint(b, j));
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    const bool leftA = i < a.size(), leftB = j < b.size();
    if (leftA != leftB)
        return leftA ? 1 : -1;
    const int plain = a.compare(b);
    return plain < 0 ? -1 : plain > 0 ? 1 : 0;
}

NextFileStep NextFileWalker::step(std::optional<std::vector<std::string>> listing, const std::string &current, bool next)
{
    if (listing) {
        m_files = std::move(*listing);
        std::sort(m_files.begin(), m_files.end(),
                  [this](const std::string &x, const std::string &y) { return naturalNameCompare(x, y, m_lower) < 0; });
    }
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
    const auto frames = dummyVideoFrames(fpsText, durationMs);
    if (!frames)
        return std::unexpected(frames.error());
    const auto fps = toCDouble(fpsText);
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, "?dummy:%f:%i:%i:%i:%i:%i:%i:%s", static_cast<double>(static_cast<float>(*fps)),
                  *frames, width, height, red, green, blue, pattern ? "c" : "");
    return std::string(buffer);
}

std::expected<int, DummyVideoError> dummyVideoFrames(std::string_view fpsText, int durationMs)
{
    const auto fps = toCDouble(fpsText);
    if (!fps || *fps < 15 || *fps > 120)
        return std::unexpected(DummyVideoError::InvalidFps);
    const float frametime = static_cast<float>(1000.f / *fps);
    return static_cast<int>(durationMs / frametime);
}

std::optional<DummyVideo> parseDummyVideo(std::string_view text)
{
    // "?dummy", then eight fields, empty ones counted.
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
    if (video.width <= 0 || video.height <= 0)
        return std::nullopt;
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
        // blue and green swapped both ways, as Aegisub does
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
    // a float frame duration, multiplied in double
    const float duration = video.fps > 0.f ? 1000.f / video.fps : 0.f;
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(std::max(video.frames, 0)));
    for (int i = 0; i < video.frames; ++i)
        out.push_back(saturate(i * static_cast<double>(duration)));
    return out;
}

std::int64_t dummyVideoAudioFrames(const DummyVideo &video, int sampleRate)
{
    const double duration = video.fps > 0.f ? static_cast<double>(1000.f / video.fps) : 0.0;
    return std::llround(std::max(video.frames, 0) * duration * sampleRate / 1000.0);
}

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
        done(std::unexpected(SourceError::InvalidInput));
        return m_generation;
    }
    SourceTimeline t;
    t.generation = m_generation;
    t.track = 0;
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
    // MUST KILL: duplicates dummyVideoTimecodes' formula
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

void DummyVideoSource::close()
{
    m_media.close();
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
        // "Name [code]" in the language field
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
    return (frameTime / 10) * 10; // truncated to 10 ms
}

}
