#include "hikari/application/karaoke_presets.h"

#include "hikari/core/ass_load.h"

#include <string_view>

namespace hikari::application {

namespace {

bool isTemplateOrCode(std::u8string_view effect)
{
    const std::u8string_view word = effect.substr(0, effect.find(u8' '));
    return word == u8"template" || word == u8"code";
}

} // namespace

std::optional<KaraokePreset> readKaraokePreset(std::span<const std::byte> bytes)
{
    const core::Document doc = core::loadAss(bytes).document;
    KaraokePreset preset;
    preset.title = doc.scriptInfo(u8"Title").value_or(std::u8string());
    preset.description = doc.scriptInfo(u8"Description").value_or(std::u8string());
    if (preset.title.empty() || preset.description.empty())
        return std::nullopt;
    for (const core::LineRecord *line : doc.lines()) {
        if (!line->comment || !isTemplateOrCode(line->effect))
            return std::nullopt;
        preset.lines.push_back({line->layer.value, line->effect, line->text});
    }
    if (preset.lines.empty())
        return std::nullopt;
    return preset;
}

} // namespace hikari::application
