#pragma once

#include "hikari/application/edit_session.h"

#include <expected>
#include <functional>
#include <string>

namespace hikari::application {

struct Resolution {
    int width = 0, height = 0;
    bool operator==(const Resolution &) const = default;
};

// PlayResX/Y; 1280x720 when both are missing, a 16:9 partner when one is.
Resolution scriptResolution(const core::Document &document);

// A value that could not be scaled; `row` is 1-based.
using ResampleWarning = std::function<void(int row, const std::u16string &value, const std::u16string &tag)>;

// Scales Style margins, font size, outline, shadow, spacing (ScaleX when stretching) and each
// non-comment Line's margins, positioning, clip, border, shadow and font tags and drawings. Edits
// the translation when there is one.
void resizeSubtitles(core::Document &document, float xScale, float yScale, bool stretch,
                     const ResampleWarning &warn = {});

// Sets PlayResX/Y (and LayoutResX/Y when LayoutResX is set) to `to`, then resizes from `from` when
// `resample`. Refused unless the Document is ASS or plain text.
std::expected<void, CommandRefusal> changeResolution(EditSession &session, Resolution from, Resolution to,
                                                     bool resample, bool stretch, const ResampleWarning &warn = {});

} // namespace hikari::application
