#include "operations.h"

#include "hikari/application/hotkeys.h"

#include <algorithm>
#include <cctype>

namespace hikari::api::detail {

Json panelJson(const PanelState &panel)
{
    return Json::object({{"panel", Json(panel.id)},
                         {"title", Json(panel.title)},
                         {"open", Json(panel.open)},
                         {"floating", Json(panel.floating)},
                         {"focused", Json(panel.focused)}});
}

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
        list.push(panelJson(panel));
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

bool keptFrom(const Caller &caller, std::string_view id)
{
    return caller.reviewed && (id.starts_with("privacy.") || id.starts_with("recent."));
}

Result settingsList(const Context &context, const Json &params)
{
    if (!context.host.settings)
        return fail(errors::kUnavailable, "No settings are available.");
    const std::string prefix = params.find("prefix") ? params.find("prefix")->asString() : std::string();
    Json list = Json::array();
    for (const application::SettingDefinition &definition : application::settingDefinitions()) {
        if (definition.disposition != application::SettingDisposition::Mapped || !definition.id.starts_with(prefix) ||
            keptFrom(context.caller, definition.id))
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
        if (keptFrom(context.caller, id.asString()))
            return fail(errors::kRefused, "AI callers can't read \"" + id.asString() + "\": privacy.* and recent.* settings stay private.",
                        Json::object({{"setting", id}, {"reason", Json("private")}}));
        values[id.asString()] = settingJson(context.host.settings->value(id.asString()));
    }
    return Json::object({{"values", values}});
}

Result commandsRun(const Context &context, const Json &params)
{
    if (!context.host.commands)
        return fail(errors::kUnavailable, "No commands are available.");
    const std::string &command = params.find("command")->asString();
    const int id = application::hotkeyIdOf(command);
    const auto state = id && !application::isRetiredHotkey(id) ? context.host.commands->state(command) : std::nullopt;
    if (!state)
        return fail(errors::kNotFound, "No command is named \"" + command + "\".", Json::object({{"command", Json(command)}}));
    if (!state->enabled)
        return fail(errors::kRefused, state->disabledReason.empty() ? "The command is disabled." : state->disabledReason,
                    Json::object({{"reason", Json("disabled")}}));
    if (!context.host.commands->run(command))
        return fail(errors::kRefused, "The command did not run.", Json::object({{"reason", Json("not_run")}}));
    return Json::object({{"command", Json(command)}, {"ran", Json(true)}});
}

std::optional<PanelState> panelNamed(const PanelHost &panels, std::string_view id)
{
    for (const PanelState &panel : panels.panels())
        if (panel.id == id)
            return panel;
    return std::nullopt;
}

SyncHandler panelChange(std::function<bool(PanelHost &, std::string_view)> change)
{
    return [change = std::move(change)](const Context &context, const Json &params) -> Result {
        if (!context.host.panels)
            return fail(errors::kUnavailable, "No panels are available.");
        const std::string &id = params.find("panel")->asString();
        if (!panelNamed(*context.host.panels, id) || !change(*context.host.panels, id))
            return fail(errors::kNotFound, "No panel is named \"" + id + "\".", Json::object({{"panel", Json(id)}}));
        return panelJson(*panelNamed(*context.host.panels, id));
    };
}

bool fitsType(const Json &value, application::SettingType type)
{
    switch (type) {
    case application::SettingType::Bool: return value.isBool();
    case application::SettingType::Int: return value.isInt();
    case application::SettingType::String: return value.isString();
    case application::SettingType::StringList:
        return value.isArray() && std::all_of(value.asArray().begin(), value.asArray().end(), [](const Json &v) { return v.isString(); });
    }
    return false;
}

application::SettingValue settingValue(const Json &value)
{
    if (value.isBool())
        return value.asBool();
    if (value.isInt())
        return value.asInt();
    if (value.isString())
        return value.asString();
    std::vector<std::string> list;
    for (const Json &item : value.asArray())
        list.push_back(item.asString());
    return list;
}

Result settingsSet(const Context &context, const Json &params)
{
    if (!context.host.settings)
        return fail(errors::kUnavailable, "No settings are available.");
    const std::string &id = params.find("setting")->asString();
    const auto *definition = offered(id);
    if (!definition)
        return fail(errors::kNotFound, "No setting is named \"" + id + "\".", Json::object({{"setting", Json(id)}}));
    const Json &value = *params.find("value");
    if (!fitsType(value, definition->type))
        return fail(errors::kInvalidParams, "Parameter /value: expected " + std::string(typeName(definition->type)) + ".",
                    Json::object({{"pointer", Json("/value")}}));
    if (!context.host.settings->set(id, settingValue(value)))
        return fail(errors::kRefused, "The setting cannot be changed.", Json::object({{"reason", Json("refused")}}));
    return Json::object({{"setting", Json(id)}, {"value", settingJson(context.host.settings->value(id))}});
}

void write(Registry &registry, std::string name, std::string summary, std::string_view params, std::string_view result,
           SyncHandler handler)
{
    registry.add(Operation{std::move(name), Access::Write, std::move(summary), schema(params), schema(result), 1,
                           std::nullopt, now(std::move(handler))});
}

constexpr std::string_view kPanelParams = R"({"type": "object", "properties": {"panel": {"type": "string",
    "description": "A panel id from panels.list."}}, "required": ["panel"], "additionalProperties": false})";
constexpr std::string_view kPanelResult = R"({"type": "object", "properties": {
    "panel": {"type": "string"}, "title": {"type": "string"}, "open": {"type": "boolean"},
    "floating": {"type": "boolean"}, "focused": {"type": "boolean"}},
    "required": ["panel", "title", "open", "floating", "focused"], "additionalProperties": false})";

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
    write(registry, "commands.run", "Runs an enabled command as its menu item, toolbar button or shortcut would.",
          R"({"type": "object", "properties": {"command": {"type": "string", "minLength": 1,
              "description": "A command id from commands.list."}}, "required": ["command"], "additionalProperties": false})",
          R"({"type": "object", "properties": {"command": {"type": "string"}, "ran": {"const": true}},
              "required": ["command", "ran"], "additionalProperties": false})",
          commandsRun);
    write(registry, "panels.open", "Opens a panel where the layout keeps it.", kPanelParams, kPanelResult,
          panelChange([](PanelHost &panels, std::string_view id) { return panels.setOpen(id, true); }));
    write(registry, "panels.close", "Closes a panel.", kPanelParams, kPanelResult,
          panelChange([](PanelHost &panels, std::string_view id) { return panels.setOpen(id, false); }));
    write(registry, "panels.focus", "Opens a panel if needed and gives it the keyboard focus.", kPanelParams, kPanelResult,
          panelChange([](PanelHost &panels, std::string_view id) { return panels.focus(id); }));
    write(registry, "settings.set", "Changes a setting; the value must have the setting's type.",
          R"({"type": "object", "properties": {"setting": {"type": "string"}, "value": {"$ref": "#/$defs/SettingValue"}},
              "required": ["setting", "value"], "additionalProperties": false})",
          R"({"type": "object", "properties": {"setting": {"type": "string"}, "value": {"$ref": "#/$defs/SettingValue"}},
              "required": ["setting", "value"], "additionalProperties": false})",
          settingsSet);
}

}
