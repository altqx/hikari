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

struct RetimeWindow {
    double offset = 0;       // ms from the Line's start to the piece's start
    double duration = 0;     // the piece's length in ms
    double lineDuration = 0; // the Line's length in ms
};

// The text for a piece of its Line that starts `offset` ms in, so the piece looks as the Line did then:
// \move, \t and \fad/\fade times move back by the offset. One that ends by the piece's start becomes its
// end state, one that starts at or after its end its start state, and a fade the piece sees fully
// opaque goes.
std::u8string retimeAnimations(std::u8string_view text, const RetimeWindow &window);

}
