#pragma once

#include <cstddef>
#include <string>

namespace hikari::core {

struct ShrunkFrame {
    int width = 0, height = 0;
    std::string bytes; // top row first; one luma byte per pixel, or BGRA
};

// Box-averages a top-down BGRA picture until its longer edge is at most maxEdge; a smaller
// picture keeps its size. Luma is BT.601's weights on each box's averages. video.frames' bytes:
// every path that serves it must come here.
ShrunkFrame shrinkFrame(const std::byte *bgra, int width, int height, std::size_t stride, int maxEdge, bool luma);

}
