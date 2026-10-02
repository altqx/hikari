#pragma once

// libass adapter for the SubtitleRenderer port (N3). One owner serializes all
// libass calls. Each prepare() builds a new context (library, renderer,
// track) that keeps the snapshot's font leases until it is replaced; libass
// output is copied into owned pixels before the call returns.

#include "hikari/application/subtitle_render.h"

#include <memory>
#include <mutex>

namespace hikari::backends {

class LibassRenderer : public application::SubtitleRendererPort {
public:
    LibassRenderer();
    ~LibassRenderer() override;

    std::expected<std::uint64_t, application::RenderError> prepare(application::RenderSnapshot snapshot) override;
    std::expected<application::OverlayFrame, application::RenderError> render(core::DocumentTime time, int width,
                                                                             int height) override;

private:
    struct Context;
    std::mutex m_mutex;
    std::unique_ptr<Context> m_context;
    std::uint64_t m_generation = 0;
};

} // namespace hikari::backends
