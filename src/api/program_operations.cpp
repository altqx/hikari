#include "operations.h"

namespace hikari::api::detail {

namespace {

constexpr std::string_view kProgramId = R"("program": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64})";

std::optional<Error> refused(const Context &context)
{
    if (!context.caller.id.starts_with("plugin:") || !context.caller.programs)
        return Error{std::string(errors::kNotGranted), "Only a plugin whose manifest asks for programs may run them.", {}};
    if (!context.host.programs)
        return Error{std::string(errors::kUnavailable), "Programs can't run here.", {}};
    return std::nullopt;
}

bool base64Of(const Json &params)
{
    const Json *encoding = params.find("encoding");
    return encoding && encoding->asString() == "base64";
}

std::expected<ProgramCall, Error> callOf(const Context &context, const Json &params)
{
    ProgramCall call;
    call.program = params.find("program")->asString();
    call.base64 = base64Of(params);
    if (const Json *args = params.find("args"))
        for (const Json &arg : args->asArray())
            call.args.push_back(arg.asString());
    if (const Json *files = params.find("files")) {
        const Json::Array &handles = files->asArray();
        for (std::size_t i = 0; i < handles.size(); ++i) {
            const std::string &handle = handles[i].asString();
            const auto file = std::ranges::find(context.files, handle, &GrantedFile::handle);
            const auto folder = std::ranges::find(context.folders, handle, &GrantedFolder::handle);
            if (file != context.files.end())
                call.files.push_back({file->path, file->writable});
            else if (folder != context.folders.end())
                call.files.push_back({folder->path, folder->writable});
            else
                return std::unexpected(Error{std::string(errors::kNotGranted), "The plugin doesn't hold " + handle + ".",
                                             Json::object({{"pointer", Json("/files/" + std::to_string(i))}})});
        }
    }
    if (const Json *timeout = params.find("timeout"))
        call.timeoutMs = int(timeout->asInt());
    return call;
}

std::expected<std::string, Error> bytesOf(const Json &params, std::string_view key)
{
    const Json *value = params.find(key);
    if (!value)
        return std::string();
    if (!base64Of(params))
        return value->asString();
    auto bytes = fromBase64(value->asString());
    if (!bytes)
        return std::unexpected(Error{std::string(errors::kInvalidParams), std::string(key) + " isn't base64.",
                                     Json::object({{"pointer", Json("/" + std::string(key))}})});
    return *bytes;
}

void programsRun(const Context &context, const Json &params, Reply reply)
{
    if (auto no = refused(context))
        return reply(std::unexpected(*no));
    auto call = callOf(context, params);
    if (!call)
        return reply(std::unexpected(call.error()));
    auto input = bytesOf(params, "stdin");
    if (!input)
        return reply(std::unexpected(input.error()));
    call->input = std::move(*input);
    context.host.programs->run(context.caller, std::move(*call), std::move(reply));
}

Result programsStart(const Context &context, const Json &params)
{
    if (auto no = refused(context))
        return std::unexpected(*no);
    auto call = callOf(context, params);
    if (!call)
        return std::unexpected(call.error());
    return context.host.programs->start(context.caller, std::move(*call));
}

Result programsSend(const Context &context, const Json &params)
{
    if (auto no = refused(context))
        return std::unexpected(*no);
    const Json *close = params.find("close");
    const Json *data = params.find("data");
    return context.host.programs->send(context.caller, params.find("process")->asString(),
                                       data ? data->asString() : std::string(), close && close->asBool());
}

Result programsStop(const Context &context, const Json &params)
{
    if (auto no = refused(context))
        return std::unexpected(*no);
    return context.host.programs->stop(context.caller, params.find("process")->asString());
}

std::string withProgram(std::string_view text)
{
    std::string out(text);
    out.replace(out.find("@program"), 8, kProgramId);
    return out;
}

}

void addProgramOperations(Registry &registry)
{
    registry.define("ProgramProcess",
                    schema(R"({"type": "string", "pattern": "^p[0-9]{1,9}$", "description": "A started program, such as p1."})"));
    addOperation(registry, Access::Write, "programs.run",
                 "Runs one of the plugin's declared programs with its declared arguments, then args, and answers once it "
                 "ends. Output is capped at 16 MiB per stream. Plugins with the programs permission only.",
                 withProgram(R"({"type": "object", "properties": {
                     @program,
                     "args": {"type": "array", "maxItems": 256, "items": {"type": "string", "maxLength": 32768},
                       "description": "${file:N} becomes the path of files[N], which the program may then open."},
                     "files": {"type": "array", "maxItems": 16, "items": {"type": "string", "description": "A file or folder handle the plugin holds."},
                       "description": "Read-only unless the handle is writable."},
                     "stdin": {"type": "string", "maxLength": 16777216},
                     "encoding": {"enum": ["utf8", "base64"], "description": "Of stdin, stdout and stderr; utf8 when omitted."},
                     "timeout": {"type": "integer", "minimum": 1000, "maximum": 86400000, "description": "Milliseconds; 60000 when omitted."}},
                     "required": ["program"], "additionalProperties": false})"),
                 R"({"type": "object", "properties": {
                     "exitCode": {"type": "integer"},
                     "stdout": {"type": "string"},
                     "stderr": {"type": "string"},
                     "encoding": {"enum": ["utf8", "base64"]}},
                     "required": ["exitCode", "stdout", "stderr", "encoding"], "additionalProperties": false})",
                 programsRun);
    addOperation(registry, Access::Write, "programs.start",
                 "Starts one of the plugin's declared programs and keeps it running; its output comes as program.output "
                 "and its end as program.exited. A plugin keeps at most 8 running. Plugins with the programs permission only.",
                 withProgram(R"({"type": "object", "properties": {
                     @program,
                     "args": {"type": "array", "maxItems": 256, "items": {"type": "string", "maxLength": 32768}},
                     "files": {"type": "array", "maxItems": 16, "items": {"type": "string"}},
                     "encoding": {"enum": ["utf8", "base64"], "description": "Of what program.output carries and programs.send takes."}},
                     "required": ["program"], "additionalProperties": false})"),
                 R"({"type": "object", "properties": {"process": {"$ref": "#/$defs/ProgramProcess"}},
                     "required": ["process"], "additionalProperties": false})",
                 now(programsStart));
    addOperation(registry, Access::Write, "programs.send",
                 "Writes to a started program's stdin; close: true closes stdin after data. Plugins with the programs "
                 "permission only.",
                 R"({"type": "object", "properties": {
                     "process": {"$ref": "#/$defs/ProgramProcess"},
                     "data": {"type": "string", "maxLength": 16777216},
                     "close": {"type": "boolean"}},
                     "required": ["process"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"process": {"$ref": "#/$defs/ProgramProcess"}},
                     "required": ["process"], "additionalProperties": false})",
                 now(programsSend));
    addOperation(registry, Access::Write, "programs.stop",
                 "Ends a started program and its children. Plugins with the programs permission only.",
                 R"({"type": "object", "properties": {"process": {"$ref": "#/$defs/ProgramProcess"}},
                     "required": ["process"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"process": {"$ref": "#/$defs/ProgramProcess"}},
                     "required": ["process"], "additionalProperties": false})",
                 now(programsStop));
}

}
