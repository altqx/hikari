#include "hikari/application/owned_file.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace hikari::application {

namespace fs = std::filesystem;

namespace {

fs::path withSuffix(const fs::path &path, std::string_view suffix)
{
    fs::path out = path;
    out += fs::path(std::u8string(reinterpret_cast<const char8_t *>(suffix.data()), suffix.size()));
    return out;
}

// <name>.broken, else the first free <name>.broken-N.
fs::path brokenName(const fs::path &path)
{
    std::error_code ec;
    fs::path name = withSuffix(path, ".broken");
    for (int n = 2; fs::exists(name, ec); ++n)
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
    // Columns count characters: UTF-8 continuation bytes are not one.
    int column = 1;
    for (std::size_t i = start; i < offset; ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            ++column;
    return {line, column};
}

std::optional<ParseFailure> checkFileVersion(std::optional<std::int64_t> version, std::int64_t supported)
{
    if (!version)
        return ParseFailure{0, 0, "It has no version number."};
    if (*version < 1)
        return ParseFailure{0, 0, "Its version " + std::to_string(*version) + " is not a version number."};
    if (*version > supported)
        return ParseFailure{0, 0,
                            "It is version " + std::to_string(*version) + ", newer than this HikariSub reads (" +
                                std::to_string(supported) + ")."};
    return std::nullopt;
}

std::optional<std::string> readOwnedFile(const fs::path &path)
{
    // A folder in the file's place opens on Linux, then throws on reading.
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return std::nullopt;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::nullopt;
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in.bad())
            return std::nullopt;
        return bytes;
    } catch (const std::ios_base::failure &) {
        return std::nullopt;
    }
}

bool writeFileAtomically(const fs::path &path, std::string_view bytes)
{
    std::error_code ec;
    if (path.has_parent_path())
        fs::create_directories(path.parent_path(), ec);
    const fs::path temporary = withSuffix(path, ".writing");
    const fs::file_status existing = fs::status(path, ec);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        // The file keeps its permissions: a private state.json stays private.
        if (fs::is_regular_file(existing))
            fs::permissions(temporary, existing.permissions(), fs::perm_options::replace, ec);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temporary, ec);
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

FileLoad loadOwnedFile(const fs::path &path, const std::function<std::optional<ParseFailure>(std::string_view)> &parse)
{
    std::error_code ec;
    if (!fs::exists(path, ec))
        return {};
    FileLoad load;
    std::optional<ParseFailure> failure;
    const auto bytes = readOwnedFile(path);
    if (bytes)
        failure = parse(*bytes);
    else
        failure = ParseFailure{0, 0, "It could not be read."};
    if (!failure) {
        load.state = FileState::Read;
        return load;
    }
    load.state = FileState::Broken;
    FileProblem problem{path, {}, std::move(*failure)};
    // A file that cannot be read stays where it is (and is not written).
    if (bytes) {
        const fs::path kept = brokenName(path);
        fs::rename(path, kept, ec);
        if (!ec)
            problem.keptAs = kept;
    }
    load.problem = std::move(problem);
    return load;
}

Conversion convertOnce(const fs::path &target, const fs::path &legacy, const std::function<bool()> &convert)
{
    std::error_code ec;
    if (fs::exists(target, ec) || !fs::exists(legacy, ec))
        return Conversion::NotNeeded;
    if (!convert())
        return Conversion::Failed;
    const fs::path old = withSuffix(legacy, ".old");
    fs::remove(old, ec);
    fs::rename(legacy, old, ec);
    return Conversion::Converted;
}

std::string displayPath(const fs::path &path)
{
    const std::u8string text = path.lexically_normal().make_preferred().u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

} // namespace hikari::application
