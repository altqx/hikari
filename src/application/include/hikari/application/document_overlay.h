#pragma once

#include "hikari/application/subtitle_render.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <vector>

namespace hikari::application {

// Draws a Document in a render context of its own, prepared as the video prepares its subtitles; an
// unchanged script and fonts are not parsed again.
class DocumentOverlay {
public:
    using Encode = std::function<std::vector<std::byte>(const core::Document &)>;
    DocumentOverlay(SubtitleRendererPort &renderer, Encode encode);

    std::expected<OverlayFrame, RenderError> render(const core::Document &document, std::vector<FontLease> fonts,
                                                    core::DocumentTime time, int width, int height);

private:
    SubtitleRendererPort &m_renderer;
    Encode m_encode;
    std::optional<std::vector<std::byte>> m_script;
    std::vector<FontLease> m_fonts;
};

// The Document with every Line but `line` removed.
core::Document withOnlyLine(const core::Document &document, core::LineId line);

// The smallest rectangle holding every pixel the overlay draws on; nullopt when it draws none.
std::optional<PixelRect> inkBounds(const OverlayFrame &overlay);

}
