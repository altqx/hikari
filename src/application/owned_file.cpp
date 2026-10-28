#include "hikari/application/owned_file.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace hikari::application {

namespace {

std::filesystem::path withSuffix(const std::filesystem::path &path, std::string_view suffix)
{
    std::filesystem::path out = path;
    out += std::filesystem::path(suffix);
    return out;
}

std::optional<std::string> readAll(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The first free <name>.broken, <name>.broken-2, ...
std::filesystem::path brokenName(const std::filesystem::path &path)
{
    std::error_code ec;
    std::filesystem::path name = withSuffix(path, ".broken");
    for (int n = 2; std::filesystem::exists(name, ec); ++n)
        name = withSuffix(path, ".broken-" + std::to_string(n));
    return name;
}

} // namespace

std::pair<int, int> lineColumnAt(std::string_view text, std::size_t offset)
{
    offset = std::min(offset, text.size());
    int line = 1;
    std::size_t start = 0;
    for (std::size_t i = 0; i < offset; ++i)
        if (text[i] == '\n') {
            ++line;
            start = i + 1;
        }
    return {line, static_cast<int>(offset - start) + 1};
}

std::optional<ParseFailure> checkFileVersion(std::optional<std::int64_t> version, std::int64_t supported)
{
    if (!version)
        return ParseFailure{0, 0, "it has no version"};
    if (*version < 1 || *version > supported)
        return ParseFailure{0, 0, "it has version " + std::to_string(*version) + ", which this version of HikariSub "
                                  "does not know"};
    return std::nullopt;
}

FileLoad loadOwnedFile(const std::filesystem::path &path,
                       const std::function<std::optional<ParseFailure>(std::string_view)> &parse)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return {};
    const auto bytes = readAll(path);
    std::optional<ParseFailure> failure =
        bytes ? parse(*bytes) : std::optional<ParseFailure>(ParseFailure{0, 0, "it cannot be read"});
    if (!failure)
        return {FileState::Read, std::nullopt};
    FileProblem problem{path, brokenName(path), std::move(*failure)};
    std::filesystem::rename(path, problem.keptAs, ec);
    if (ec)
        problem.keptAs.clear();
    return {FileState::Broken, std::move(problem)};
}

Conversion convertOnce(const std::filesystem::path &target, const std::filesystem::path &legacy,
                       const std::function<bool()> &convert)
{
    std::error_code ec;
    if (std::filesystem::exists(target, ec) || !std::filesystem::is_regular_file(legacy, ec))
        return Conversion::NotNeeded;
    if (!convert())
        return Conversion::Failed;
    const std::filesystem::path old = withSuffix(legacy, ".old");
    std::filesystem::remove(old, ec);
    std::filesystem::rename(legacy, old, ec);
    return Conversion::Converted;
}

bool writeFileAtomically(const std::filesystem::path &path, std::string_view bytes)
{
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temporary = withSuffix(path, ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

} // namespace hikari::application
