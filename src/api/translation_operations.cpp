#include "operations.h"

#include <array>
#include <filesystem>

namespace hikari::api::detail {

namespace {

constexpr std::size_t kMaxProjectFileBytes = 16 * 1024 * 1024;
constexpr std::array<std::string_view, 4> kProjectFiles{"PROJECT.md", "GLOSSARY.md", "CHARACTERS.md", "STYLE.md"};

struct Folder {
    std::filesystem::path path;
    FileHost *files = nullptr;
};

std::expected<std::optional<Folder>, Error> folderOf(const Context &context, const Json &params)
{
    auto t = target(context, params);
    if (!t)
        return std::unexpected(t.error());
    if (!context.host.translation || !context.host.files)
        return fail(errors::kUnavailable, "Project folders are not available.");
    const auto folder = context.host.translation->projectFolder(t->id);
    if (!folder)
        return std::nullopt;
    return Folder{std::filesystem::path(std::u8string(folder->begin(), folder->end())), context.host.files};
}

std::string fileOf(const std::filesystem::path &folder, std::string_view name)
{
    const std::u8string path = (folder / std::u8string_view(reinterpret_cast<const char8_t *>(name.data()), name.size())).u8string();
    return {reinterpret_cast<const char *>(path.data()), path.size()};
}

Result translationProject(const Context &context, const Json &params)
{
    const auto folder = folderOf(context, params);
    if (!folder)
        return std::unexpected(folder.error());
    Json files = Json::array();
    if (!*folder)
        return Json::object({{"folder", Json()}, {"files", std::move(files)}});
    for (const std::string_view name : kProjectFiles) {
        const auto bytes = (*folder)->files->read(fileOf((*folder)->path, name), kMaxProjectFileBytes + 1);
        if (!bytes || bytes->size() > kMaxProjectFileBytes || !isUtf8(*bytes))
            continue;
        files.push(Json::object({{"name", Json(std::string(name))}, {"data", Json(*bytes)}}));
    }
    const std::u8string name = (*folder)->path.filename().empty() ? (*folder)->path.parent_path().filename().u8string()
                                                                  : (*folder)->path.filename().u8string();
    return Json::object({{"folder", Json(narrow(name))}, {"files", std::move(files)}});
}

Result translationProjectWrite(const Context &context, const Json &params)
{
    const auto folder = folderOf(context, params);
    if (!folder)
        return std::unexpected(folder.error());
    if (!*folder)
        return fail(errors::kNotFound, "This Document has no project folder.");
    const std::string &name = params.find("file")->asString();
    const std::string &data = params.find("data")->asString();
    if (data.size() > kMaxProjectFileBytes)
        return fail(errors::kRefused, "The data is larger than 16 MiB.", Json::object({{"reason", Json("too_large")}}));
    if (!(*folder)->files->write(fileOf((*folder)->path, name), data))
        return fail(errors::kRefused, "\"" + name + "\" could not be written.", Json::object({{"reason", Json("not_written")}}));
    return Json::object({{"file", Json(name)}, {"size", Json(static_cast<std::int64_t>(data.size()))}});
}

constexpr std::string_view kDocument = R"("document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."})";
constexpr std::string_view kNames = R"({"enum": ["PROJECT.md", "GLOSSARY.md", "CHARACTERS.md", "STYLE.md"]})";

}

void addTranslationOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "translation.project",
                 "The Document's Honya project files: PROJECT.md, GLOSSARY.md, CHARACTERS.md and STYLE.md, those that exist. "
                 "folder is null when the Document has no project folder.",
                 R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(}, "additionalProperties": false})",
                 R"({"type": "object", "properties": {"folder": {"type": ["string", "null"]},
                     "files": {"type": "array", "items": {"type": "object", "properties": {"name": )" + std::string(kNames) +
                     R"(, "data": {"type": "string"}}, "required": ["name", "data"], "additionalProperties": false}}},
                     "required": ["folder", "files"], "additionalProperties": false})",
                 now(translationProject));
    addOperation(registry, Access::Write, "translation.project_write",
                 "Replaces one of the Document's Honya project files, keeping Honya's Markdown and honya:data format.",
                 R"({"type": "object", "properties": {)" + std::string(kDocument) + R"(, "file": )" + std::string(kNames) +
                     R"(, "data": {"type": "string"}}, "required": ["file", "data"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"file": )" + std::string(kNames) +
                     R"(, "size": {"type": "integer", "minimum": 0}}, "required": ["file", "size"], "additionalProperties": false})",
                 now(translationProjectWrite));
}

}
