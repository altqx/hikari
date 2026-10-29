#pragma once

#include "hikari/application/display_audio_port.h"
#include "hikari/application/indexed_source.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

// The extension is compared lower-cased.
bool isNextFileVideo(std::string_view path);

struct NextFileStep {
    enum class Kind {
        Open,    // open `path`; a failed open ends the walk
        Restart, // no file that way: seek to 0, then toggle play
    };
    Kind kind = Kind::Restart;
    std::string path;
};

// `path`'s folder as written, not cleaned: the volume (Dos: "C:", "\\server",
// "\\?\C:"), the root separator when absolute, then the names joined by the
// separator, empty ones dropped, "." and ".." kept; "" when `path` names no
// folder.
enum class PathFormat { Unix, Dos };
std::string nextFileFolder(std::string_view path, PathFormat format);

// One code point lowered. Empty: A-Z only.
using NameLower = std::function<char32_t(char32_t)>;

// UTF-8 code points compared through `lower`, a run of ASCII digits as one
// number ("Ep 2" before "Ep 10"); ties broken by the plain bytes, so 0 only
// for equal strings.
int naturalNameCompare(std::string_view a, std::string_view b, const NameLower &lower = {});

// `files`: full paths in one folder, walked in natural name order; nullopt
// (folder not opened) walks the last listing again. A current file not found
// by exact comparison starts from the position the last step remembered.
class NextFileWalker {
public:
    explicit NextFileWalker(NameLower lower = {}) : m_lower(std::move(lower)) {}
    NextFileStep step(std::optional<std::vector<std::string>> files, const std::string &current, bool next);
    int position() const { return m_actualFile; }

private:
    NameLower m_lower;
    std::vector<std::string> m_files;
    int m_actualFile = 0;
};

// "?dummy:fps:frames:width:height:r:g:b:c"
struct DummyVideo {
    float fps = 0;
    int frames = 0;
    int width = 0, height = 0;
    std::uint8_t red = 0, green = 0, blue = 0;
    bool pattern = false;
};

inline constexpr char kDummyVideoPrefix[] = "?dummy";
// Any path starting "?".
bool isDummyVideo(std::string_view path);

// `fps` 15 to 120, else InvalidFps; the text holds duration / (1000 / fps)
// frames, truncated.
enum class DummyVideoError { InvalidFps };
std::expected<std::string, DummyVideoError> dummyVideoText(std::string_view fps, int durationMs, int width, int height,
                                                           std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                                           bool pattern);
// duration / (1000 / fps), truncated; `fps` 15 to 120, else InvalidFps.
std::expected<int, DummyVideoError> dummyVideoFrames(std::string_view fps, int durationMs);

// nullopt for too few fields, an unreadable frame rate, fewer than one frame,
// or a size not above zero.
std::optional<DummyVideo> parseDummyVideo(std::string_view text);

// One top-down BGRA frame: the colour, or a checkerboard of 10 px squares in
// the colour and the colour 24 lighter (HSL with Aegisub's swapped blue and
// green).
std::vector<std::byte> dummyVideoFrame(const DummyVideo &video);

// Frame n at n * (1000 / fps) ms, truncated.
std::vector<int> dummyVideoTimecodes(const DummyVideo &video);

// The frames at `sampleRate` of silent audio as long as the video, rounded.
std::int64_t dummyVideoAudioFrames(const DummyVideo &video, int sampleRate);

// Serves dummy videos and passes every other path to `media`. A dummy's
// results arrive at once; it has no audio.
class DummyVideoSource : public IndexedSourcePort {
public:
    explicit DummyVideoSource(IndexedSourcePort &media) : m_media(media) {}

    std::uint64_t open(const std::string &path, Progress progress, Opened done) override;
    std::uint64_t openIndexed(const std::string &path, const IndexRequest &request, Progress progress,
                              Opened done) override;
    void cancelOpen() override;
    void frame(int index, FrameReady done) override;
    void openAudio(int track, AudioOpened done) override;
    void audio(std::int64_t start, std::int64_t count, AudioReady done) override;
    void beginPcm(std::int64_t start, std::int64_t count, int outRate, int outChannels, PcmBegun done) override;
    void nextPcm(std::int64_t maxFrames, PcmReady done) override;
    // A dummy takes any matrix.
    void setInputMatrix(int colorSpace, int colorRange, MatrixSet done) override;
    void cancelReads() override;
    void close() override;
    std::uint64_t generation() const override;
    std::optional<OpenFailure> openFailure() const override;

    bool dummyOpen() const { return m_dummy.has_value(); }

private:
    IndexedSourcePort &m_media;
    std::optional<DummyVideo> m_dummy;
    // the latest open was a dummy's text, refused or not
    bool m_dummyText = false;
    std::vector<std::byte> m_frame;
    std::uint64_t m_generation = 0; // the dummy's; above the helper's while a dummy is open
};

// `startsMs`: the chapters' starts; `previous` (-1 after a video loads)
// becomes the chapter jumped to. nullopt without chapters.
std::optional<int> nextChapter(const std::vector<int> &startsMs, int nowMs, int &previous);
std::optional<int> previousChapter(const std::vector<int> &startsMs, int nowMs, int &previous);
// The first chapter whose next one starts after `nowMs`; -1 without chapters.
int currentChapter(const std::vector<int> &startsMs, int nowMs);

// "A: ", the name, " [language]" after a name or the language alone, else
// "Untitled", then " (codec)".
std::string audioStreamLabel(const AudioTrack &track);

// The frames counted at 24000/1001 fps, in ms, sorted without duplicates.
std::vector<int> keyframesWithoutVideo(const std::vector<int> &frames);
// Half a 23.976 fps frame earlier, truncated to 10 ms.
int keyframeSnapWithoutVideo(int keyMs);

} // namespace hikari::application
