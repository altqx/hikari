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

}

void addAutomationOperations(Registry &registry)
{
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
