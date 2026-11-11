#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/document.h"
#include "hikari/core/style.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct GlowLayer {
    double blur = 3;
    double border = 0; // added to the Line's border
    std::optional<core::Colour> colour; // none: the Line's border colour, or its fill without a border
    int alpha = 0x80;
};

enum class UnderBorder { BorderColour, Transparent };

struct LayerOptions {
    bool glow = true;
    std::vector<GlowLayer> glows{GlowLayer{}};

    bool border = true;
    UnderBorder under = UnderBorder::BorderColour;
    std::optional<double> borderBlur; // none: the Line's blur
    bool fadeFix = true;
    int fadeMs = 45;
    bool secondBorder = false;
    double secondWidth = 2;
    std::optional<core::Colour> secondColour; // none: the Line's fill colour

    bool shadow = false;
    std::optional<int> steps; // none: one a pixel of the offset
    std::optional<core::Colour> nearColour, farColour; // none: the Line's shadow colour
    int nearAlpha = 0x00, farAlpha = 0xA0;

    double defaultBlur = 0.6;
};

inline constexpr int kMaxGlows = 8;
inline constexpr int kMaxShadowSteps = 64;
inline constexpr int kMaxBuiltLines = 20000;

struct BuiltLayers {
    std::vector<std::u8string> below; // bottom to top
    std::u8string top;
    bool skippedBorder = false;
    bool skippedShadow = false;
};

// One Line's stack. `durationMs` places the fade fix's \t.
BuiltLayers buildLayers(std::u8string_view text, const core::StyleValues &style,
                        const std::vector<core::StyleValues> &styles, std::int64_t durationMs,
                        const LayerOptions &options);

enum class LayerProblem { NothingOn, NoLines, TooManyGlows, TooManySteps, TooManyLines };

struct LayerSource {
    core::LineId id;
    int row = 0; // 1-based Grid row
    std::int64_t layer = 0;
    std::int64_t newLayer = 0;
    BuiltLayers layers;
};

// A Line above a source, shown with it, that the source's stack will cover.
struct CoveredLine {
    int row = 0;
    std::int64_t layer = 0;
    int sourceRow = 0;
};

struct LayerPlan {
    std::vector<LayerSource> sources;
    int built = 0;
    int skippedBorder = 0;
    int skippedShadow = 0;
    std::vector<CoveredLine> covered;
};

// The selected Dialogue Lines `visible` shows, in Grid order.
std::expected<LayerPlan, LayerProblem> planLayers(const core::Document &doc, const std::set<core::LineId> &selection,
                                                  const LayerOptions &options, const LineVisible &visible = {});

// The built Lines go in just above their source, bottom layer first; the source keeps its id.
bool applyLayerPlan(core::Document &doc, const LayerPlan &plan);

// One history step, "Blur and glow layers"; the selection stays on the sources.
std::expected<void, CommandRefusal> buildLayerStacks(EditSession &session, const LayerOptions &options,
                                                     const LineVisible &visible = {});

}
