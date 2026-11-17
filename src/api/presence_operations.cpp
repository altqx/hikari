#include "operations.h"

namespace hikari::api::detail {

namespace {

constexpr std::string_view kPluginPrefix = "plugin:";

constexpr std::string_view kSetTextParams = R"({"type": "object", "properties": {
    "text": {"type": "string", "maxLength": 64, "pattern": "^[^\\u0000-\\u001F\\u007F]*$", "description": "Plain text; empty clears it."}},
    "required": ["text"], "additionalProperties": false})";

constexpr std::string_view kSetTextResult = R"({"type": "object", "additionalProperties": false})";

Result setText(const Context &context, const Json &params)
{
    if (!context.caller.id.starts_with(kPluginPrefix) || !context.caller.presence)
        return fail(errors::kNotGranted, "Only a plugin whose manifest asks for presence may set Discord status text.");
    if (!context.host.presence)
        return fail(errors::kUnavailable, "Discord presence isn't available here.");
    context.host.presence->setPluginText(std::string_view(context.caller.id).substr(kPluginPrefix.size()),
                                         params.find("text")->asString());
    return Json::object();
}

}

void addPresenceOperations(Registry &registry)
{
    addOperation(registry, Access::Write, "presence.set_text",
                 "Sets or clears the caller's text for {plugin} in the user's Discord status. Plugins whose manifest "
                 "asks for presence only.",
                 kSetTextParams, kSetTextResult, now(setText));
}

}
