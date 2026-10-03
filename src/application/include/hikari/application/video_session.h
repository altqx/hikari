#pragma once

// Indexed video for the editing target (I1; docs/qt/media.md). Opening
// indexes the source; showing a frame decodes it, renders the subtitle
// overlay at that frame's start time and submits both to the presenter.
// Seeking to a Line shows the first frame at or after its start (the first
// sampled frame showing it). A newer request supersedes an older one whose
// frame has not arrived; results from an older source generation are dropped.
// Subtitle editing never depends on the video: a missing or failed source
// only leaves the session Failed or Closed.

#include "hikari/application/indexed_source.h"
#include "hikari/application/presenter.h"
#include "hikari/application/subtitle_render.h"
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

    void open(const std::string &path);
    void close();
    // The overlay's subtitles (a Document's encoded ASS bytes). The shown
    // frame is rendered again.
    void setSubtitles(std::vector<std::byte> script);
    void seekTo(core::DocumentTime start);
    bool step(int frames); // false at either end or without video
    void showFrame(int index);
    // Decodes a frame without showing it (automation get_frame), with the
    // overlay rendered at its start when asked; nullptr without video.
    void requestFrame(int index, bool withSubtitles,
                      std::function<void(std::shared_ptr<const IndexedFrame>)> done);

    State state() const { return m_state; }
    const std::string &path() const { return m_path; }
    std::optional<SourceError> error() const { return m_error; }
    int frameCount() const { return static_cast<int>(m_starts.size()); }
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
};

} // namespace hikari::application
