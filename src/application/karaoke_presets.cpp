#include "hikari/application/karaoke_presets.h"

#include "hikari/application/template_preview.h"
#include "hikari/core/ass_load.h"

namespace hikari::application {

std::optional<KaraokePreset> readKaraokePreset(std::span<const std::byte> bytes)
{
    const core::Document doc = core::loadAss(bytes).document;
    KaraokePreset preset;
    preset.title = doc.scriptInfo(u8"Title").value_or(std::u8string());
    preset.description = doc.scriptInfo(u8"Description").value_or(std::u8string());
    if (preset.title.empty() || preset.description.empty())
        return std::nullopt;
    for (const core::LineRecord *line : doc.lines()) {
        if (!templateKindOf(*line))
            return std::nullopt;
        preset.lines.push_back({line->layer.value, line->effect, line->text});
    }
    if (preset.lines.empty())
        return std::nullopt;
    return preset;
}

}
