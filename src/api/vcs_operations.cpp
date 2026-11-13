#include "operations.h"

#include <filesystem>

namespace hikari::api::detail {

namespace {

namespace fs = std::filesystem;

constexpr int kDefaultCommits = 50;

constexpr std::string_view kRefsParams = R"({"type": "object", "properties": {
    "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document; an unsaved one gives the Project's repository."},
    "limit": {"type": "integer", "minimum": 1, "maximum": 200, "description": "How many recent commits; 50 when left out."}},
    "additionalProperties": false})";

constexpr std::string_view kRefsResult = R"({"type": "object", "properties": {
    "root": {"type": "string", "description": "The repository's work tree."},
    "path": {"type": ["string", "null"], "description": "The Document's path in the repository, '/'-separated; null for the Project's."},
    "head": {"anyOf": [{"type": "null"}, {"type": "object", "properties": {
        "ref": {"type": ["string", "null"], "description": "The current branch; null when detached."},
        "commit": {"type": "string"}}, "required": ["ref", "commit"], "additionalProperties": false}],
        "description": "null before the first commit."},
    "branches": {"type": "array", "items": {"type": "object", "properties": {"name": {"type": "string"},
        "commit": {"type": "string"}, "current": {"type": "boolean"}}, "required": ["name", "commit", "current"],
        "additionalProperties": false}, "description": "At most 500, newest first."},
    "tags": {"type": "array", "items": {"type": "object", "properties": {"name": {"type": "string"},
        "commit": {"type": "string"}}, "required": ["name", "commit"], "additionalProperties": false},
        "description": "At most 500, newest first."},
    "commits": {"type": "array", "items": {"type": "object", "properties": {"commit": {"type": "string"},
        "summary": {"type": "string"}, "author": {"type": "string"},
        "time": {"type": "integer", "description": "Seconds since 1970, when it was committed."}},
        "required": ["commit", "summary", "author", "time"], "additionalProperties": false},
        "description": "The newest from HEAD."}},
    "required": ["root", "path", "head", "branches", "tags", "commits"], "additionalProperties": false})";

constexpr std::string_view kShowParams = R"({"type": "object", "properties": {
    "ref": {"type": "string", "minLength": 1, "maxLength": 256, "pattern": "^[A-Za-z0-9._/@{}~^][A-Za-z0-9._/@{}~^-]*$",
        "description": "A branch, tag, commit or revision such as HEAD~2."},
    "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."}},
    "required": ["ref"], "additionalProperties": false})";

constexpr std::string_view kShowResult = R"({"type": "object", "properties": {
    "ref": {"type": "string"}, "commit": {"type": "string"},
    "path": {"type": "string", "description": "The Document's path in the repository, '/'-separated."},
    "text": {"type": "string", "description": "The file at that commit, as UTF-8."}},
    "required": ["ref", "commit", "path", "text"], "additionalProperties": false})";

std::unexpected<Error> failure(const application::VcsFailure &f)
{
    const auto because = [&](std::string_view code, std::string_view reason) {
        return fail(code, f.message, Json::object({{"reason", Json(std::string(reason))}}));
    };
    switch (f.problem) {
    case application::VcsProblem::NoGit: return because(errors::kUnavailable, "no_git");
    case application::VcsProblem::TimedOut: return because(errors::kUnavailable, "timed_out");
    case application::VcsProblem::NoRepository: return because(errors::kNotFound, "no_repository");
    case application::VcsProblem::UnsafeRepository: return because(errors::kRefused, "unsafe_repository");
    case application::VcsProblem::NoRef: return because(errors::kNotFound, "no_ref");
    case application::VcsProblem::NoFile: return because(errors::kNotFound, "no_file");
    case application::VcsProblem::TooLarge: return because(errors::kRefused, "too_large");
    case application::VcsProblem::NotText: return because(errors::kRefused, "not_text");
    case application::VcsProblem::Failed: break;
    }
    return because(errors::kUnavailable, "failed");
}

std::optional<Error> refused(const Context &context)
{
    if (!context.caller.id.starts_with("plugin:") || !context.caller.vcsRead)
        return Error{std::string(errors::kNotGranted), "Only a plugin whose manifest asks for vcs.read may read git.", {}};
    if (!context.host.vcs)
        return Error{std::string(errors::kUnavailable), "Git isn't available here.", {}};
    return std::nullopt;
}

std::unexpected<Error> noPath()
{
    return fail(errors::kNotFound, "The Document was never saved, so it isn't in a repository.",
                Json::object({{"reason", Json("no_path")}}));
}

std::string utf8(const fs::path &p)
{
    const std::u8string text = p.u8string();
    return std::string(text.begin(), text.end());
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::optional<std::string> documentPath(const Context &context, const Json &params, std::optional<Error> &error)
{
    auto t = target(context, params);
    if (!t) {
        error = t.error();
        return std::nullopt;
    }
    return context.host.documents->path(t->id);
}

Json refsJson(const application::VcsRefs &refs, const std::optional<std::string> &file)
{
    Json branches = Json::array(), tags = Json::array(), commits = Json::array();
    for (const application::VcsRef &b : refs.branches)
        branches.push(Json::object({{"name", Json(b.name)}, {"commit", Json(b.commit)}, {"current", Json(b.current)}}));
    for (const application::VcsRef &t : refs.tags)
        tags.push(Json::object({{"name", Json(t.name)}, {"commit", Json(t.commit)}}));
    for (const application::VcsCommit &c : refs.commits)
        commits.push(Json::object({{"commit", Json(c.commit)}, {"summary", Json(c.summary)}, {"author", Json(c.author)},
                                   {"time", Json(c.time)}}));
    Json head;
    if (!refs.headCommit.empty())
        head = Json::object({{"ref", refs.head ? Json(*refs.head) : Json()}, {"commit", Json(refs.headCommit)}});
    return Json::object({{"root", Json(refs.root)},
                         {"path", file ? Json(refs.prefix + *file) : Json()},
                         {"head", std::move(head)},
                         {"branches", std::move(branches)},
                         {"tags", std::move(tags)},
                         {"commits", std::move(commits)}});
}

void refs(const Context &context, const Json &params, Reply reply)
{
    if (auto no = refused(context))
        return reply(std::unexpected(*no));
    std::optional<Error> error;
    const std::optional<std::string> path = documentPath(context, params, error);
    if (error && params.find("document"))
        return reply(std::unexpected(*error));
    std::string at;
    std::optional<std::string> file;
    if (path) {
        at = utf8(pathOf(*path).parent_path());
        file = utf8(pathOf(*path).filename());
    } else if (auto project = context.host.project ? context.host.project->project() : std::nullopt) {
        at = project->root;
    } else {
        return reply(error ? std::unexpected(*error) : noPath());
    }
    const Json *limit = params.find("limit");
    context.host.vcs->refs(at, limit ? int(limit->asInt()) : kDefaultCommits,
                           [reply, file](std::expected<application::VcsRefs, application::VcsFailure> found) {
                               if (!found)
                                   return reply(failure(found.error()));
                               reply(refsJson(*found, file));
                           });
}

void show(const Context &context, const Json &params, Reply reply)
{
    if (auto no = refused(context))
        return reply(std::unexpected(*no));
    std::optional<Error> error;
    const std::optional<std::string> path = documentPath(context, params, error);
    if (error)
        return reply(std::unexpected(*error));
    if (!path)
        return reply(noPath());
    const std::string ref = params.find("ref")->asString();
    context.host.vcs->show(*path, ref, [reply, ref](std::expected<application::VcsFile, application::VcsFailure> found) {
        if (!found)
            return reply(failure(found.error()));
        reply(Json::object({{"ref", Json(ref)},
                            {"commit", Json(found->commit)},
                            {"path", Json(found->path)},
                            {"text", Json(found->text)}}));
    });
}

}

void addVcsOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "vcs.refs",
                 "The branches, tags and recent commits of the git repository holding the Document, or the Project's. "
                 "Plugins whose manifest asks for vcs.read only.",
                 kRefsParams, kRefsResult, refs);
    addOperation(registry, Access::Read, "vcs.show",
                 "The Document's file as a git branch, tag or commit has it. Plugins whose manifest asks for vcs.read only.",
                 kShowParams, kShowResult, show);
}

}
