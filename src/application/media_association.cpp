#include "hikari/application/media_association.h"

#include <cstdlib>

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
        return path.size() > 1 && path[1] == ':';
    return !path.empty() && path[0] == '/';
}

std::string directoryOf(const std::string &path, bool windows)
{
    const auto at = windows ? path.find_last_of("\\/") : path.find_last_of('/');
    return at == std::string::npos ? std::string() : path.substr(0, at + 1);
}

std::optional<int> intValue(const core::Document &document, std::u8string_view key)
{
    const auto value = projectValue(document, key);
    if (!value)
        return std::nullopt;
    return std::atoi(value->c_str());
}

std::optional<MediaReference> resolve(const core::Document &document, std::u8string_view key,
                                      const std::string &subtitleDir,
                                      const std::function<bool(const std::string &)> &exists, bool windows)
{
    auto value = projectValue(document, key);
    if (!value || value->empty())
        return std::nullopt;
    MediaReference ref{*value, std::nullopt};
    // a "?dummy" video is not a file
    if (key == u8"Video File" && value->starts_with("?dummy")) {
        ref.resolved = *value;
        return ref;
    }
    if (isAbsolute(*value, windows)) {
        if (exists(*value))
            ref.resolved = *value;
    } else if (!subtitleDir.empty() && exists(subtitleDir + *value)) {
        ref.resolved = subtitleDir + *value;
    }
    return ref;
}

}

std::optional<std::string> projectValue(const core::Document &document, std::u8string_view key)
{
    if (auto value = document.projectGarbage(key))
        return std::string(value->begin(), value->end());
    return scriptInfo(document, key);
}

SavedView savedView(const core::Document &document)
{
    SavedView view{intValue(document, u8"Active Line"), intValue(document, u8"Scroll Position"),
                   intValue(document, u8"Video Position"), std::nullopt};
    switch (intValue(document, u8"Video AR Mode").value_or(0)) {
    case 1: view.aspect = 4.0 / 3.0; break;
    case 2: view.aspect = 16.0 / 9.0; break;
    case 3: view.aspect = 2.35; break;
    case 4:
        if (const auto value = projectValue(document, u8"Video AR Value"); value && std::atof(value->c_str()) > 0)
            view.aspect = std::atof(value->c_str());
        break;
    default: break;
    }
    return view;
}

bool MediaAssociations::offersAnything() const
{
    return (video && video->resolved) || (audio && audio->resolved) || (keyframes && keyframes->resolved) ||
           (timecodes && timecodes->resolved);
}

MediaAssociations resolveMediaAssociations(const core::Document &document, const std::string &subtitlePath,
                                           const std::function<bool(const std::string &)> &exists, bool windows)
{
    const std::string dir = directoryOf(subtitlePath, windows);
    return {resolve(document, u8"Video File", dir, exists, windows),
            resolve(document, u8"Audio File", dir, exists, windows),
            resolve(document, kKeyframesFileKey, dir, exists, windows),
            resolve(document, kTimecodesFileKey, dir, exists, windows)};
}

}
