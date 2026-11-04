#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/visual_perspective.h"

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

struct ResampleOptions {
    bool stretch = false;
    // A Line turned in 3D, or turned about its own \org (any turn or shear when stretching) when the aspect
    // ratio changes, keeps its shape on screen: its leading \pos, \org, \fr, \fax, \fsc, outline and
    // shadow are found again from its quad, as arch1t3cht's Resample Perspective finds them.
    bool adjustRotations = true;
    // The video's height, at which libass lays out without LayoutResY; 0 without video.
    int frameHeight = 0;
    visual::perspective::TextMeasure measure;
    ResampleWarning unscaled;
    // A Line adjustRotations would change but cannot: it moves, animates its shape or cannot be measured.
    std::function<void(int row)> unadjusted;
};

// Sets PlayResX/Y to `to`. When `resample` it resizes from `from` and, as TypesettingTools' Aegisub
// does, keeps LayoutResY and scales LayoutResX by the aspect ratio's change when both are set.
// Refused unless the Document is ASS or plain text.
std::expected<void, CommandRefusal> changeResolution(EditSession &session, Resolution from, Resolution to,
                                                     bool resample, const ResampleOptions &options = {});

}
