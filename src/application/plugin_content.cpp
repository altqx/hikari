#include "hikari/application/plugin_content.h"

#include "hikari/application/karaoke_presets.h"
#include "hikari/application/style_manager.h"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <string_view>

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

std::optional<std::string> dictionaryAffProblem(std::span<const std::byte> bytes)
{
    if (std::ranges::find(bytes, std::byte{0}) != bytes.end())
        return "isn\xE2\x80\x99t text";
    return std::nullopt;
}

std::optional<std::string> dictionaryDicProblem(std::span<const std::byte> bytes)
{
    std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    text = text.substr(0, text.find('\n'));
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    if (text.starts_with('+'))
        text.remove_prefix(1);
    int words = 0;
    if (std::from_chars(text.data(), text.data() + text.size(), words).ec != std::errc() || words <= 0)
        return "doesn\xE2\x80\x99t start with its word count";
    return std::nullopt;
}

}
