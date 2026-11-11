#include "operations.h"

#include <algorithm>

namespace hikari::api::detail {

namespace {

constexpr std::size_t kMaxSkillFileBytes = 256 * 1024;

std::expected<const SkillHost *, Error> skillHost(const Context &context)
{
    if (!context.caller.skills)
        return fail(errors::kNotGranted, "This caller may not read the user's skills.");
    if (!context.host.skills)
        return fail(errors::kUnavailable, "Skills are not available.");
    return context.host.skills;
}

Result skillsList(const Context &context, const Json &)
{
    const auto host = skillHost(context);
    if (!host)
        return std::unexpected(host.error());
    Json skills = Json::array();
    for (const SkillFolder &skill : (*host)->skills()) {
        Json files = Json::array();
        for (const std::string &file : skill.files)
            files.push(Json(file));
        skills.push(Json::object({{"skill", Json(skill.name)}, {"files", std::move(files)}}));
    }
    return Json::object({{"skills", std::move(skills)}});
}

Result skillsRead(const Context &context, const Json &params)
{
    const auto host = skillHost(context);
    if (!host)
        return std::unexpected(host.error());
    const std::string &skill = params.find("skill")->asString();
    const std::string &file = params.find("file")->asString();
    const auto where = Json::object({{"skill", Json(skill)}, {"file", Json(file)}});
    const auto bytes = (*host)->read(skill, file, kMaxSkillFileBytes + 1);
    if (!bytes)
        return fail(errors::kNotFound, "The skill \"" + skill + "\" has no file \"" + file + "\"; see skills.list.", where);
    if (bytes->size() > kMaxSkillFileBytes)
        return fail(errors::kRefused, "\"" + file + "\" is larger than 256 KiB.", Json::object({{"reason", Json("too_large")}}));
    if (!isUtf8(*bytes))
        return fail(errors::kRefused, "\"" + file + "\" is not UTF-8 text.", Json::object({{"reason", Json("not_text")}}));
    return Json::object({{"skill", Json(skill)},
                         {"file", Json(file)},
                         {"size", Json(static_cast<std::int64_t>(bytes->size()))},
                         {"data", Json(*bytes)}});
}

}

void addSkillOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "skills.list",
                 "The user's skills: each skill folder's name and its files, SKILL.md among them.",
                 R"({"type": "object", "additionalProperties": false})",
                 R"({"type": "object", "properties": {"skills": {"type": "array", "items": {"type": "object",
                     "properties": {"skill": {"$ref": "#/$defs/SkillName"},
                                    "files": {"type": "array", "items": {"type": "string"}}},
                     "required": ["skill", "files"], "additionalProperties": false}}},
                     "required": ["skills"], "additionalProperties": false})",
                 now(skillsList));
    addOperation(registry, Access::Read, "skills.read", "One file of a user's skill, as UTF-8 text; at most 256 KiB.",
                 R"({"type": "object", "properties": {"skill": {"$ref": "#/$defs/SkillName"},
                     "file": {"type": "string", "minLength": 1, "maxLength": 512,
                              "description": "A path skills.list gives, such as SKILL.md or references/tags.md."}},
                     "required": ["skill", "file"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"skill": {"$ref": "#/$defs/SkillName"}, "file": {"type": "string"},
                     "size": {"type": "integer", "minimum": 0}, "data": {"type": "string"}},
                     "required": ["skill", "file", "size", "data"], "additionalProperties": false})",
                 now(skillsRead));
}

}
