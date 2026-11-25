#include "hikari/application/subtitle_boundary.h"

namespace hikari::application {

std::optional<std::int64_t> nextBoundaryInLine(LineFrames line, std::int64_t shown)
{
    if (line.first > shown)
        return line.first;
    if (line.last > shown)
        return line.last;
    return std::nullopt;
}

std::optional<std::int64_t> previousBoundaryInLine(LineFrames line, std::int64_t shown)
{
    if (line.last < shown)
        return line.last;
    if (line.first < shown)
        return line.first;
    return std::nullopt;
}

}
