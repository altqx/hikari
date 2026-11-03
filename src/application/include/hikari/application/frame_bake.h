#pragma once

#include "hikari/core/style.h"

#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct BakeFrame {
    double now = 0;      // ms from the Line's start to the frame's start
    double duration = 0; // the Line's length in ms
    int playResX = 0, playResY = 0;
};

// The text as shown at `frame`, by arch.Line2Fbf's rules: a moving \move becomes its \pos, each \t the
// values it has reached (with its acceleration) and \fad/\fade a factor on every alpha. `styles` resolve
// \r. Text with none of these comes back unchanged.
std::u8string bakeAnimations(std::u8string_view text, const core::StyleValues &style,
                             const std::vector<core::StyleValues> &styles, const BakeFrame &frame);

}
