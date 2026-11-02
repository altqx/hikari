#include "operations.h"

namespace hikari::api::detail {

namespace {

constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;

Json grantedJson(const GrantedFile &file)
{
    return Json::object({{"file", Json(file.handle)},
                         {"name", Json(file.name)},
                         {"access", Json(file.writable ? "write" : "read")}});
}

std::expected<const GrantedFile *, Error> granted(const Context &context, const Json &params)
{
    const std::string &handle = params.find("file")->asString();
    for (const GrantedFile &file : context.files)
        if (file.handle == handle)
            return &file;
    return fail(errors::kNotGranted, "No file \"" + handle + "\" was granted to this caller; see files.list.",
                Json::object({{"file", Json(handle)}}));
}

bool base64Encoding(const Json &params)
{
    const Json *encoding = params.find("encoding");
    return encoding && encoding->asString() == "base64";
}

Result filesList(const Context &context, const Json &)
{
    Json files = Json::array();
    for (const GrantedFile &file : context.files)
        files.push(grantedJson(file));
    return Json::object({{"files", std::move(files)}});
}

Result filesRead(const Context &context, const Json &params)
{
    if (!context.host.files)
        return fail(errors::kUnavailable, "Files are not available.");
    const auto file = granted(context, params);
    if (!file)
        return std::unexpected(file.error());
    const auto bytes = context.host.files->read((*file)->path, kMaxFileBytes + 1);
    if (!bytes)
        return fail(errors::kNotFound, "\"" + (*file)->name + "\" cannot be read.",
                    Json::object({{"file", Json((*file)->handle)}}));
    if (bytes->size() > kMaxFileBytes)
        return fail(errors::kRefused, "\"" + (*file)->name + "\" is larger than 16 MiB.",
                    Json::object({{"reason", Json("too_large")}}));
    const bool asBase64 = base64Encoding(params);
    if (!asBase64 && !isUtf8(*bytes))
        return fail(errors::kRefused, "\"" + (*file)->name + "\" is not UTF-8 text; read it as base64.",
                    Json::object({{"reason", Json("not_text")}}));
    return Json::object({{"file", Json((*file)->handle)},
                         {"size", Json(static_cast<std::int64_t>(bytes->size()))},
                         {"encoding", Json(asBase64 ? "base64" : "utf8")},
                         {"data", Json(asBase64 ? base64(*bytes) : *bytes)}});
}

Result filesWrite(const Context &context, const Json &params)
{
    if (!context.host.files)
        return fail(errors::kUnavailable, "Files are not available.");
    const auto file = granted(context, params);
    if (!file)
        return std::unexpected(file.error());
    if (!(*file)->writable)
        return fail(errors::kNotGranted, "\"" + (*file)->name + "\" was granted for reading only.",
                    Json::object({{"file", Json((*file)->handle)}}));
    const std::string &data = params.find("data")->asString();
    const auto bytes = base64Encoding(params) ? fromBase64(data) : std::optional(data);
    if (!bytes)
        return fail(errors::kInvalidParams, "Parameter /data: not base64.", Json::object({{"pointer", Json("/data")}}));
    if (bytes->size() > kMaxFileBytes)
        return fail(errors::kRefused, "The data is larger than 16 MiB.", Json::object({{"reason", Json("too_large")}}));
    if (!context.host.files->write((*file)->path, *bytes))
        return fail(errors::kRefused, "\"" + (*file)->name + "\" could not be written.",
                    Json::object({{"reason", Json("not_written")}}));
    return Json::object({{"file", Json((*file)->handle)}, {"size", Json(static_cast<std::int64_t>(bytes->size()))}});
}

constexpr std::string_view kFileParam = R"("file": {"type": "string", "description": "A handle from files.list."})";
constexpr std::string_view kEncoding = R"("encoding": {"enum": ["utf8", "base64"], "description": "Default utf8."})";

}

void addFileOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "files.list", "The files the user granted this caller, with read or write access.",
                 R"({"type": "object", "additionalProperties": false})",
                 R"({"type": "object", "properties": {"files": {"type": "array", "items": {"$ref": "#/$defs/GrantedFile"}}},
                     "required": ["files"], "additionalProperties": false})",
                 now(filesList));
    addOperation(registry, Access::Read, "files.read", "A granted file's content, as UTF-8 text or base64; at most 16 MiB.",
                 R"({"type": "object", "properties": {)" + std::string(kFileParam) + ", " + std::string(kEncoding) +
                     R"(}, "required": ["file"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"file": {"type": "string"}, "size": {"type": "integer", "minimum": 0},
                     "encoding": {"enum": ["utf8", "base64"]}, "data": {"type": "string"}},
                     "required": ["file", "size", "encoding", "data"], "additionalProperties": false})",
                 now(filesRead));
    addOperation(registry, Access::Write, "files.write", "Replaces a file granted for writing with new content.",
                 R"({"type": "object", "properties": {)" + std::string(kFileParam) + ", " + std::string(kEncoding) +
                     R"(, "data": {"type": "string"}}, "required": ["file", "data"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"file": {"type": "string"}, "size": {"type": "integer", "minimum": 0}},
                     "required": ["file", "size"], "additionalProperties": false})",
                 now(filesWrite));
}

}
