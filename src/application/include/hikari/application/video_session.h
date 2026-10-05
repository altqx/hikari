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
    // The overlay's subtitles (a Document's encoded ASS bytes). The shown
    // frame is rendered again.
    void setSubtitles(std::vector<std::byte> script);
    // Y6: fonts given to the renderer with the subtitles from the next
    // setSubtitles on (EXTERNAL_FONTS_DIRECTORY's fonts).
    void setFonts(std::vector<FontLease> fonts) { m_fonts = std::move(fonts); }
    void seekTo(core::DocumentTime start);
    bool step(int frames); // false at either end or without video
    void showFrame(int index);
    // Decodes a frame without showing it (automation get_frame), with the
    // overlay rendered at its start when asked; nullptr without video.
    void requestFrame(int index, bool withSubtitles,
                      std::function<void(std::shared_ptr<const IndexedFrame>)> done);

    // V1: the general player (none: no playback).
    void setGeneralPlayer(GeneralPlayerPort *player) { m_player = player; }
    bool play();  // from the shown frame; false without video or player
    bool pause(); // shows the indexed frame of the last delivered time
    bool stop();  // pauses, then shows the first frame (legacy Seek(0))
    bool playing() const { return m_playing; }
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
};

} // namespace hikari::application
