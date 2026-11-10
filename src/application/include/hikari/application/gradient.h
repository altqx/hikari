#pragma once

#include "hikari/application/gradient_geometry.h"
#include "hikari/application/tag_lerp.h"

#include "hikari/core/style.h"

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application::gradient {

enum class Mode { Rect, Rotated, Skew, ClipEdge, Wedge, ByChar, ByLine };
enum class MaskShape { Wedge, Box, Ring, Star, Vignette };

struct TagStops {
    std::string key; // a lerp::TagSpec key
    lerp::Value from, to;
};

struct Spec {
    Mode mode = Mode::Rect;
    // Values mode: these tags from `from` to `to`. Picked lines (strip modes only): every target is a stop.
    std::vector<TagStops> tags;
    bool pickedLines = false;
    Point start, end;
    double skew = 0; // degrees
    double strip = 4, overlap = 0, accel = 1;
    lerp::Blend blend = lerp::Blend::Rgb;
    bool merge = true;
    MaskShape shape = MaskShape::Wedge;
    MaskFrame mask;
    double spacing = 20, thickness = 0.1, centre = 0.22, hole = 0;
    int spikes = 20;
    bool measured = true; // By character: t by measured advance, else by character count
};

enum class Problem {
    NoTags,         // Values mode with no tag
    TooFewStops,    // Picked lines with fewer than two Lines
    StopsDiffer,    // Picked lines whose text or blocks differ
    NothingDiffers, // Picked lines with no tag to interpolate
    NoLength,       // the drag is a point (or the mask frame is flat)
    NoArea,         // no clip and no text to cover
    NeedsClip,      // Strips along the clip on a Line without one
    InverseClip,    // Strips along the clip on an \iclip
    NoText,         // By character on a Line without characters
};

struct LineIn {
    std::string text;
    core::StyleValues style;
    std::optional<Rect> box; // the text's rectangle, border included
};

// The width of `text` in the Line's style; nullopt when it can't be measured.
using Measure = std::function<std::optional<double>(const std::string &text)>;

// One Line per strip, first to last; the caller comments the original above them.
std::expected<std::vector<std::string>, Problem> stripTexts(const Spec &spec, const LineIn &line);
// Picked lines: the stops in Grid order; the strips are built on the first one.
std::expected<std::vector<std::string>, Problem> stripTextsFromStops(const Spec &spec,
                                                                     const std::vector<LineIn> &stops);
// The strips' shapes before tags: what the preview outlines.
std::expected<std::vector<Shape>, Problem> stripShapes(const Spec &spec, const LineIn &line);

std::expected<std::string, Problem> byCharText(const Spec &spec, const LineIn &line, const Measure &measure);
// `index` of `count` targets in Grid order.
std::expected<std::string, Problem> byLineText(const Spec &spec, const LineIn &line, std::size_t index,
                                               std::size_t count);
std::expected<std::string, Problem> maskText(const Spec &spec, const LineIn &line);

// The text with every instance of the tag (outside \t) removed and `value` put in the first block.
std::string replaceTag(std::string_view text, std::string_view key, std::string_view tag);
// The first vector \clip's shape; nullopt for none, a rect clip or an \iclip.
std::optional<Shape> vectorClip(std::string_view text);
// The tag's value in the first block, else the Style's.
std::optional<lerp::Value> startValue(std::string_view text, std::string_view key, const core::StyleValues &style);

}
