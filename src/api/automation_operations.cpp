#include "operations.h"

namespace hikari::api::detail {

namespace {

void automationRun(const Context &context, const Json &params, Reply reply)
{
    if (!context.caller.scriptApproved)
        return reply(fail(errors::kNotGranted, "Only a Lua script the user approved in HikariSub runs."));
    if (!context.host.automation)
        return reply(fail(errors::kUnavailable, "Automation is not available."));
    LuaScript script{params.find("name")->asString(), params.find("script")->asString(), {}};
    if (const Json *macro = params.find("macro"))
        script.macro = macro->asString();
    const auto answer = [reply](LuaRun ran) {
        reply(Json::object({{"file", Json(ran.file)},
                            {"outcome", Json(ran.outcome)},
                            {"message", Json(ran.message)},
                            {"log", Json(ran.log)}}));
    };
    switch (context.host.automation->run(script, answer)) {
    case LuaStart::Started:
        return;
    case LuaStart::Busy:
        return reply(fail(errors::kBusy, "Another macro is running; try again when it ends."));
    case LuaStart::NoDocument:
        return reply(fail(errors::kRefused, "Open a document to run the macro on.", Json::object({{"reason", Json("no_document")}})));
    case LuaStart::NotSaved:
        return reply(fail(errors::kRefused, "The script could not be saved to the AI scripts folder.",
                          Json::object({{"reason", Json("not_saved")}})));
    }
}

bool isPlugin(const Caller &caller)
{
    return caller.id.starts_with("plugin:");
}

Result automationMacros(const Context &context, const Json &params)
{
    if (!context.host.macros)
        return fail(errors::kUnavailable, "Automation is not available.");
    const std::string query = params.find("query") ? lowerAscii(params.find("query")->asString()) : std::string();
    Json list = Json::array();
    for (const MacroInfo &m : context.host.macros->macros()) {
        if (!query.empty() && lowerAscii(m.macro).find(query) == std::string::npos &&
            lowerAscii(m.script).find(query) == std::string::npos && lowerAscii(m.file).find(query) == std::string::npos)
            continue;
        Json entry = Json::object({{"script", Json(m.script)},
                                   {"file", Json(m.file)},
                                   {"macro", Json(m.macro)},
                                   {"ordinal", Json(m.ordinal)},
                                   {"description", Json(m.description)},
                                   {"enabled", Json(m.enabled)}});
        if (!m.enabled && !m.disabledReason.empty())
            entry["disabledReason"] = Json(m.disabledReason);
        list.push(std::move(entry));
    }
    return Json::object({{"macros", list}});
}

std::optional<application::DialogValue> dialogValue(const Json &value)
{
    if (value.isString())
        return value.asString();
    if (value.isBool())
        return value.asBool();
    if (value.isInt())
        return int(value.asInt());
    if (value.isNumber())
        return value.asNumber();
    return std::nullopt;
}

void runMacro(const Context &context, const Json &params, Reply reply)
{
    if (!isPlugin(context.caller))
        return reply(fail(errors::kNotGranted, "Only plugins run installed macros.", Json::object()));
    if (!context.host.macros)
        return reply(fail(errors::kUnavailable, "Automation is not available."));
    MacroRequest request{params.find("file")->asString(), params.find("macro")->asString(), {},
                         params.find("show") && params.find("show")->asString() == "prefilled"};
    if (const Json *answers = params.find("answers"))
        for (const Json &a : answers->asArray()) {
            DialogAnswer answer{a.find("key") ? a.find("key")->asString() : "", {},
                                a.find("button") ? a.find("button")->asString() : "", {}};
            if (const Json *controls = a.find("controls"))
                for (const Json &c : controls->asArray())
                    answer.controls.push_back(DialogControlInfo{c.find("name")->asString(), c.find("kind")->asString(), {}, {}, {}, {}, {}, {}});
            for (const Json::Member &v : a.find("values")->asObject())
                if (const auto value = dialogValue(v.value))
                    answer.values.insert_or_assign(v.key, *value);
            request.answers.push_back(std::move(answer));
        }
    const auto refused = [&](std::string_view reason, std::string message) {
        reply(fail(errors::kRefused, std::move(message), Json::object({{"reason", Json(reason)}})));
    };
    const auto answer = [reply, caller = context.caller, documents = context.host.documents](LuaRun ran) {
        Json result = Json::object({{"outcome", Json(ran.outcome)}, {"message", Json(ran.message)}, {"log", Json(ran.log)}});
        const auto target = documents ? documents->workspace().editingTarget() : std::nullopt;
        if (ran.document && sees(caller, target, *ran.document))
            result["dialogs"] = dialogsJson(ran.dialogs);
        reply(std::move(result));
    };
    switch (context.host.macros->runMacro(request, answer)) {
    case MacroStart::Started:
        return;
    case MacroStart::Busy:
        return reply(fail(errors::kBusy, "Another macro is running; try again when it ends."));
    case MacroStart::NoDocument:
        return refused("no_document", "Open a document to run the macro on.");
    case MacroStart::NotFound:
        return reply(fail(errors::kNotFound, "No loaded macro \"" + request.macro + "\" in " + request.file + ".",
                          Json::object({{"file", Json(request.file)}, {"macro", Json(request.macro)}})));
    case MacroStart::Ambiguous:
        return refused("ambiguous", "More than one loaded script is named " + request.file + ".");
    case MacroStart::Disabled:
        return refused("disabled", "The macro's script is not loaded or not shown in this window.");
    }
}

}

void addAutomationOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "automation.macros",
                 "The loaded Automation macros, as the Automation menu lists them.",
                 R"({"type": "object", "properties": {"query": {"type": "string",
                     "description": "Keep macros whose name, script or file name holds this, ignoring case."}},
                     "additionalProperties": false})",
                 R"({"type": "object", "properties": {"macros": {"type": "array", "items": {"type": "object", "properties": {
                     "script": {"type": "string", "description": "The script's name."},
                     "file": {"type": "string", "description": "The script's file name."},
                     "macro": {"type": "string"}, "ordinal": {"type": "integer", "minimum": 0,
                         "description": "The macro's place among the script's, from 0."},
                     "description": {"type": "string"},
                     "enabled": {"type": "boolean", "description": "The script is loaded and shown in the user's window."},
                     "disabledReason": {"type": "string"}},
                     "required": ["script", "file", "macro", "ordinal", "description", "enabled"],
                     "additionalProperties": false}}},
                     "required": ["macros"], "additionalProperties": false})",
                 now(automationMacros));
    addOperation(registry, Access::Write, "automation.run_macro",
                 "Runs a loaded Automation macro on the active tab's selection, as the menu would, as one undo step. "
                 "Answers fill its dialogs. Plugins only.",
                 R"({"type": "object", "properties": {
                     "file": {"type": "string", "minLength": 1, "description": "The script's file name."},
                     "macro": {"type": "string", "minLength": 1},
                     "answers": {"type": "array", "maxItems": 64, "items": {"$ref": "#/$defs/DialogAnswer"},
                         "description": "One per dialog, in order, as macro.ran gives them."},
                     "show": {"enum": ["never", "prefilled"],
                         "description": "never (the default) answers a dialog whose key matches without showing it; prefilled shows it filled in. A dialog without a matching answer, and a question, is always shown."}},
                     "required": ["file", "macro"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {
                     "outcome": {"enum": ["applied", "refused", "failed", "cancelled", "declined", "stopped", "lost"]},
                     "message": {"type": "string"}, "log": {"type": "string"},
                     "dialogs": {"type": "array", "items": {"$ref": "#/$defs/DialogAnswer"},
                         "description": "The answers its dialogs got, as macro.ran gives them; only when the caller sees the Document it ran on."}},
                     "required": ["outcome", "message", "log"], "additionalProperties": false})",
                 runMacro);
    addOperation(registry, Access::Write, "automation.run",
                 "Saves a Lua Automation script to the AI scripts folder and runs one of its macros on the active tab's "
                 "selection, once the user approves the script. The run is one undo step.",
                 R"({"type": "object", "properties": {
                     "name": {"type": "string", "pattern": "^[A-Za-z0-9][A-Za-z0-9 _.-]*$", "maxLength": 64,
                              "description": "The script's file name without .lua; an earlier script of that name is replaced."},
                     "script": {"type": "string", "minLength": 1, "maxLength": 262144,
                                "description": "The whole Lua script, registering its macro with aegisub.register_macro."},
                     "macro": {"type": "string", "minLength": 1,
                               "description": "The macro to run, by its registered name; default the first."}},
                     "required": ["name", "script"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"file": {"type": "string", "description": "The saved script's file name."},
                     "outcome": {"enum": ["applied", "refused", "failed", "cancelled", "declined", "stopped", "lost", "invalid"]},
                     "message": {"type": "string"}, "log": {"type": "string"}},
                     "required": ["file", "outcome", "message", "log"], "additionalProperties": false})",
                 automationRun);
}

}
