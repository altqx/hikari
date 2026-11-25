#include "hikari/application/script_resolution.h"

#include <algorithm>
#include <cstdlib>

namespace hikari::application {

namespace {

int atoiOf(const std::string &text)
{
    return std::atoi(text.c_str());
}

std::u8string wide(const std::string &s)
{
    return std::u8string(s.begin(), s.end());
}

}

Resolution writtenScriptResolution(const core::Document &document)
{
    const auto read = [&](std::u8string_view key) {
        const auto value = document.scriptInfo(key).value_or(u8"");
        return atoiOf(std::string(value.begin(), value.end()));
    };
    return {read(u8"PlayResX"), read(u8"PlayResY")};
}

Resolution writtenScriptResolution(const std::vector<MacroInfoLine> &info)
{
    Resolution r;
    for (const auto &entry : info) {
        if (entry.key == "PlayResX")
            r.width = atoiOf(entry.value);
        else if (entry.key == "PlayResY")
            r.height = atoiOf(entry.value);
    }
    return r;
}

bool lacksScriptResolution(Resolution written)
{
    return written.width < 1 || written.height < 1;
}

Resolution legacyScriptResolution(Resolution written)
{
    int x = written.width, y = written.height;
    if (x < 1 && y < 1)
        return {1280, 720};
    if (x < 1)
        x = static_cast<int>(static_cast<float>(y) * (16.0 / 9.0));
    else if (y < 1)
        y = static_cast<int>(static_cast<float>(x) * (9.0 / 16.0));
    return {x, y};
}

Resolution playersScriptResolution(Resolution written)
{
    int x = written.width, y = written.height;
    if (x < 1 && y < 1)
        return {384, 288};
    if (x < 1)
        x = y == 1024 ? 1280 : std::max(1, static_cast<int>(static_cast<long long>(y) * 4 / 3));
    else if (y < 1)
        y = x == 1280 ? 1024 : std::max(1, static_cast<int>(static_cast<long long>(x) * 3 / 4));
    return {x, y};
}

Resolution layoutResolution(const core::Document &document, std::optional<Resolution> video)
{
    const bool dialogue = std::ranges::any_of(document.lines(), [](const core::LineRecord *l) { return !l->comment; });
    const Resolution written = writtenScriptResolution(document);
    if (!lacksScriptResolution(written))
        return written;
    return dialogue ? playersScriptResolution(written) : suggestedScriptResolution(written, video);
}

Resolution layoutResolution(const std::vector<MacroInfoLine> &info, bool hasDialogue, std::optional<Resolution> video)
{
    const Resolution written = writtenScriptResolution(info);
    if (!lacksScriptResolution(written))
        return written;
    return hasDialogue ? playersScriptResolution(written) : suggestedScriptResolution(written, video);
}

bool stampScriptResolution(core::Document &document, Resolution resolution)
{
    if (document.format() != core::SubtitleFormat::Ass || resolution.width < 1 || resolution.height < 1 ||
        !lacksScriptResolution(writtenScriptResolution(document)))
        return false;
    return document.setScriptInfo(u8"PlayResX", wide(std::to_string(resolution.width))) &&
           document.setScriptInfo(u8"PlayResY", wide(std::to_string(resolution.height)));
}

Resolution suggestedScriptResolution(Resolution written, std::optional<Resolution> video)
{
    if (video && video->width > 0 && video->height > 0)
        return *video;
    return legacyScriptResolution(written);
}

void setStagedScriptInfo(std::vector<MacroInfoLine> &info, std::string_view key, std::string value)
{
    for (auto it = info.rbegin(); it != info.rend(); ++it)
        if (it->key == key) {
            it->value = std::move(value);
            return;
        }
    info.push_back({std::string(key), std::move(value)});
}

void setStagedScriptResolution(std::vector<MacroInfoLine> &info, Resolution resolution)
{
    setStagedScriptInfo(info, "PlayResX", std::to_string(resolution.width));
    setStagedScriptInfo(info, "PlayResY", std::to_string(resolution.height));
}

std::expected<void, CommandRefusal> setScriptResolution(EditSession &session, Resolution resolution)
{
    if (session.document().format() != core::SubtitleFormat::Ass || resolution.width < 1 || resolution.height < 1)
        return std::unexpected(CommandRefusal::Invalid);
    const auto x = wide(std::to_string(resolution.width)), y = wide(std::to_string(resolution.height));
    const auto &doc = session.document();
    if (doc.scriptInfo(u8"PlayResX") == x && doc.scriptInfo(u8"PlayResY") == y)
        return {};
    const auto ran = session.run(Command{std::string(kScriptResolutionStep), session.revision(), {}, [&](core::Document &d) {
        return d.setScriptInfo(u8"PlayResX", x) && d.setScriptInfo(u8"PlayResY", y);
    }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

}
