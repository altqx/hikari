#pragma once

// Indexed video for the editing target (I1; docs/qt/media.md). Opening
// indexes the source; showing a frame decodes it, renders the subtitle
// overlay at that frame's start time and submits both to the presenter.
// Seeking to a Line shows the first frame at or after its start (the first
// sampled frame showing it). A newer request supersedes an older one whose
// frame has not arrived; results from an older source generation are dropped.
// Subtitle editing never depends on the video: a missing or failed source
// only leaves the session Failed or Closed.
//
// Playback (V1) goes through the general player on the same file: it starts
// from the shown frame, each frame it delivers is shown with the overlay
// rendered at that frame's time, and pausing resolves the last delivered
// time through the indexed timestamp table to show that exact indexed frame
// (the accepted I4 handoff: frame identity on pause, never the player's
// approximate position).

#include "hikari/application/general_player.h"
#include "hikari/application/indexed_source.h"
#include "hikari/application/legacy_timebase.h"
#include "hikari/application/presenter.h"
#include "hikari/application/subtitle_render.h"
#include "hikari/application/video_matrix.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/frame_timeline.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

class VideoSession {
public:
    enum class State { Closed, Opening, Ready, Failed };

    VideoSession(IndexedSourcePort &source, SubtitleRendererPort &renderer);

    // The panel's presenter; it may arrive after the video opened, and then
    // receives the frame already shown.
    void setPresenter(PresenterPort *presenter);
    void setObserver(std::function<void()> changed) { m_observer = std::move(changed); }

    // A1: `index` carries legacy's chosen audio track and index file.
    void open(const std::string &path, IndexRequest index = {});
    void close();
    // V3: the indexing's progress while Opening (legacy ProgressSink "Indexing
    // video"): done of total, nullopt before the first report.
    std::optional<std::pair<std::int64_t, std::int64_t>> indexingProgress() const { return m_progress; }
    // V3: the progress window's Cancel (FFMS_CancelIndexing through the
    // helper): the open ends and the panel has no video, as legacy's
    // cancelled provider left the VideoBox without a renderer.
    void cancelOpen();
    // V3: why the latest open failed (the source's stage and FFMS2 text).
    const std::optional<OpenFailure> &openFailure() const { return m_openFailure; }
    // V3: a dummy video (legacy "?dummy:..." path, ProviderDummy).
    bool dummy() const;
    // The overlay's subtitles (a Document's encoded ASS bytes). The shown
    // frame is rendered again.
    void setSubtitles(std::vector<std::byte> script);
    // Y6: fonts given to the renderer with the subtitles from the next
    // setSubtitles on (EXTERNAL_FONTS_DIRECTORY's fonts).
    void setFonts(std::vector<FontLease> fonts) { m_fonts = std::move(fonts); }
    // V4: the Document's YCbCr matrix (documentVideoMatrix). An open takes
    // it as legacy ProviderFFMS2::Init did; a change afterwards is legacy
    // SetColorSpace, and the shown frame is decoded again (legacy rendered
    // it again while paused). A converter that refuses keeps the old matrix
    // and logs legacy's "Cannot change YCbCr matrix".
    void setMatrix(std::string matrix);
    const std::string &matrix() const { return m_docMatrix; }
    const LegacyColourMatrix &colourMatrix() const { return m_colour; }
    void setLog(std::function<void(const std::string &)> log) { m_log = std::move(log); }
    void seekTo(core::DocumentTime start);
    bool step(int frames); // false at either end or without video
    void showFrame(int index);
    // Decodes a frame without showing it (automation get_frame), with the
    // overlay rendered at its start when asked; nullptr without video.
    void requestFrame(int index, bool withSubtitles,
                      std::function<void(std::shared_ptr<const IndexedFrame>)> done);

    // V1: the general player (none: no playback). W1: another player (the
    // DirectShow adapter chosen in the settings) replaces it: a playing
    // video pauses first, and the new player opens the video at the next play.
    void setGeneralPlayer(GeneralPlayerPort *player);
    GeneralPlayerPort *generalPlayer() const { return m_player; }
    // W1: opens the video in the player without playing (legacy built its
    // DirectShow graph when the video loaded, which its Filters menu lists);
    // nothing when the player has it open already or is opening it.
    void preparePlayer();
    bool playerHasVideo() const { return m_player && !m_path.empty() && m_playerPath == m_path; }
    // W1: preparePlayer's open of this video failed; the player is not asked
    // again (to open, prepare or play) until the video is loaded again or the
    // player changes.
    bool playerFailed() const { return !m_path.empty() && m_failedPath == m_path; }
    bool play();  // from the shown frame; false without video or player
    bool pause(); // shows the indexed frame of the last delivered time
    bool stop();  // pauses, then shows the first frame (legacy Seek(0))
    bool playing() const { return m_playing; }
    // V3: legacy VideoBox::Tell(): while playing the last delivered frame's
    // time, else the shown frame's start, in ms (0 without video).
    int tell() const;
    // V3: legacy Seek(ms, true, SEEK_NO_SNAP) (the chapters): the frame at or
    // after the time (0 at or before 0), clamped; while playing the player
    // goes on from there.
    bool seekToMs(int ms);
    // V3: legacy Seek(0) then Pause(false) (VideoBox::NextFile with no file
    // that way): the first frame, and play toggled (a paused video plays from
    // its start, a playing one pauses there).
    bool restartToggled();
    // V3: the stream menu. The video's audio tracks in container order (the
    // general player's numbering) and the one general playback plays;
    // choosing another switches the playing player at once and every later
    // play, through each pause's handoff to the indexed frame and back.
    const std::vector<int> &audioTracks() const { return m_audioTracks; }
    int playbackAudioTrack() const { return m_state == State::Ready ? m_audioOrdinal : -1; }
    bool selectPlaybackAudioTrack(int ordinal);
    // A4: legacy RendererVideo::PlayLine (GLOBAL_PLAY_ACTUAL_LINE with
    // Timebase::PlayEndBefore): from the frame at `startMs` (FrameAt) until a
    // frame at or after the start of the frame before the one at `endMs` is
    // shown, then paused (PlaybackReachedEnd, OnEndFile). Nothing when the
    // start is not before that end or not before the video's duration (its
    // last frame's time); the end is clamped to the duration, an end of 0
    // plays on; a playing video is paused first.
    bool playLine(int startMs, int endMs);
    // A1: legacy's Stopped state: Stop while playing, until the next Play
    // (legacy marks the paused video's frame in the audio box only when Paused).
    bool stopped() const { return m_stopped; }
    // A frame the general player delivered, converted to BGRA by the UI.
    void generalFrame(IndexedFrame frame, std::int64_t startUs);

    // V2: legacy VideoBox::Seek and keyframe navigation, from the shown frame.
    // A start time shows the frame at or after it (Timebase::FrameAt, 0 at or
    // before 0); an end time the frame showing 1 ms before it
    // (FrameShownAt(end - 1)); both are clamped to the video.
    void seekToEnd(core::DocumentTime end);
    bool seekBy(std::int64_t ms);          // VIDEO_5_SECONDS_* / VIDEO_MINUTE_*
    bool nextKeyframe();                   // wraps to the first after the last
    bool previousKeyframe();               // wraps to the last before the first
    bool isKeyframe(int index) const;
    const std::vector<int> &keyframes() const { return m_keyframes; }
    // GLOBAL_OPEN_KEYFRAMES: a keyframe file's frames replace the video's own
    // (legacy Timebase::SetKeyframes) until the next video opens.
    void setKeyframes(std::vector<int> frames);
    // The legacy Timebase over this video (empty without one).
    LegacyTimebase legacyTimebase() const;
    // V6: legacy VideoBox::Tell (RendererVideo::m_Time): the shown frame's
    // time in whole ms while paused or stopped, the last delivered frame's
    // while playing; 0 without video.
    int tellMs() const;
    // V6: legacy GetDuration (FFMS2's LastTime): the last frame's start in ms.
    int durationMs() const;
    // V6: legacy VideoBox::Seek while playing (RendererFFMS2::SetPosition):
    // playback goes on from the frame a start time (or an end time) shows.
    // While not playing it is seekTo / seekToEnd.
    void seekKeepPlaying(core::DocumentTime time, bool startTime);
    // V6: legacy Play(); Pause() on a Stopped video (SetVideoLineTime): it is
    // Paused where it stands.
    void unstop() { m_stopped = false; }

    State state() const { return m_state; }
    const std::string &path() const { return m_path; }
    // A1: the opened video has an audio track (legacy GetSampleRate() > 0).
    bool hasAudio() const { return m_state == State::Ready && m_hasAudio; }
    // A1: the video's audio track: the one its open chose (legacy's provider
    // track, chooser or ACCEPTED_AUDIO_STREAM), else the first; -1 without.
    // General playback plays it, as legacy played the box's track.
    int audioTrack() const { return m_state == State::Ready ? m_audioTrack : -1; }
    // A1: the open indexed now instead of reading legacy's index file.
    bool newIndex() const { return m_newIndex; }
    // A1: the index handed over in a temporary file when the index file could
    // not be written (SourceTimeline::handoffIndexFile); empty otherwise.
    const std::string &indexHandoff() const { return m_indexHandoff; }
    std::optional<SourceError> error() const { return m_error; }
    int frameCount() const { return static_cast<int>(m_starts.size()); }
    // T1: the open video's frame size and SAR (the source's, else the first
    // frame shown's with no SAR); invalid without one. It stays when a
    // frame fails to decode, so the visual tools keep working.
    visual::SourceGeometry sourceGeometry() const;
    std::optional<int> requestedFrame() const { return m_requested; }
    std::optional<int> shownFrame() const { return m_shown ? std::optional(m_shown->index) : std::nullopt; }
    std::optional<core::DocumentTime> frameStart(int index) const;
    // What was last submitted, and how the presenter answered.
    std::shared_ptr<const IndexedFrame> lastFrame() const { return m_shown; }
    std::shared_ptr<const OverlayFrame> lastOverlay() const { return m_overlay; }
    std::optional<PresentResult> lastPresent() const { return m_lastPresent; }

private:
    void notify();
    void present();
    void render();

    IndexedSourcePort &m_source;
    SubtitleRendererPort &m_renderer;
    std::vector<FontLease> m_fonts; // Y6
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
    bool m_hasSubtitles = false;
    std::optional<PresentResult> m_lastPresent;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    // V1
    GeneralPlayerPort *m_player = nullptr;
    std::string m_playerPath; // what the player has open
    bool m_preparing = false;  // W1: preparePlayer's open is pending
    std::string m_failedPath;  // W1: preparePlayer could not open it
    bool m_playing = false;
    bool m_stopped = false;
    std::uint64_t m_playEpoch = 0;
    std::optional<std::int64_t> m_lastGeneralUs;
    int m_playEndMs = 0; // A4: legacy m_PlayEndTime (0: none)
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
    std::optional<std::pair<std::int64_t, std::int64_t>> m_progress; // V3
    std::optional<OpenFailure> m_openFailure;                          // V3
    std::vector<int> m_audioTracks;                                    // V3
    // V4
    std::string m_docMatrix;
    LegacyColourMatrix m_colour;
    std::function<void(const std::string &)> m_log;
    void applyInputMatrix(LegacyColourMatrix::Input input, std::optional<LegacyColourMatrix::Change> change);
};

} // namespace hikari::application
