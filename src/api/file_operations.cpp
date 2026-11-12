#include "operations.h"

#include "hikari/api/folder.h"

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace hikari::api::detail {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;

std::string utf8(const fs::path &p)
{
    const std::u8string text = p.u8string();
    return std::string(text.begin(), text.end());
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

Json grantedJson(const GrantedFile &file)
{
    return Json::object({{"file", Json(file.handle)},
                         {"name", Json(file.name)},
                         {"access", Json(file.writable ? "write" : "read")}});
}

Json grantedJson(const GrantedFolder &folder)
{
    return Json::object({{"folder", Json(folder.handle)},
                         {"name", Json(folder.name)},
                         {"access", Json(folder.writable ? "write" : "read")}});
}

std::expected<const GrantedFolder *, Error> grantedFolder(const Context &context, const std::string &handle)
{
    for (const GrantedFolder &folder : context.folders)
        if (folder.handle == handle)
            return &folder;
    return fail(errors::kNotGranted, "No folder \"" + handle + "\" was granted to this caller; see files.list.",
                Json::object({{"folder", Json(handle)}}));
}

std::expected<fs::path, Error> inside(const GrantedFolder &folder, const std::string &path)
{
    auto real = pathInside(pathOf(folder.path), path);
    if (!real)
        return fail(errors::kNotGranted, "\"" + path + "\": " + real.error() + ".", Json::object({{"path", Json(path)}}));
    return *real;
}

std::vector<std::string> aiExcludes(const Context &context, const GrantedFolder &folder)
{
    if (!context.caller.reviewed)
        return {};
    std::vector<std::string> globs(std::begin(kAiExcludeDefaults), std::end(kAiExcludeDefaults));
    if (context.host.settings)
        std::ranges::move(context.host.settings->list("ai.exclude"), std::back_inserter(globs));
    if (context.host.project)
        std::ranges::move(context.host.project->aiExclude(folder.path), std::back_inserter(globs));
    return globs;
}

std::optional<Error> excluded(const Context &context, const GrantedFolder &folder, const std::string &path,
                              const fs::path &real, bool isFolder)
{
    const std::vector<std::string> globs = aiExcludes(context, folder);
    if (globs.empty())
        return std::nullopt;
    std::error_code error;
    const std::u8string generic = real.lexically_relative(fs::canonical(pathOf(folder.path), error)).generic_u8string();
    const std::string resolved(generic.begin(), generic.end());
    if (!excludedPath(path, isFolder, globs) && !excludedPath(resolved == "." ? std::string() : resolved, isFolder, globs))
        return std::nullopt;
    return Error{std::string(errors::kRefused), "\"" + path + "\" matches ai.exclude, so it is never sent to AI.",
                 Json::object({{"path", Json(path)}, {"reason", Json("excluded")}})};
}

bool mediaFile(const std::string &path)
{
    static constexpr std::string_view kMedia[] = {
        "avi", "mp4", "mkv",  "ogm", "wmv", "asf", "rmvb", "rm",  "3gp", "ts",   "m2ts", "m4v", "flv",  "webm", "mpg",
        "mpeg", "mov", "wav", "mp3", "flac", "ogg", "opus", "m4a", "aac", "ac3", "eac3", "dts", "wma", "mka", "aiff"};
    const std::size_t dot = path.find_last_of("./\\");
    if (dot == std::string::npos || path[dot] != '.')
        return false;
    std::string extension = path.substr(dot + 1);
    std::ranges::transform(extension, extension.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    return std::ranges::find(kMedia, extension) != std::end(kMedia);
}

// The file a call names: a granted `file`, or a `path` inside a granted `folder`, which grants it.
std::expected<GrantedFile, Error> named(const Context &context, const Json &params)
{
    const Json *file = params.find("file");
    const Json *folderParam = params.find("folder");
    const Json *path = params.find("path");
    if ((file != nullptr) == (folderParam != nullptr) || (folderParam != nullptr) != (path != nullptr))
        return fail(errors::kInvalidParams, "Name a file, or a folder and a path.", Json::object({{"pointer", Json("")}}));
    if (file) {
        for (const GrantedFile &granted : context.files)
            if (granted.handle == file->asString())
                return granted;
        return fail(errors::kNotGranted, "No file \"" + file->asString() + "\" was granted to this caller; see files.list.",
                    Json::object({{"file", Json(file->asString())}}));
    }
    const auto folder = grantedFolder(context, folderParam->asString());
    if (!folder)
        return std::unexpected(folder.error());
    if (path->asString().empty())
        return fail(errors::kInvalidParams, "Parameter /path: name a file inside the folder.",
                    Json::object({{"pointer", Json("/path")}}));
    const auto real = inside(**folder, path->asString());
    if (!real)
        return std::unexpected(real.error());
    std::error_code error;
    if (fs::is_directory(*real, error))
        return fail(errors::kRefused, "\"" + path->asString() + "\" is a folder.", Json::object({{"reason", Json("folder")}}));
    if (auto refused = excluded(context, **folder, path->asString(), *real, false))
        return std::unexpected(std::move(*refused));
    const std::string resolved = utf8(*real);
    return GrantedFile{context.grantFile(resolved, (*folder)->writable), path->asString(), resolved, (*folder)->writable};
}

bool base64Encoding(const Json &params)
{
    const Json *encoding = params.find("encoding");
    return encoding && encoding->asString() == "base64";
}

Result folderList(const Context &context, const std::string &handle, const std::string &path, Json out)
{
    const auto folder = grantedFolder(context, handle);
    if (!folder)
        return std::unexpected(folder.error());
    const auto real = inside(**folder, path);
    if (!real)
        return std::unexpected(real.error());
    if (auto refused = excluded(context, **folder, path, *real, true))
        return std::unexpected(std::move(*refused));
    const auto listed = folderEntries(pathOf((*folder)->path), *real);
    if (!listed)
        return fail(errors::kNotFound, "\"" + path + "\" isn't a folder.", Json::object({{"path", Json(path)}}));
    const std::vector<std::string> globs = aiExcludes(context, **folder);
    Json entries = Json::array();
    for (const FolderEntry &entry : listed->entries) {
        const std::string inner = path.empty() ? entry.name : path + "/" + entry.name;
        if (!excludedPath(inner, entry.folder, globs))
            entries.push(Json::object({{"path", Json(inner)}, {"folder", Json(entry.folder)}}));
    }
    out["entries"] = std::move(entries);
    if (listed->truncated)
        out["truncated"] = Json(true);
    return out;
}

Result filesList(const Context &context, const Json &params)
{
    Json files = Json::array();
    for (const GrantedFile &file : context.files)
        files.push(grantedJson(file));
    Json folders = Json::array();
    for (const GrantedFolder &folder : context.folders)
        folders.push(grantedJson(folder));
    Json out = Json::object({{"files", std::move(files)}, {"folders", std::move(folders)}});
    const Json *folder = params.find("folder");
    const Json *path = params.find("path");
    if (!folder && path)
        return fail(errors::kInvalidParams, "Parameter /path: name the folder too.", Json::object({{"pointer", Json("/path")}}));
    if (!folder)
        return out;
    return folderList(context, folder->asString(), path ? path->asString() : std::string(), std::move(out));
}

Result filesRead(const Context &context, const Json &params)
{
    if (!context.host.files)
        return fail(errors::kUnavailable, "Files are not available.");
    const auto file = named(context, params);
    if (!file)
        return std::unexpected(file.error());
    if (context.caller.reviewed && mediaFile(file->path))
        return fail(errors::kRefused, "\"" + file->name + "\" is a media file; read it through video.frame or audio.peaks.",
                    Json::object({{"reason", Json("media")}}));
    const auto bytes = context.host.files->read(file->path, kMaxFileBytes + 1);
    if (!bytes)
        return fail(errors::kNotFound, "\"" + file->name + "\" cannot be read.", Json::object({{"file", Json(file->handle)}}));
    if (bytes->size() > kMaxFileBytes)
        return fail(errors::kRefused, "\"" + file->name + "\" is larger than 16 MiB.",
                    Json::object({{"reason", Json("too_large")}}));
    const bool asBase64 = base64Encoding(params);
    if (!asBase64 && !isUtf8(*bytes))
        return fail(errors::kRefused, "\"" + file->name + "\" is not UTF-8 text; read it as base64.",
                    Json::object({{"reason", Json("not_text")}}));
    return Json::object({{"file", Json(file->handle)},
                         {"size", Json(static_cast<std::int64_t>(bytes->size()))},
                         {"encoding", Json(asBase64 ? "base64" : "utf8")},
                         {"data", Json(asBase64 ? base64(*bytes) : *bytes)}});
}

Result filesWrite(const Context &context, const Json &params)
{
    if (!context.host.files)
        return fail(errors::kUnavailable, "Files are not available.");
    const auto file = named(context, params);
    if (!file)
        return std::unexpected(file.error());
    if (!file->writable)
        return fail(errors::kNotGranted, "\"" + file->name + "\" was granted for reading only.",
                    Json::object({{"file", Json(file->handle)}}));
    const std::string &data = params.find("data")->asString();
    const auto bytes = base64Encoding(params) ? fromBase64(data) : std::optional(data);
    if (!bytes)
        return fail(errors::kInvalidParams, "Parameter /data: not base64.", Json::object({{"pointer", Json("/data")}}));
    if (bytes->size() > kMaxFileBytes)
        return fail(errors::kRefused, "The data is larger than 16 MiB.", Json::object({{"reason", Json("too_large")}}));
    if (params.find("folder")) {
        std::error_code error;
        fs::create_directories(pathOf(file->path).parent_path(), error);
    }
    if (!context.host.files->write(file->path, *bytes))
        return fail(errors::kRefused, "\"" + file->name + "\" could not be written.",
                    Json::object({{"reason", Json("not_written")}}));
    return Json::object({{"file", Json(file->handle)}, {"size", Json(static_cast<std::int64_t>(bytes->size()))}});
}

constexpr std::string_view kNamed =
    R"("file": {"type": "string", "description": "A handle from files.list."},
       "folder": {"type": "string", "description": "Instead of file, a folder handle from files.list."},
       "path": {"type": "string", "description": "With folder, the file's path inside it, with / between its parts."})";
constexpr std::string_view kEncoding = R"("encoding": {"enum": ["utf8", "base64"], "description": "Default utf8."})";

}

void addFileOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "files.list",
                 "The files and folders the user granted this caller; with folder, also what that folder holds.",
                 R"({"type": "object", "properties": {
                     "folder": {"type": "string", "description": "A folder handle to list."},
                     "path": {"type": "string", "description": "A folder inside it; default the folder itself."}},
                     "additionalProperties": false})",
                 R"({"type": "object", "properties": {"files": {"type": "array", "items": {"$ref": "#/$defs/GrantedFile"}},
                     "folders": {"type": "array", "items": {"$ref": "#/$defs/GrantedFolder"}},
                     "entries": {"type": "array", "items": {"$ref": "#/$defs/FolderEntry"},
                                 "description": "With folder: in name order, at most 4096, without links that lead outside it."},
                     "truncated": {"type": "boolean", "description": "Present when the folder holds more than 4096 entries."}},
                     "required": ["files"], "additionalProperties": false})",
                 now(filesList));
    addOperation(registry, Access::Read, "files.read", "A granted file's content, as UTF-8 text or base64; at most 16 MiB.",
                 R"({"type": "object", "properties": {)" + std::string(kNamed) + ", " + std::string(kEncoding) +
                     R"(}, "additionalProperties": false})",
                 R"({"type": "object", "properties": {"file": {"type": "string"}, "size": {"type": "integer", "minimum": 0},
                     "encoding": {"enum": ["utf8", "base64"]}, "data": {"type": "string"}},
                     "required": ["file", "size", "encoding", "data"], "additionalProperties": false})",
                 now(filesRead));
    addOperation(registry, Access::Write, "files.write",
                 "Replaces a file granted for writing with new content; in a writable folder, makes it and its folders.",
                 R"({"type": "object", "properties": {)" + std::string(kNamed) + ", " + std::string(kEncoding) +
                     R"(, "data": {"type": "string"}}, "required": ["data"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"file": {"type": "string"}, "size": {"type": "integer", "minimum": 0}},
                     "required": ["file", "size"], "additionalProperties": false})",
                 now(filesWrite));
}

}
