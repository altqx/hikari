#include "hikari/application/generated_runs.h"

#include "hikari/application/node_graph.h"

namespace hikari::application {

bool isFoldableGeneratedLine(const core::LineRecord &line)
{
    if (line.comment)
        return false;
    constexpr std::u8string_view kGraph = u8"fx ";
    return line.effect == u8"fx" || line.effect == u8"Effector [fx]" ||
           (line.effect.starts_with(kGraph) && isGraphName(std::u8string_view(line.effect).substr(kGraph.size())));
}

std::vector<GeneratedRun> generatedRuns(const std::vector<const core::LineRecord *> &lines)
{
    std::vector<GeneratedRun> runs;
    for (std::size_t row = 0; row < lines.size(); ++row) {
        if (!isFoldableGeneratedLine(*lines[row]))
            continue;
        if (!runs.empty() && runs.back().first + runs.back().count == row)
            ++runs.back().count;
        else
            runs.push_back({row, 1});
    }
    return runs;
}

}
