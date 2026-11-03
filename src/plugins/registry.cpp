#include "hikari/plugins/registry.h"

#include "hikari/api/json.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace hikari::plugins {

namespace {

using api::Json;

constexpr std::int64_t kFormat = 1;

bool isPluginId(std::string_view id)
{
    return !id.empty() && id.front() != '.' && id.back() != '.' && id.find("..") == std::string_view::npos &&
           std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; });
}

std::optional<InstalledPlugin> readPlugin(const Json &json)
{
    const Json *version = json.find("version");
    const Json *enabled = json.find("enabled");
    if (!version || !version->isString() || !enabled || !enabled->isBool())
        return std::nullopt;
    InstalledPlugin plugin{version->asString(), enabled->asBool(), std::nullopt, {}, Json::object()};
    if (const Json *key = json.find("key")) {
        if (!key->isString() || !(plugin.key = minisign::parsePublicKey(key->asString())))
            return std::nullopt;
    }
    if (const Json *grants = json.find("grants")) {
        if (!grants->isObject())
            return std::nullopt;
        for (const Json::Member &grant : grants->asObject()) {
            const Json *path = grant.value.find("path");
            const Json *writable = grant.value.find("writable");
            if (!path || !path->isString() || !writable || !writable->isBool())
                return std::nullopt;
            plugin.grants[grant.key] = {path->asString(), writable->asBool()};
        }
    }
    if (const Json *settings = json.find("settings")) {
        if (!settings->isObject())
            return std::nullopt;
        plugin.settings = *settings;
    }
    return plugin;
}

}

std::expected<Registry, std::string> loadRegistry(const std::filesystem::path &file)
{
    std::error_code error;
    if (!std::filesystem::exists(file, error))
        return error ? std::expected<Registry, std::string>(std::unexpected(error.message())) : Registry{};
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    if (!in)
        return std::unexpected("cannot read " + file.filename().string());
    const auto json = Json::parse(text.str());
    if (!json)
        return std::unexpected(json.error().message);
    const Json *format = json->find("format");
    const Json *plugins = json->find("plugins");
    if (!format || !format->isInt() || format->asInt() != kFormat || !plugins || !plugins->isObject())
        return std::unexpected("not a format " + std::to_string(kFormat) + " plugins file");
    Registry registry;
    for (const Json::Member &member : plugins->asObject()) {
        auto plugin = isPluginId(member.key) ? readPlugin(member.value) : std::nullopt;
        if (!plugin)
            return std::unexpected("malformed entry \"" + member.key + "\"");
        registry[member.key] = std::move(*plugin);
    }
    return registry;
}

bool saveRegistry(const std::filesystem::path &file, const Registry &registry)
{
    Json plugins = Json::object();
    for (const auto &[id, plugin] : registry) {
        Json entry = Json::object({{"version", plugin.version}, {"enabled", plugin.enabled}});
        if (plugin.key)
            entry["key"] = plugin.key->text();
        if (!plugin.grants.empty()) {
            Json grants = Json::object();
            for (const auto &[grantId, grant] : plugin.grants)
                grants[grantId] = Json::object({{"path", grant.path}, {"writable", grant.writable}});
            entry["grants"] = std::move(grants);
        }
        if (!plugin.settings.asObject().empty())
            entry["settings"] = plugin.settings;
        plugins[id] = std::move(entry);
    }
    const std::string text = Json::object({{"format", kFormat}, {"plugins", std::move(plugins)}}).dump(2) + "\n";

    std::filesystem::path partial = file;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out.flush())
            return false;
    }
    std::error_code error;
    std::filesystem::rename(partial, file, error);
    if (!error)
        return true;
    std::filesystem::remove(partial, error);
    return false;
}

}
