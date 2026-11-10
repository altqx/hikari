#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

struct CompiledSource {
    std::string code;
    // Each generated line and the source position it came from.
    std::vector<std::pair<double, double>> lines;

    bool operator==(const CompiledSource &) const = default;
};

// One entry a file and compiler, in `dir`. An entry answers only for the exact source it was compiled from.
class CompileCache {
public:
    CompileCache(std::filesystem::path dir, std::string compiler);

    std::optional<CompiledSource> find(std::string_view file, std::string_view source) const;
    // Best effort: a cache that can't be written stays as it was.
    void store(std::string_view file, std::string_view source, const CompiledSource &compiled) const;

    // Identifies a compiler by its own source text.
    static std::string compilerId(std::string_view compilerSource);

private:
    std::filesystem::path entry(std::string_view file) const;

    std::filesystem::path m_dir;
    std::string m_compiler;
};

}
