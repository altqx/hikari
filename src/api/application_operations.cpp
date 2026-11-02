#include "operations.h"

#include "hikari/application/hotkeys.h"

#include <algorithm>
#include <cctype>

namespace hikari::api::detail {

namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

Result apiVersion(const Context &, const Json &)
{
    return Json::object({{"version", Json(kApiVersion)}});
}

Result commandsList(const Context &context, const Json &params)
{
    if (!context.host.commands)
        return fail(errors::kUnavailable, "No commands are available.");
    const Json *window = params.find("window");
    const std::string query = params.find("query") ? lower(params.find("query")->asString()) : std::string();
    Json list = Json::array();
    for (const application::HotkeyAction &action : application::hotkeyActions()) {
        const std::string name = application::hotkeyName(action.id);
        if (name.empty() || application::isRetiredHotkey(action.id))
            continue;
        const std::string windowName = lower(application::hotkeyWindowName(application::hotkeyType(action.id)));
        if (window && window->asString() != windowName)
            continue;
        if (!query.empty() && lower(action.symbol).find(query) == std::string::npos &&
            lower(name).find(query) == std::string::npos)
            continue;
        const auto state = context.host.commands->state(action.symbol);
        if (!state)
            continue;
        Json keys = Json::array();
        for (const std::string &k : state->keys)
            keys.push(Json(k));
        Json entry = Json::object({{"command", Json(action.symbol)},
                                   {"name", Json(name)},
                                   {"window", Json(windowName)},
                                   {"enabled", Json(state->enabled)},
                                   {"keys", keys}});
        if (!state->enabled && !state->disabledReason.empty())
            entry["disabledReason"] = Json(state->disabledReason);
        list.push(std::move(entry));
    }
    return Json::object({{"commands", list}});
}

Result panelsList(const Context &context, const Json &)
{
    if (!context.host.panels)
        return fail(errors::kUnavailable, "No panels are available.");
    Json list = Json::array();
    for (const PanelState &panel : context.host.panels->panels())
        list.push(Json::object({{"panel", Json(panel.id)},
                                {"title", Json(panel.title)},
                                {"open", Json(panel.open)},
                                {"floating", Json(panel.floating)},
                                {"focused", Json(panel.focused)}}));
    return Json::object({{"panels", list}});
}

Json settingJson(const application::SettingValue &value)
{
    return std::visit(
        [](const auto &v) -> Json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                Json list = Json::array();
                for (const std::string &s : v)
                    list.push(Json(s));
                return list;
            } else {
                return Json(v);
            }
        },
        value);
}

std::string_view typeName(application::SettingType type)
{
    switch (type) {
    case application::SettingType::Bool: return "boolean";
    case application::SettingType::Int: return "integer";
    case application::SettingType::String: return "string";
    case application::SettingType::StringList: return "stringList";
    }
    return "string";
}

const application::SettingDefinition *offered(std::string_view id)
{
    const auto *definition = application::findSetting(id);
    return definition && definition->disposition == application::SettingDisposition::Mapped ? definition : nullptr;
}

Result settingsList(const Context &context, const Json &params)
{
    if (!context.host.settings)
        return fail(errors::kUnavailable, "No settings are available.");
    const std::string prefix = params.find("prefix") ? params.find("prefix")->asString() : std::string();
    Json list = Json::array();
    for (const application::SettingDefinition &definition : application::settingDefinitions()) {
        if (definition.disposition != application::SettingDisposition::Mapped || !definition.id.starts_with(prefix))
            continue;
        list.push(Json::object({{"setting", Json(definition.id)},
                                {"type", Json(typeName(definition.type))},
                                {"scope", Json(application::scopeName(definition.scope))},
                                {"default", settingJson(definition.defaultValue)},
                                {"value", settingJson(context.host.settings->value(definition.id))}}));
    }
    return Json::object({{"settings", list}});
}

Result settingsGet(const Context &context, const Json &params)
{
    if (!context.host.settings)
        return fail(errors::kUnavailable, "No settings are available.");
    Json values = Json::object();
    for (const Json &id : params.find("settings")->asArray()) {
        if (!offered(id.asString()))
            return fail(errors::kNotFound, "No setting is named \"" + id.asString() + "\".",
                        Json::object({{"setting", id}}));
        values[id.asString()] = settingJson(context.host.settings->value(id.asString()));
    }
    return Json::object({{"values", values}});
}

void read(Registry &registry, std::string name, std::string summary, std::string_view params, std::string_view result,
          SyncHandler handler)
{
    registry.add(Operation{std::move(name), Access::Read, std::move(summary), schema(params), schema(result), 1,
                           std::nullopt, now(std::move(handler))});
}

}

void addApplicationOperations(Registry &registry)
{
    read(registry, "api.version", "The API version: it grows by one with each release that adds to the API.",
         R"({"type": "object", "additionalProperties": false})",
         R"({"type": "object", "properties": {"version": {"type": "integer", "minimum": 0}},
             "required": ["version"], "additionalProperties": false})",
         apiVersion);
    read(registry, "commands.list", "The commands of menus, toolbars and shortcuts, with their enabled state and keys.",
         R"({"type": "object", "properties": {
             "window": {"enum": ["global", "subtitles", "editor", "video", "audio"]},
             "query": {"type": "string", "description": "A case-insensitive part of the command id or its name."}},
             "additionalProperties": false})",
         R"({"type": "object", "properties": {"commands": {"type": "array", "items": {"type": "object", "properties": {
             "command": {"type": "string", "description": "The stable command id, e.g. GLOBAL_SAVE_SUBS."},
             "name": {"type": "string", "description": "The English name the shortcut editor shows."},
             "window": {"enum": ["global", "subtitles", "editor", "video", "audio"]},
             "enabled": {"type": "boolean"},
             "disabledReason": {"type": "string"},
             "keys": {"type": "array", "items": {"type": "string"}}},
             "required": ["command", "name", "window", "enabled", "keys"], "additionalProperties": false}}},
             "required": ["commands"], "additionalProperties": false})",
         commandsList);
    read(registry, "panels.list", "The workspace panels and whether each is open, floating or focused.",
         R"({"type": "object", "additionalProperties": false})",
         R"({"type": "object", "properties": {"panels": {"type": "array", "items": {"type": "object", "properties": {
             "panel": {"type": "string"}, "title": {"type": "string"}, "open": {"type": "boolean"},
             "floating": {"type": "boolean"}, "focused": {"type": "boolean"}},
             "required": ["panel", "title", "open", "floating", "focused"], "additionalProperties": false}}},
             "required": ["panels"], "additionalProperties": false})",
         panelsList);
    read(registry, "settings.list", "Settings with their type, scope, default and current value.",
         R"({"type": "object", "properties": {"prefix": {"type": "string", "description": "Only ids starting with it, e.g. grid."}},
             "additionalProperties": false})",
         R"({"type": "object", "properties": {"settings": {"type": "array", "items": {"type": "object", "properties": {
             "setting": {"type": "string"},
             "type": {"enum": ["boolean", "integer", "string", "stringList"]},
             "scope": {"enum": ["profile", "collections", "workspace"]},
             "default": {"$ref": "#/$defs/SettingValue"},
             "value": {"$ref": "#/$defs/SettingValue"}},
             "required": ["setting", "type", "scope", "default", "value"], "additionalProperties": false}}},
             "required": ["settings"], "additionalProperties": false})",
         settingsList);
    read(registry, "settings.get", "The current values of the settings named.",
         R"({"type": "object", "properties": {"settings": {"type": "array", "items": {"type": "string"}, "minItems": 1}},
             "required": ["settings"], "additionalProperties": false})",
         R"({"type": "object", "properties": {"values": {"type": "object", "additionalProperties": {"$ref": "#/$defs/SettingValue"}}},
             "required": ["values"], "additionalProperties": false})",
         settingsGet);
}

}
