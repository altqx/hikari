#pragma once

#include "hikari/application/general_player.h"
#include "hikari/application/indexed_source.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/application/playback_pipeline.h"
#include "hikari/application/presenter.h"
#include "hikari/application/subtitle_render.h"
#include "hikari/application/video_matrix.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/frame_timeline.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hikari::application {

// A newer frame request supersedes an older one; results from an older source
// generation are dropped.
class VideoSession {
public:
    enum class State { Closed, Opening, Ready, Failed };

    VideoSession(IndexedSourcePort &source, SubtitleRendererPort &renderer);

    // May come after open; it then receives the frame already shown.
    void setPresenter(PresenterPort *presenter);
    const PresenterPort *presenter() const { return m_presenter; }
    void setObserver(std::function<void()> changed) { m_observer = std::move(changed); }

    // `index`: the chosen audio track and index file.
    void open(const std::string &path, IndexRequest index = {});
    void close();
    // Done of total while Opening; nullopt before the first report.
    std::optional<std::pair<std::int64_t, std::int64_t>> indexingProgress() const { return m_progress; }
    // The open ends with no video.
    void cancelOpen();
    const std::optional<OpenFailure> &openFailure() const { return m_openFailure; }
    // A "?dummy:..." path.
    bool dummy() const;
    // Encoded ASS bytes; the shown frame is rendered again.
    void setSubtitles(std::vector<std::byte> script);
    // Scripts given to the renderer, each one a parse.
    std::uint64_t subtitleUpdates() const { return m_subtitleUpdates; }
    // Takes effect from the next setSubtitles.
    void setFonts(std::vector<FontLease> fonts) { m_fonts = std::move(fonts); }
    // The Document's YCbCr matrix; a change decodes the shown frame again. A
    // converter that refuses keeps the old matrix and logs it.
    void setMatrix(std::string matrix);
    const std::string &matrix() const { return m_docMatrix; }
    const LegacyColourMatrix &colourMatrix() const { return m_colour; }
    void setLog(std::function<void(const std::string &)> log) { m_log = std::move(log); }
    // setSubtitles shows them again.
    void closeSubtitles();
    // Overlays at the picture's on-screen size in device pixels (nullopt: the
    // video's size). requestFrame frames stay at the video's size.
    void setScreenSize(std::optional<std::pair<int, int>> size);
    bool hasSubtitles() const { return m_hasSubtitles; }
    void seekTo(core::DocumentTime start);
    bool step(int frames); // false at either end, without video or while playing
    void showFrame(int index);
    // Decodes without showing; the overlay at its start when asked; nullptr without video.
    void requestFrame(int index, bool withSubtitles,
                      std::function<void(std::shared_ptr<const IndexedFrame>)> done);

    // nullptr: no playback. A playing video pauses first; the new player opens
    // the video at the next play.
    void setGeneralPlayer(GeneralPlayerPort *player);
    GeneralPlayerPort *generalPlayer() const { return m_player; }
    // Opens without playing; nothing when the player has it open or is opening it.
    void preparePlayer();
    bool playerHasVideo() const { return m_player && !m_path.empty() && m_playerPath == m_path; }
    // The player is not asked again until the video is loaded again or the player changes.
    bool playerFailed() const { return !m_path.empty() && m_failedPath == m_path; }
    bool play();  // from the shown frame; false without video or player
    bool pause(); // shows the indexed frame of the last delivered time
    bool stop();  // pauses, then shows the first frame
    bool playing() const { return m_playing; }
    // Bumped by each play, and each seek or pause of playback.
    std::uint64_t playEpoch() const { return m_playEpoch; }
    // Ms: while playing the last delivered frame's time, else the shown
    // frame's start; 0 without video.
    int tell() const;
    // The frame at or after the time (0 at or before 0), clamped; while
    // playing the player goes on from there.
    bool seekToMs(int ms);
    // The first frame with play toggled: a paused video plays from its start,
    // a playing one pauses there.
    bool restartToggled();
    // In container order (the general player's numbering); selecting one
    // switches a playing player at once.
    const std::vector<int> &audioTracks() const { return m_audioTracks; }
    int playbackAudioTrack() const { return m_state == State::Ready ? m_audioOrdinal : -1; }
    bool selectPlaybackAudioTrack(int ordinal);
    // From the frame at `startMs` until a frame at or after the start of the
    // frame before the one at `endMs` shows, then paused. Nothing when the
    // start is not before that end or the duration; the end is clamped to the
    // duration, 0 plays on; a playing video pauses first. `loop` replays from
    // the start each time the end shows, until a pause, stop or seek.
    bool playLine(int startMs, int endMs, bool loop = false);
    bool looping() const { return m_playing && m_playEndMs > 0 && m_loop; }
    // The span a loop plays (its start and end in ms), whether or not it plays.
    std::pair<int, int> loopSpan() const { return {m_loopStartMs, m_loopEndMs}; }
    // From a stop while playing until the next play.
    bool stopped() const { return m_stopped; }
    // Already BGRA; shown and its overlay rendered at once.
    void generalFrame(IndexedFrame frame, std::int64_t startUs);
    // `post` runs the pipeline's results on this thread. Without one a frame
    // is handled before submitGeneralFrame returns.
    void setFrameWorker(PlaybackPipeline::Post post);
    // Only the newest waits; a result older than the frame shown, or from
    // before a pause, stop or seek, is dropped.
    void submitGeneralFrame(std::int64_t startUs, std::optional<std::int64_t> endUs, PlaybackPipeline::Make make);
    std::optional<PlaybackPipeline::Counters> pipelineCounters() const;
    // The last input matrix set, else the source's own; nullopt without video.
    std::optional<LegacyColourMatrix::Input> playerColour() const;
    // Then player frames are better handed over as Y'CbCr planes.
    bool presenterConvertsYcbcr() const { return m_presenter && m_presenter->convertsYcbcr(); }

    // From the requested frame, or the player's while playing (playback goes
    // on from the target). A start time shows the frame at or after it (0 at
    // or before 0); an end time the frame showing 1 ms before it; both clamped.
    void seekToEnd(core::DocumentTime end);
    bool seekBy(std::int64_t ms);
    bool nextKeyframe();                   // wraps to the first after the last
    bool previousKeyframe();               // wraps to the last before the first
    bool isKeyframe(int index) const;
    const std::vector<int> &keyframes() const { return m_keyframes; }
    // Replaces the video's own until the next video opens.
    void setKeyframes(std::vector<int> frames);
    // Empty without video.
    LegacyTimebase legacyTimebase() const;
    // The shown frame's time in whole ms, the last delivered frame's while
    // playing; 0 without video.
    int tellMs() const;
    // The last frame's start.
    int durationMs() const;
    // While not playing, seekTo / seekToEnd.
    void seekKeepPlaying(core::DocumentTime time, bool startTime);
    // A stopped video becomes paused where it stands.
    void unstop() { m_stopped = false; }

    State state() const { return m_state; }
    const std::string &path() const { return m_path; }
    bool hasAudio() const { return m_state == State::Ready && m_hasAudio; }
    // The one the open chose, else the first; -1 without.
    int audioTrack() const { return m_state == State::Ready ? m_audioTrack : -1; }
    // The open indexed instead of reading an index file.
    bool newIndex() const { return m_newIndex; }
    // A temporary index file when the index file could not be written; empty otherwise.
    const std::string &indexHandoff() const { return m_indexHandoff; }
    std::optional<SourceError> error() const { return m_error; }
    int frameCount() const { return static_cast<int>(m_starts.size()); }
    // Float numerator / float denominator; 0 without video.
    float legacyFps() const { return m_state == State::Ready ? m_legacyFps : 0.f; }
    // The source's, else the first shown frame's; invalid without video. Kept
    // when a frame fails to decode.
    visual::SourceGeometry sourceGeometry() const;
    std::optional<int> requestedFrame() const { return m_requested; }
    // While playing the indexed frame holding the last delivered frame's
    // start (clamped to the video), else the requested frame.
    std::optional<int> currentFrame() const;
    // "The video's frame" for editing features: the shown indexed frame while
    // paused, else currentFrame().
    std::optional<int> reportedFrame() const;
    // While playing, playback goes on from its start. False outside the video.
    bool seekFrame(int index);
    std::optional<int> shownFrame() const { return m_shown ? std::optional(m_shown->index) : std::nullopt; }
    std::optional<core::DocumentTime> frameStart(int index) const;
    // nullopt past the last frame's start.
    std::optional<int> frameAtOrAfter(core::DocumentTime time) const;
    std::shared_ptr<const IndexedFrame> lastFrame() const { return m_shown; }
    std::shared_ptr<const OverlayFrame> lastOverlay() const { return m_overlay; }
    std::optional<PresentResult> lastPresent() const { return m_lastPresent; }

private:
    // Clamped to the video's frames.
    std::optional<int> frameHolding(std::int64_t us) const;
    std::optional<int> jumpOrigin() const;
    void jumpTo(int frame);
    void notify();
    void present(std::optional<std::optional<PixelRect>> overlayBounds = std::nullopt);
    void render();
    void subtitlesChanged();
    void showGeneral(PlaybackPipeline::Output out);

    IndexedSourcePort &m_source;
    SubtitleRendererPort &m_renderer;
    std::vector<FontLease> m_fonts;
    PresenterPort *m_presenter = nullptr;
    std::function<void()> m_observer;
    State m_state = State::Closed;
    std::string m_path;
    std::optional<SourceError> m_error;
    std::vector<core::DocumentTime> m_starts;
    std::optional<core::FrameTimeline> m_timeline;
    std::optional<int> m_requested;
    std::optional<core::DocumentTime> m_pendingSeek;
    std::uint64_t m_request = 0; // the newest frame request; older answers are dropped
    std::shared_ptr<const IndexedFrame> m_shown;
    std::shared_ptr<const OverlayFrame> m_overlay;
    std::optional<std::pair<int, int>> m_screen;
    bool m_hasSubtitles = false;
    std::uint64_t m_subtitleUpdates = 0;
    float m_legacyFps = 0.f;
    std::optional<PresentResult> m_lastPresent;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    GeneralPlayerPort *m_player = nullptr;
    std::string m_playerPath; // what the player has open
    bool m_preparing = false;  // preparePlayer's open is pending
    std::string m_failedPath;  // preparePlayer could not open it
    bool m_playing = false;
    bool m_stopped = false;
    std::uint64_t m_playEpoch = 0;
    std::optional<std::int64_t> m_lastGeneralUs;
    std::uint64_t m_lastGeneralEpoch = 0; // the play epoch m_lastGeneralUs belongs to
    std::uint64_t m_subtitleSerial = 0;   // each prepare and close of the subtitles
    int m_playEndMs = 0; // 0: none
    bool m_loop = false;
    int m_loopStartMs = 0, m_loopEndMs = 0;
    bool startPlayback(std::int64_t fromUs);
    std::optional<core::DocumentTime> m_overlayTime; // a general frame's time
    std::vector<int> m_keyframes;
    bool m_hasAudio = false;
    int m_audioTrack = -1;
    int m_audioOrdinal = -1; // the track among the audio tracks (the general player's numbering)
    bool m_newIndex = true;
    std::string m_indexHandoff;
    double m_fps = 0;
    visual::SourceGeometry m_geometry; // the source's, from its timeline
    std::optional<std::pair<std::int64_t, std::int64_t>> m_progress;
    std::optional<OpenFailure> m_openFailure;
    std::vector<int> m_audioTracks;
    std::string m_docMatrix;
    LegacyColourMatrix m_colour;
    std::optional<LegacyColourMatrix::Input> m_inputMatrix; // the last one set and not refused
    std::function<void(const std::string &)> m_log;
    void applyInputMatrix(LegacyColourMatrix::Input input, std::optional<LegacyColourMatrix::Change> change);
    // MUST KILL: must stay the last member so its worker stops before what it reads
    std::unique_ptr<PlaybackPipeline> m_pipeline;
};

}
