#include "operations.h"

namespace hikari::api {

namespace detail {

Json projectJson(const Host &host)
{
    const std::optional<ProjectState> project = host.project ? host.project->project() : std::nullopt;
    if (!project)
        return Json();
    return Json::object({{"name", Json(project->name)}, {"trusted", Json(project->trusted)}});
}

namespace {

constexpr std::string_view kProjectResult = R"({"type": "object", "properties": {"project": {"$ref": "#/$defs/Project"}},
    "required": ["project"], "additionalProperties": false})";

Result projectInfo(const Context &context, const Json &)
{
    return Json::object({{"project", projectJson(context.host)}});
}

}

void addProjectOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "project.info", "The Project of the window the user works in, or null.",
                 R"({"type": "object", "additionalProperties": false})", kProjectResult, now(projectInfo), 2);
    registry.addEvent(EventType{"project.changed",
                                "The window the user works in opened or closed a Project, or the user moved to a window "
                                "with another one.",
                                schema(kProjectResult), 2});
}

}

void Api::projectChanged()
{
    publish(Event{"project.changed", Json::object({{"project", detail::projectJson(m_host)}})}, std::nullopt);
}

}
