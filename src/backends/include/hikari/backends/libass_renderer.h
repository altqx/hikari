#pragma once

// libass adapter for the SubtitleRenderer port (N3). One mutex serializes the
// rendering; prepare() parses a new script outside it, so the shown track
// keeps rendering meanwhile (see prepare() for why that is safe).
// - prepare() keeps the libass library and renderer (fonts, font provider,
//   caches) while the snapshot's fonts are the same leases with the same
//   default family and provider, and only switches the track; other fonts,
//   or a reload of the system's fonts (application::reloadSubtitleFonts,
//   legacy ReloadLibraries), build a new library and renderer, and the old
//   ones release their leases.
//   Parsed tracks are kept by a hash of the script, as legacy
//   SubtitlesLibass::ReadTrack keeps them (ParsedScripts, three), so going
//   back to a script parsed before needs no parse.
// - render() composites libass's images with legacy BlendAssBitmap
//   (overlay_blend.h) into a pooled buffer, clearing only what that buffer
//   drew last. The frame shares the buffer (OverlayPixels); an unchanged
//   frame hands the same buffer out again, and a buffer is drawn into again
//   only once nobody else holds it.

#include "hikari/application/subtitle_render.h"
#include "hikari/backends/overlay_blend.h"

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

    // For tests: the tracks kept, and the libass renderer in use (another
    // one after the fonts change).
    std::size_t parsedTracks() const;
    const void *engineIdentity() const;

private:
    struct Engine;

    std::mutex m_prepare;     // one prepare() at a time; it parses outside m_mutex
    mutable std::mutex m_mutex; // the engine's tracks, render() and its buffers
    std::unique_ptr<Engine> m_engine;
    std::uint64_t m_generation = 0;
    bool m_redraw = true;            // the next render draws, whatever libass says changed
    OverlayBuffers m_buffers;
    application::PixelRect m_bounds; // what the shown buffer has drawn
};

} // namespace hikari::backends
