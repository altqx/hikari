#pragma once

#include <span>
#include <string_view>

namespace hikari::application {

// A library graph for ordinary Lines. Its knobs are value nodes whose ids name them.
struct TypesettingGraph {
    std::string_view id;
    std::string_view graph;
};

std::span<const TypesettingGraph> typesettingGraphs();

}
