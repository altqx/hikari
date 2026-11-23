#include "hikari/application/plugin_content.h"

#include "hikari/application/karaoke_presets.h"
#include "hikari/application/style_manager.h"

#include <mutex>

namespace hikari::application {

namespace {

struct Store {
    std::mutex mutex;
    PluginContent content;
};

Store &store()
{
    static Store s;
    return s;
}

}

void setPluginContent(PluginContent content)
{
    Store &s = store();
    const std::lock_guard lock(s.mutex);
    s.content = std::move(content);
}

PluginContent pluginContent()
{
    Store &s = store();
    const std::lock_guard lock(s.mutex);
    return s.content;
}

std::optional<std::string> styleCatalogueProblem(std::span<const std::byte> bytes)
{
    if (readCatalog({reinterpret_cast<const char *>(bytes.data()), bytes.size()}).empty())
        return "holds no Style: lines";
    return std::nullopt;
}

std::optional<std::string> karaokePresetProblem(std::span<const std::byte> bytes)
{
    if (!readKaraokePreset(bytes))
        return "isn\xE2\x80\x99t a karaoke preset: it needs a Title, a Description and only template or code lines";
    return std::nullopt;
}

}
