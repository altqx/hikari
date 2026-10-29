#include "hikari/application/macro_validation.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace hikari::application {

namespace {

std::filesystem::path normalized(const std::string &utf8)
{
    namespace fs = std::filesystem;
    fs::path p(std::u8string(utf8.begin(), utf8.end()));
    std::error_code ec;
    if (p.is_relative()) {
        const fs::path absolute = fs::absolute(p, ec);
        if (!ec)
            p = absolute;
    }
    const fs::path canonical = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : canonical;
}

}

std::vector<MacroRef> dependencyValidations(const std::vector<ScriptStatus> &scripts, const std::string &path)
{
    const auto self = std::ranges::find(scripts, path, &ScriptStatus::path);
    if (self == scripts.end())
        return {};
    const std::filesystem::path selfFile = normalized(path);
    std::vector<std::filesystem::path> files;
    files.reserve(scripts.size());
    for (const auto &s : scripts)
        files.push_back(normalized(s.path));

    std::vector<MacroRef> out;
    std::vector<bool> taken(scripts.size(), false);
    for (const auto &loaded : self->info.loadedFiles) {
        const std::filesystem::path file = normalized(loaded);
        if (file == selfFile)
            continue;
        for (std::size_t i = 0; i < scripts.size(); ++i) {
            const auto &s = scripts[i];
            if (taken[i] || s.path == path || files[i] != file || s.state != ScriptStatus::State::Ready)
                continue;
            taken[i] = true;
            for (std::size_t m = 0; m < s.info.macros.size(); ++m)
                if (s.info.macros[m].hasValidate)
                    out.push_back({s.path, static_cast<int>(m)});
        }
    }
    return out;
}

}
