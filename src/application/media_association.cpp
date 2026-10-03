#include "hikari/application/media_association.h"

namespace hikari::application {

namespace {

std::optional<std::string> scriptInfo(const core::Document &document, std::u8string_view key)
{
    for (const auto &section : document.sections()) {
        if (section.kind != core::SectionKind::ScriptInfo)
            continue;
        for (const auto &record : section.records)
            if (const auto *p = std::get_if<core::PropertyRecord>(&record); p && p->key == key)
                return std::string(p->value.begin(), p->value.end());
    }
    return std::nullopt;
}

bool isAbsolute(const std::string &path, bool windows)
{
    if (windows)
        return path.size() > 1 && path[1] == ':'; // legacy: find(':') == 1
    return !path.empty() && path[0] == '/';
}

std::string directoryOf(const std::string &path, bool windows)
{
    const auto at = windows ? path.find_last_of("\\/") : path.find_last_of('/');
    return at == std::string::npos ? std::string() : path.substr(0, at + 1);
}

std::optional<MediaReference> resolve(const core::Document &document, std::u8string_view key,
                                      const std::string &subtitleDir,
                                      const std::function<bool(const std::string &)> &exists, bool windows)
{
    auto value = scriptInfo(document, key);
    if (!value || value->empty())
        return std::nullopt;
    MediaReference ref{*value, std::nullopt};
    if (isAbsolute(*value, windows)) {
        if (exists(*value))
            ref.resolved = *value;
    } else if (!subtitleDir.empty() && exists(subtitleDir + *value)) {
        ref.resolved = subtitleDir + *value;
    }
    return ref;
}

} // namespace

bool MediaAssociations::offersAnything() const
{
    return (video && video->resolved) || (audio && audio->resolved) || (keyframes && keyframes->resolved);
}

MediaAssociations resolveMediaAssociations(const core::Document &document, const std::string &subtitlePath,
                                           const std::function<bool(const std::string &)> &exists, bool windows)
{
    const std::string dir = directoryOf(subtitlePath, windows);
    return {resolve(document, u8"Video File", dir, exists, windows),
            resolve(document, u8"Audio File", dir, exists, windows),
            resolve(document, u8"Keyframes File", dir, exists, windows)};
}

} // namespace hikari::application
