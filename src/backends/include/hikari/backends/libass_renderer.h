#pragma once

#include "hikari/application/subtitle_render.h"
#include "hikari/backends/overlay_blend.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace hikari::backends {

// render() may run while prepare() parses the next script.
class LibassRenderer : public application::SubtitleRendererPort {
public:
    LibassRenderer();
    ~LibassRenderer() override;

    std::expected<std::uint64_t, application::RenderError> prepare(application::RenderSnapshot snapshot) override;
    std::expected<application::OverlayFrame, application::RenderError> render(core::DocumentTime time, int width,
                                                                             int height) override;
    std::expected<application::OverlayFrame, application::RenderError>
    renderScaled(core::DocumentTime time, int width, int height, int videoWidth, int videoHeight) override;

    // render() then counts each frame's shown events and libass images into its cost.
    void countCost(bool on) { m_countCost = on; }

    // For tests: engineIdentity() changes when new fonts make a new libass renderer.
    std::size_t parsedTracks() const;
    const void *engineIdentity() const;

private:
    struct Engine;
    std::expected<application::OverlayFrame, application::RenderError> draw(core::DocumentTime time, int width,
                                                                           int height, int videoWidth, int videoHeight);

    std::mutex m_prepare;
    mutable std::mutex m_mutex;
    std::unique_ptr<Engine> m_engine;
    std::uint64_t m_generation = 0;
    bool m_redraw = true;
    std::atomic<bool> m_countCost = false;
    OverlayBuffers m_buffers;
    application::PixelRect m_bounds;
};

} // namespace hikari::backends
