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

// PlayResX/Y as the players render them; see playersScriptResolution.
Resolution scriptResolution(const core::Document &document);

// Pixels added to (+) or cut from (−) each edge of the subtitles' picture before it is scaled.
struct Edges {
    int left = 0, right = 0, top = 0, bottom = 0;
    bool operator==(const Edges &) const = default;
};

// Aegisub's add borders, or remove borders when `crop`: the edges that give `from` the shape of `to`, split
// equally between two sides. None when the shapes differ by 1 % or less.
Edges fittingEdges(Resolution from, Resolution to, bool crop);

// A value that could not be scaled; `row` is 1-based.
using ResampleWarning = std::function<void(int row, const std::u16string &value, const std::u16string &tag)>;

// Scales Style margins, font size, outline, shadow, spacing (and, when stretching, ScaleX or ScaleY of the
// axis that grew more) and each non-comment Line's margins, positioning, clip, border, shadow and font tags
// and drawings. Edits the translation when there is one. Margins, positions and clips move by `edges` first,
// as Aegisub's resampler moves them; \p drawings do not.
void resizeSubtitles(core::Document &document, float xScale, float yScale, bool stretch,
                     const ResampleWarning &warn = {}, Edges edges = {});

struct ResampleOptions {
    bool stretch = false;
    Edges edges;
    // Converts the colours to this YCbCr Matrix and sets it (MatrixConversion); empty keeps the matrix.
    std::string matrix;
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

// Sets PlayResX/Y to `to`. When `resample` it resizes from `from` grown by the edges and, as Aegisub 3.5.1
// does when both are set, grows LayoutResY with the top and bottom edges and scales LayoutResX with it and
// the aspect ratio's change. Refused unless the Document is ASS or plain text, or when `matrix` cannot be
// converted to.
std::expected<void, CommandRefusal> changeResolution(EditSession &session, Resolution from, Resolution to,
                                                     bool resample, const ResampleOptions &options = {});

}
