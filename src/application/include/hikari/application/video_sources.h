#pragma once

// V3: the Video panel's sources, tracks and chapters without Qt (legacy
// VideoBox, DummyVideo, ProviderDummy and KeyframeLoader at 20d647c4):
// - the folder walk of VIDEO_PREVIOUS_FILE / VIDEO_NEXT_FILE,
// - the dummy video (GLOBAL_OPEN_DUMMY_VIDEO): the dialog's text, its
//   parsing, frames and timecodes, and a source port serving it,
// - VIDEO_PREVIOUS_CHAPTER / VIDEO_NEXT_CHAPTER and the chapter menu's mark,
// - the stream menu's audio entries,
// - keyframes opened without a video.

#include "hikari/application/display_audio_port.h"
#include "hikari/application/indexed_source.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// VideoBox::NextFile's extensions (VideoBox.cpp:719-723), compared
// lower-cased: avi, mp4, mkv, ogm, wmv, asf, rmvb, rm, 3gp, ts, m2ts, mpg,
// mpeg (avs left out, as legacy's comment says).
bool isNextFileVideo(std::string_view path);

// What VIDEO_PREVIOUS_FILE / VIDEO_NEXT_FILE do (VideoBox::NextFile,
// VideoBox.cpp:682-739).
struct NextFileStep {
    enum class Kind {
        Open,    // open `path` (legacy Hikari->OpenFile); a failed open ends the walk
        Restart, // no file that way: Seek(0) then Pause(false), which toggles play
    };
    Kind kind = Kind::Restart;
    std::string path;
};

// VideoBox::NextFile over `files`, the folder's files (wxDir::GetAllFiles with
// wxDIR_FILES: hidden files left out, in the platform's own listing order,
// unsorted) as full paths, or nullopt when the folder could not be opened:
// the last listing is walked again (legacy's member `files`, cleared only
// when wxDir opened the folder). The current file is found by exact
// comparison; when it is not there the walk starts from the position the
// last step remembered (legacy's member actualFile, 0 at first).
class NextFileWalker {
public:
    NextFileStep step(std::optional<std::vector<std::string>> files, const std::string &current, bool next);
    int position() const { return m_actualFile; }

private:
    std::vector<std::string> m_files;
    int m_actualFile = 0;
};

// The dummy video (legacy "?dummy:fps:frames:width:height:r:g:b:c").
struct DummyVideo {
    float fps = 0;
    int frames = 0;
    int width = 0, height = 0;
    std::uint8_t red = 0, green = 0, blue = 0;
    bool pattern = false;
};

inline constexpr char kDummyVideoPrefix[] = "?dummy";
// Legacy treats any video path starting "?" as the dummy (VideoBox::LoadVideo,
// VideoBox.cpp:303-304; Notebook.cpp:1175 accepts "?dummy" as Video File).
bool isDummyVideo(std::string_view path);

// DummyVideo::GetDummyText (DummyVideo.cpp:118-140): the frame rate typed or
// chosen (15 to 120, else "Invalid FPS value."), the duration in ms, the
// size and colour; the text names "%f" frames per second and
// duration / (1000 / fps) frames (truncated).
enum class DummyVideoError { InvalidFps };
std::expected<std::string, DummyVideoError> dummyVideoText(std::string_view fps, int durationMs, int width, int height,
                                                           std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                                           bool pattern);
// The dialog's "This gives %i frames" (DummyVideo.cpp:94-97): computed once
// from the default 0:25:00.00 at 23.976 and never updated.
int dummyVideoDialogFrames();

// ProviderDummy::ParseDummyData (ProviderDummy.cpp:144-223): nullopt for a
// text it refuses (too few fields, a frame rate that does not parse, fewer
// than one frame, a zero size) and for a negative size (V3-dummy-negative-size).
std::optional<DummyVideo> parseDummyVideo(std::string_view text);

// ProviderDummy::GenerateFrame (ProviderDummy.cpp:98-142): one BGRA frame,
// top-down, every frame the same: the colour, or the checkerboard of 10 px
// squares in the colour and the colour 24 lighter (HSL, Aegisub's swapped
// blue and green kept).
std::vector<std::byte> dummyVideoFrame(const DummyVideo &video);

// Timebase::FromFps(fps, frames) (ProviderDummy::GenerateTimecodes): frame n
// at n * (1000 / fps) ms, truncated.
std::vector<int> dummyVideoTimecodes(const DummyVideo &video);

// An IndexedSourcePort that serves dummy videos and passes every other path
// to the media helper's port (legacy's RendererFFMS2 made a ProviderDummy for
// a "?" path, ProviderFFMS2 otherwise). Its results arrive at once; it has no
// audio (ProviderDummy::GetBuffer writes silence the box never asks for).
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
    void cancelReads() override;
    std::uint64_t generation() const override;
    std::optional<OpenFailure> openFailure() const override;

    bool dummyOpen() const { return m_dummy.has_value(); }

private:
    IndexedSourcePort &m_media;
    std::optional<DummyVideo> m_dummy;
    // the latest open was a dummy's text, refused or not: a refused text has
    // no failure of the helper's to report (legacy ProviderDummy logs nothing)
    bool m_dummyText = false;
    std::vector<std::byte> m_frame;
    std::uint64_t m_generation = 0; // the dummy's; above the helper's while a dummy is open
};

// VideoBox::NextChap (VideoBox.cpp:1420-1444) over the chapters' start times
// in ms (legacy chapter.time) at `nowMs` (Tell()); `previous` is legacy's
// prevchap (-1 after a video loads, VideoBox.cpp:287) and becomes the chapter
// jumped to. nullopt without chapters.
std::optional<int> nextChapter(const std::vector<int> &startsMs, int nowMs, int &previous);
// VideoBox::PrevChap (VideoBox.cpp:1446-1468).
std::optional<int> previousChapter(const std::vector<int> &startsMs, int nowMs, int &previous);
// The chapter menu's dot (VideoBox.cpp:1010-1016 with Menu.cpp:764-767): the
// first chapter whose next chapter starts after `nowMs` (the last's next is
// INT_MAX), so the first chapter before playback reaches it; -1 without.
int currentChapter(const std::vector<int> &startsMs, int nowMs);

// The stream menu's audio entries: the audio tracks of the open video (the
// helper's probe), "A: " and legacy's track description (ProviderFFMS2::Init,
// ProviderFFMS2.cpp:213-247: the name, " [language]" after a name, the
// language alone, else "Untitled", then " (codec)").
std::string audioStreamLabel(const AudioTrack &track);

// VideoBox::OpenKeyframes with an audio box and no video
// (VideoBox.cpp:1730-1731): the file's frames counted at 24000/1001 fps,
// in ms, sorted without duplicates (Timebase::SetKeyframes).
std::vector<int> keyframesWithoutVideo(const std::vector<int> &frames);
// AudioDisplay::GetBoundarySnap without video (AudioDisplay.cpp:2506-2509):
// half of a 23.976 fps frame earlier, ZEROIT.
int keyframeSnapWithoutVideo(int keyMs);

} // namespace hikari::application
