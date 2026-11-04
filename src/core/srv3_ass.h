#pragma once

#include "srv3_model.h"

#include <map>
#include <vector>

namespace hikari::core::srv3 {

struct VideoSize {
    int width = 384, height = 288;
};

struct AssDocument {
    VideoSize video;
    LineMergeType lineMergeType = LineMergeType::MoveNew;
    std::vector<LinePtr> lines;
    std::vector<AssStyle> styles;
    std::vector<AssStyleOptions> options;
    const AssStyle *defaultStyle = nullptr;

    const AssStyle *style(u16v name) const;
    const AssStyleOptions *styleOptions(u16v name) const;
    PointF defaultPosition(AnchorPoint anchor) const;
};

PointF defaultPosition(VideoSize video, AnchorPoint anchor);

// AssDocument(TextReader, styleOptions): the ASS script as lines ready for YttDocument.
AssDocument readAss(u16v script, std::vector<AssStyleOptions> options);

// YTSubConverter's built-in options for its YT* and __yt* style names.
std::vector<AssStyleOptions> builtInStyleOptions();

}
