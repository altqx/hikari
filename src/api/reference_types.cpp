#include "hikari/api/reference.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>

namespace hikari::api {

namespace {

std::string pascal(std::string_view name)
{
    std::string out;
    bool upper = true;
    for (const char c : name) {
        if (c == '.' || c == '_' || c == '-') {
            upper = true;
            continue;
        }
        out += upper ? char(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return out;
}

std::string text(const Json &json, std::string_view key)
{
    const Json *value = json.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

std::string oneLine(std::string value)
{
    for (char &c : value)
        if (c == '\n' || c == '\r')
            c = ' ';
    return value;
}

std::string defName(const std::string &ref)
{
    constexpr std::string_view prefix = "#/$defs/";
    return ref.starts_with(prefix) ? ref.substr(prefix.size()) : ref;
}

bool identifier(std::string_view name)
{
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())))
        return false;
    for (const char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    return true;
}

std::set<std::string> requiredOf(const Json &schema)
{
    std::set<std::string> out;
    if (const Json *required = schema.find("required"); required && required->isArray())
        for (const Json &name : required->asArray())
            if (name.isString())
                out.insert(name.asString());
    return out;
}

bool takesNothingRequired(const Json &schema)
{
    return requiredOf(schema).empty();
}

std::string joined(const std::vector<std::string> &parts, std::string_view between)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i)
        out += (i ? std::string(between) : std::string()) + parts[i];
    return out;
}

std::vector<std::string> typeNames(const Json &schema)
{
    std::vector<std::string> out;
    if (const Json *type = schema.find("type")) {
        if (type->isString())
            out.push_back(type->asString());
        else if (type->isArray())
            for (const Json &t : type->asArray())
                if (t.isString())
                    out.push_back(t.asString());
    }
    return out;
}

bool unionAtTop(std::string_view type)
{
    int depth = 0;
    for (const char c : type) {
        if (c == '{' || c == '(' || c == '<' || c == '[')
            ++depth;
        else if (c == '}' || c == ')' || c == '>' || c == ']')
            --depth;
        else if (depth == 0 && (c == '|' || c == '&'))
            return true;
    }
    return false;
}

struct Lua {
    static std::string group(const std::string &type) { return unionAtTop(type) ? "(" + type + ")" : type; }

    static std::string type(const Json &schema)
    {
        if (!schema.isObject())
            return "any";
        if (const Json *ref = schema.find("$ref"); ref && ref->isString())
            return "hikari." + defName(ref->asString());
        if (const Json *constant = schema.find("const"))
            return constant->isNull() ? "hikari.Null" : constant->dump();
        if (const Json *values = schema.find("enum"); values && values->isArray()) {
            std::vector<std::string> parts;
            for (const Json &v : values->asArray())
                parts.push_back(v.isNull() ? "hikari.Null" : v.dump());
            return joined(parts, "|");
        }
        for (const char *key : {"anyOf", "oneOf"})
            if (const Json *options = schema.find(key); options && options->isArray()) {
                std::vector<std::string> parts;
                for (const Json &option : options->asArray())
                    parts.push_back(type(option));
                return joined(parts, "|");
            }
        const std::vector<std::string> types = typeNames(schema);
        if (types.empty())
            return "any";
        std::vector<std::string> parts;
        for (const std::string &t : types)
            parts.push_back(single(schema, t));
        return joined(parts, "|");
    }

    static std::string single(const Json &schema, const std::string &t)
    {
        if (t == "string" || t == "integer" || t == "number" || t == "boolean")
            return t;
        if (t == "null")
            return "hikari.Null";
        if (t == "array") {
            const Json *items = schema.find("items");
            return group(items ? type(*items) : "any") + "[]";
        }
        if (t == "object") {
            const Json *properties = schema.find("properties");
            if (properties && properties->isObject() && !properties->asObject().empty()) {
                const std::set<std::string> required = requiredOf(schema);
                std::vector<std::string> fields;
                for (const auto &member : properties->asObject()) {
                    const std::string key = identifier(member.key) ? member.key : "[\"" + member.key + "\"]";
                    fields.push_back(key + (required.contains(member.key) ? ": " : "?: ") + type(member.value));
                }
                return "{ " + joined(fields, ", ") + " }";
            }
            const Json *more = schema.find("additionalProperties");
            return "table<string, " + (more && more->isObject() ? type(*more) : std::string("any")) + ">";
        }
        return "any";
    }

    static std::string named(const std::string &name, const Json &schema, const std::string &summary)
    {
        std::string out;
        if (!summary.empty())
            out += "---" + oneLine(summary) + "\n";
        const Json *properties = schema.isObject() ? schema.find("properties") : nullptr;
        const bool object = std::ranges::contains(typeNames(schema), std::string("object")) && typeNames(schema).size() == 1;
        if (!object || !properties || !properties->isObject() || properties->asObject().empty())
            return out + "---@alias hikari." + name + " " + type(schema) + "\n\n";
        out += "---@class hikari." + name + "\n";
        const std::set<std::string> required = requiredOf(schema);
        for (const auto &member : properties->asObject()) {
            const std::string key = identifier(member.key) ? member.key : "[\"" + member.key + "\"]";
            const std::string about = text(member.value, "description");
            out += "---@field " + key + (required.contains(member.key) ? " " : "? ") + type(member.value) +
                   (about.empty() ? "" : " " + oneLine(about)) + "\n";
        }
        return out + "\n";
    }
};

struct TypeScript {
    static std::string group(const std::string &type) { return unionAtTop(type) ? "(" + type + ")" : type; }

    static std::string pad(int depth) { return std::string(std::size_t(depth) * 4, ' '); }

    static std::string doc(const std::string &about, int depth)
    {
        return about.empty() ? std::string() : pad(depth) + "/** " + oneLine(about) + " */\n";
    }

    static std::string key(const std::string &name) { return identifier(name) ? name : Json(name).dump(); }

    static std::string fields(const Json &schema, int depth)
    {
        std::string out;
        const std::set<std::string> required = requiredOf(schema);
        for (const auto &member : schema.find("properties")->asObject())
            out += doc(text(member.value, "description"), depth) + pad(depth) + key(member.key) +
                   (required.contains(member.key) ? ": " : "?: ") + type(member.value, depth) + ";\n";
        return out;
    }

    static std::string type(const Json &schema, int depth = 0)
    {
        if (!schema.isObject())
            return "Json";
        if (const Json *ref = schema.find("$ref"); ref && ref->isString())
            return defName(ref->asString());
        if (const Json *constant = schema.find("const"))
            return constant->dump();
        if (const Json *values = schema.find("enum"); values && values->isArray()) {
            std::vector<std::string> parts;
            for (const Json &v : values->asArray())
                parts.push_back(v.dump());
            return joined(parts, " | ");
        }
        for (const char *key : {"anyOf", "oneOf"})
            if (const Json *options = schema.find(key); options && options->isArray()) {
                std::vector<std::string> parts;
                for (const Json &option : options->asArray())
                    parts.push_back(group(type(option, depth)));
                return joined(parts, " | ");
            }
        const std::vector<std::string> types = typeNames(schema);
        if (types.empty())
            return "Json";
        std::vector<std::string> parts;
        for (const std::string &t : types)
            parts.push_back(single(schema, t, depth));
        return joined(parts, " | ");
    }

    static std::string single(const Json &schema, const std::string &t, int depth)
    {
        if (t == "string" || t == "number" || t == "boolean" || t == "null")
            return t;
        if (t == "integer")
            return "number";
        if (t == "array") {
            const Json *items = schema.find("items");
            return group(items ? type(*items, depth) : "Json") + "[]";
        }
        if (t == "object") {
            const Json *properties = schema.find("properties");
            const Json *more = schema.find("additionalProperties");
            const bool closed = more && more->isBool() && !more->asBool();
            const std::string rest = more && more->isObject() ? "{ [key: string]: " + type(*more, depth) + " }" : std::string();
            if (properties && properties->isObject() && !properties->asObject().empty()) {
                const std::string shape = "{\n" + fields(schema, depth + 1) + pad(depth) + "}";
                return rest.empty() ? shape : shape + " & " + rest;
            }
            if (!rest.empty())
                return rest;
            return closed ? "Record<string, never>" : "{ [key: string]: Json }";
        }
        return "Json";
    }

    static std::string named(const std::string &name, const Json &schema, const std::string &summary)
    {
        std::string out = doc(summary, 0);
        const Json *properties = schema.isObject() ? schema.find("properties") : nullptr;
        const Json *more = schema.isObject() ? schema.find("additionalProperties") : nullptr;
        const std::vector<std::string> types = typeNames(schema);
        if (types.size() == 1 && types[0] == "object" && properties && properties->isObject() &&
            !properties->asObject().empty() && !(more && more->isObject()))
            return out + "export interface " + name + " {\n" + fields(schema, 1) + "}\n\n";
        return out + "export type " + name + " = " + type(schema) + ";\n\n";
    }
};

constexpr std::string_view kLuaHead = R"(---@meta hikari
-- LuaLS annotations for the `hikari` global of HikariSub plugins, generated from api.json by
-- `hikarisub --export-docs`. Put this file in your workspace's library; don't edit it.

---JSON null, as `hikari.null` stands for it.
---@class hikari.Null

---An error from HikariSub: the Hikari API's `{code, message, data}`.
---@class hikari.Error
---@field code string
---@field message string
---@field data? any

---A file handle: the handle, the path asked for, and `"read"` or `"write"`.
---@class hikari.File
---@field file string
---@field name string
---@field access "read"|"write"

---A panel or dialog event.
---@class hikari.UiEvent
---@field kind string
---@field widget? string
---@field value? any
---@field panel? string
---@field dialog? string
---@field button? string
---@field values? table<string, any>
---@field files? hikari.File[]

---Stages the edits of one `hikari.document.edit`.
---@class hikari.Change
local Change = {}
---@param line integer
---@param fields table
function Change:set_line(line, fields) end
---@param fields table
---@param where { after?: integer, before?: integer }
function Change:insert_line(fields, where) end
---@param line integer
function Change:delete_line(line) end
---@param line integer
---@param before integer?
function Change:move_line(line, before) end
---@param name string
---@param fields table
function Change:set_style(name, fields) end
---@param name string
function Change:delete_style(name) end
---@param key string
---@param value string?
function Change:set_info(key, value) end

---A snapshot of one Document, at one revision.
---@class hikari.Snapshot
---@field document integer
---@field revision integer
---@field title string
---@field format string
---@field lines hikari.Line[]
---@field styles hikari.Style[]
---@field info { key: string, value: string }[]

)";

constexpr std::string_view kLuaGlobal = R"(
hikari = {}

---The Hikari API number.
---@type integer
hikari.api_version = 1

---JSON `null`.
---@type hikari.Null
hikari.null = nil

---Marks a table as a JSON array, so `{}` goes as `[]`.
---@generic T: table
---@param t T
---@return T
function hikari.array(t) end

---A toast and a Log panel entry under the plugin's name.
---@param text string
---@param level? "info"|"warning"|"error"
function hikari.notify(text, level) end

hikari.plugin = {}
---@type string
hikari.plugin.id = ""
---@type string
hikari.plugin.version = ""

hikari.commands = {}
---Gives a command from `contributes.commands` its handler, once.
---@param id string
---@param fn fun(args: any): any
function hikari.commands.register(id, fn) end
---Runs a HikariSub command, as `commands.run` does.
---@param id string
---@return hikari.CommandsRunResult? result
---@return hikari.Error? error
function hikari.commands.run(id) end

hikari.settings = {}
---@param id string
---@return any
function hikari.settings.get(id) end
---Keeps a setting's value; one that doesn't fit the setting raises an error.
---@param id string
---@param value any
function hikari.settings.set(id, value) end

hikari.i18n = {}
---The UI language, such as `"pt_BR"` or `"en"`.
---@type string
hikari.i18n.locale = ""
---The text for `key` from the plugin's `i18n/` files; `%1` to `%9` take the arguments.
---@param key string
---@param ... any
---@return string
function hikari.i18n.t(key, ...) end

hikari.project = {}
---@return $(project.info#project)? project
---@return hikari.Error? error
function hikari.project.info() end

hikari.document = {}
---@return $(documents.list#documents)? documents
---@return hikari.Error? error
function hikari.document.list() end
---@param document? integer
---@return hikari.Snapshot? snapshot
---@return hikari.Error? error
function hikari.document.read(document) end
---Makes one change, applied as one undo step named "<plugin name>: <name>".
---@param name string
---@param fn fun(doc: hikari.Snapshot, change: hikari.Change)
---@param options? { document?: integer }
---@return { revision: integer, inserted: integer[], changed: boolean }? result
---@return hikari.Error? error
function hikari.document.edit(name, fn, options) end
---Every step `fn` commits becomes one undo step; returns `{steps}`, then what `fn` returned.
---@param name string
---@param fn fun(): ...
---@return { steps: integer }? group
---@return any ...
function hikari.document.group(name, fn) end

hikari.selection = {}
---@param document? integer
---@return hikari.SelectionGetResult? selection
---@return hikari.Error? error
function hikari.selection.get(document) end
---@param selected integer[]
---@param active? integer
---@param document? integer
---@return hikari.SelectionSetResult? result
---@return hikari.Error? error
function hikari.selection.set(selected, active, document) end

hikari.panels = {}
---Draws a panel from `contributes.panels` from a tree of widgets.
---@param panel string
---@param tree table
function hikari.panels.render(panel, tree) end
---@param panel string
function hikari.panels.show(panel) end
---@param panel string
---@param fn fun(event: hikari.UiEvent)
function hikari.panels.on(panel, fn) end
---Sends `data` to a QML panel's `api.received`; false when the panel isn't loaded.
---@param panel string
---@param data any
---@return boolean
function hikari.panels.post(panel, data) end

hikari.dialogs = {}
---@param dialog string
---@param spec { title: string, body: table, buttons: table[], width?: integer, height?: integer }
function hikari.dialogs.show(dialog, spec) end
---Opens a dialog and waits: the button pressed and the values, or nil when it was closed without one.
---@param dialog string
---@param spec { title: string, body: table, buttons: table[], width?: integer, height?: integer }
---@return string? button
---@return table<string, any>? values
function hikari.dialogs.ask(dialog, spec) end
---@param dialog string
function hikari.dialogs.close(dialog) end
---@param dialog string
---@param fn fun(event: hikari.UiEvent)
function hikari.dialogs.on(dialog, fn) end

hikari.files = {}
---@param place string `"data"`, `"cache"`, `"project"` or a folder id
---@param path string
---@return hikari.File? file
---@return hikari.Error? error
function hikari.files.open(place, path) end
---@param place string
---@param path string
---@return { name: string, folder: boolean }[]? entries
---@return hikari.Error? error
function hikari.files.entries(place, path) end
---@param options? { title?: string, save?: boolean, name?: string, filters?: { name: string, patterns: string[] }[] }
---@return hikari.File? file
---@return hikari.Error? error
function hikari.files.pick(options) end
---Asks the user for the folder the first time; whether the plugin has it.
---@param id string
---@return boolean? granted
---@return hikari.Error? error
function hikari.files.folder(id) end
---@return $(files.list#files)? files
---@return hikari.Error? error
function hikari.files.list() end
---@param file string
---@param encoding? "text"|"base64"
---@return string? data
---@return hikari.Error? error
function hikari.files.read(file, encoding) end
---@param file string
---@param data string
---@param encoding? "text"|"base64"
---@return true? written
---@return hikari.Error? error
function hikari.files.write(file, data, encoding) end

hikari.net = {}
---An HTTPS request HikariSub makes to a host from the manifest.
---@param request string|{ url: string, method?: "GET"|"HEAD"|"POST"|"PUT"|"PATCH"|"DELETE", headers?: table<string, string>, body?: string, encoding?: "text"|"base64" }
---@return { status: integer, headers: table<string, string>, body: string }? response
---@return hikari.Error? error
function hikari.net.fetch(request) end

hikari.job = {}
---@param options { title: string }
---@return true? begun
---@return hikari.Error? error
function hikari.job.begin(options) end
---@param fraction number? nil for a bar without a fraction
---@param text? string
---@return true? shown
---@return hikari.Error? error
function hikari.job.progress(fraction, text) end
---@return boolean
function hikari.job.cancelled() end

hikari.timer = {}
---@param ms integer
---@param fn fun()
---@return integer? id
---@return hikari.Error? error
function hikari.timer.after(ms, fn) end
---@param ms integer
---@param fn fun()
---@return integer? id
---@return hikari.Error? error
function hikari.timer.every(ms, fn) end
---@param id integer
---@return true? cancelled
---@return hikari.Error? error
function hikari.timer.cancel(id) end

hikari.marks = {}
---@param marks { line: integer, colour: "accent"|"warning"|"danger"|"success"|"muted", tooltip?: string }[]
---@param document? integer
---@return hikari.MarksSetResult? result
---@return hikari.Error? error
function hikari.marks.set(marks, document) end
---@param lines? integer[]
---@param document? integer
---@return hikari.MarksClearResult? result
---@return hikari.Error? error
function hikari.marks.clear(lines, document) end
---@param document? integer
---@return $(marks.list#marks)? marks
---@return hikari.Error? error
function hikari.marks.list(document) end

hikari.vcs = {}
---@param options? hikari.VcsRefsParams
---@return hikari.VcsRefsResult? refs
---@return hikari.Error? error
function hikari.vcs.refs(options) end
---@param ref string
---@param document? integer
---@return string? text
---@return hikari.VcsShowResult|hikari.Error shown
function hikari.vcs.show(ref, document) end

hikari.programs = {}
---Runs a program the manifest declares and waits for it to end.
---@param program string
---@param options? { args?: string[], files?: (hikari.File|string)[], stdin?: string, encoding?: "text"|"base64", timeout?: integer }
---@return hikari.ProgramsRunResult? result
---@return hikari.Error? error
function hikari.programs.run(program, options) end
---Starts a program the manifest declares: its process.
---@param program string
---@param options? { args?: string[], files?: (hikari.File|string)[], encoding?: "text"|"base64" }
---@return string? process
---@return hikari.Error? error
function hikari.programs.start(program, options) end
---@param process string
---@param data? string
---@param close? boolean
---@return true? sent
---@return hikari.Error? error
function hikari.programs.send(process, data, close) end
---@param process string
---@return true? stopped
---@return hikari.Error? error
function hikari.programs.stop(process) end
)";

constexpr std::string_view kTypeScriptHead = R"(// TypeScript types for HikariSub program plugins, generated from api.json by `hikarisub --export-docs`.
// Don't edit this file.

/** JSON, as the Hikari API takes and gives it. */
export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

/** An error from the Hikari API. */
export interface ApiError {
    code: string;
    message: string;
    data?: Json;
}

)";

constexpr std::string_view kTypeScriptProtocol = R"(/** The program-plugin protocol's version. */
export type ProtocolVersion = 1;

/** A file handle: the handle, the path asked for, and its access. */
export interface FileHandle {
    file: string;
    name: string;
    access: "read" | "write";
}

/** A panel or dialog event. */
export interface UiEvent {
    kind: string;
    widget?: string;
    value?: Json;
    panel?: string;
    dialog?: string;
    button?: string;
    values?: { [id: string]: Json };
    files?: FileHandle[];
}

export interface InitializeParams {
    protocol: ProtocolVersion;
    api: number;
    plugin: { id: string; version: string };
    locale: string;
    settings: { [id: string]: Json };
}

export interface CommandParams {
    command: string;
    /** null for a command the user runs. */
    args: Json;
}

export type EventParams = { [N in keyof Events]: { name: N; data: Events[N] } }[keyof Events];

export interface PanelParams {
    panel: string;
    event: UiEvent;
}

export interface DialogParams {
    dialog: string;
    event: UiEvent;
}

/** What HikariSub asks the program, and what it answers. */
export interface HostRequests {
    initialize: { params: InitializeParams; result: Record<string, never> };
    command: { params: CommandParams; result: Json };
    event: { params: EventParams; result: null };
    panel: { params: PanelParams; result: null };
    dialog: { params: DialogParams; result: null };
    shutdown: { params: Record<string, never>; result: null };
}

/** What HikariSub tells the program without asking: after `shutdown`, `exit`. */
export interface HostNotifications {
    exit: undefined;
}

/** The members a Lua plugin's `hikari` global has, as methods. */
export interface PluginMethods {
    "hikari/notify": { params: { text: string; level?: "info" | "warning" | "error" }; result: null };
    "hikari/settings.get": { params: { id: string }; result: Json };
    "hikari/settings.set": { params: { id: string; value: Json }; result: null };
    "hikari/subscribe": { params: { events: (keyof Events)[] }; result: null };
    "hikari/unsubscribe": { params: { events: (keyof Events)[] }; result: null };
    /** Takes the app-wide edit turn; every step until `hikari/edit.end` is one undo step. */
    "hikari/edit.begin": { params: { name: string }; result: null };
    "hikari/edit.end": { params: Record<string, never>; result: { steps: number } };
}

/** The plugin services, beyond the Hikari API operations. */
export type ServiceMethod =
    | `panels.${string}`
    | `dialogs.${string}`
    | `quick_pick.${string}`
    | `input_box.${string}`
    | `files.${string}`
    | "net.fetch"
    | `job.${string}`;

/** Every method the program may call, with its params and result. */
export type ProgramCalls = { [M in keyof Operations]: Operations[M] } & PluginMethods;

export type RequestId = number | string;

export interface Request<M extends string = string, P = Json> {
    jsonrpc: "2.0";
    id: RequestId;
    method: M;
    params?: P;
}

export interface Notification<M extends string = string, P = Json> {
    jsonrpc: "2.0";
    method: M;
    params?: P;
}

export interface Success<R = Json> {
    jsonrpc: "2.0";
    id: RequestId;
    result: R;
}

/** -32000 carries a Hikari API error as `{code, data}`; -32601 is an unknown method; -32602 breaks the schema, with `data.pointer`. */
export type ErrorCode = -32700 | -32600 | -32601 | -32602 | -32603 | -32000;

export interface RpcError {
    code: ErrorCode;
    message: string;
    data?: Json;
}

export interface Failure {
    jsonrpc: "2.0";
    id: RequestId | null;
    error: RpcError;
}

export type Response<R = Json> = Success<R> | Failure;

/** A request HikariSub sends. */
export type HostRequest = { [M in keyof HostRequests]: Request<M, HostRequests[M]["params"]> }[keyof HostRequests];

/** A request the program sends. */
export type ProgramRequest =
    | { [M in keyof ProgramCalls]: Request<M, ProgramCalls[M]["params"]> }[keyof ProgramCalls]
    | Request<ServiceMethod, { [key: string]: Json }>;
)";

std::string withResultFields(std::string_view source, const Json &reference)
{
    std::string out;
    std::size_t at = 0;
    while (true) {
        const std::size_t open = source.find("$(", at);
        if (open == std::string_view::npos)
            break;
        const std::size_t close = source.find(')', open);
        const std::string_view inside = source.substr(open + 2, close - open - 2);
        const std::size_t hash = inside.find('#');
        std::string type = "any";
        for (const Json &op : reference.find("operations")->asArray())
            if (text(op, "name") == inside.substr(0, hash))
                if (const Json *properties = op.find("result")->find("properties"))
                    if (const Json *field = properties->find(inside.substr(hash + 1)))
                        type = Lua::group(Lua::type(*field));
        out += std::string(source.substr(at, open - at)) + type;
        at = close + 1;
    }
    return out + std::string(source.substr(at));
}

}

std::string luaAnnotations(const Json &reference)
{
    std::string out(kLuaHead);
    if (const Json *defs = reference.find("$defs"); defs && defs->isObject())
        for (const auto &member : defs->asObject())
            out += Lua::named(member.key, member.value, text(member.value, "description"));
    std::string calls;
    for (const Json &op : reference.find("operations")->asArray()) {
        const std::string name = text(op, "name");
        const std::string type = pascal(name);
        out += Lua::named(type + "Params", *op.find("params"), "The params of " + name + ".");
        out += Lua::named(type + "Result", *op.find("result"), "The result of " + name + ".");
        calls += "---@overload fun(operation: \"" + name + "\", params" + (takesNothingRequired(*op.find("params")) ? "?" : "") +
                 ": hikari." + type + "Params): hikari." + type + "Result?, hikari.Error?\n";
    }
    std::string events;
    for (const Json &event : reference.find("events")->asArray()) {
        const std::string name = text(event, "name");
        const std::string type = pascal(name) + "Data";
        out += Lua::named(type, *event.find("data"), text(event, "summary"));
        events += "---@overload fun(name: \"" + name + "\", fn: fun(data: hikari." + type + "))\n";
    }
    out += withResultFields(kLuaGlobal, reference);
    out += "\n---Calls a Hikari API operation as the plugin: the result, or nil and the error.\n" + calls +
           "---@param operation string\n---@param params? table\n---@return any result\n---@return hikari.Error? error\n"
           "function hikari.call(operation, params) end\n";
    out += "\nhikari.events = {}\n---Calls `fn(data)` for each Hikari API event of that name.\n" + events +
           "---@param name string\n---@param fn fun(data: any)\nfunction hikari.events.on(name, fn) end\n";
    return out;
}

std::string typeScriptTypes(const Json &reference)
{
    std::string out(kTypeScriptHead);
    if (const Json *defs = reference.find("$defs"); defs && defs->isObject())
        for (const auto &member : defs->asObject())
            out += TypeScript::named(member.key, member.value, text(member.value, "description"));
    std::string operations = "/** Every Hikari API operation: a method of its own name, with its params and result. */\n"
                             "export interface Operations {\n";
    for (const Json &op : reference.find("operations")->asArray()) {
        const std::string name = text(op, "name");
        const std::string type = pascal(name);
        out += TypeScript::named(type + "Params", *op.find("params"), "The params of `" + name + "`: " + text(op, "summary"));
        out += TypeScript::named(type + "Result", *op.find("result"), "The result of `" + name + "`.");
        operations += "    " + Json(name).dump() + ": { params: " + type + "Params; result: " + type + "Result };\n";
    }
    out += operations + "}\n\n";
    std::string events = "/** Every Hikari API event, with its data. */\nexport interface Events {\n";
    for (const Json &event : reference.find("events")->asArray()) {
        const std::string name = text(event, "name");
        const std::string type = pascal(name) + "Data";
        out += TypeScript::named(type, *event.find("data"), text(event, "summary"));
        events += "    " + Json(name).dump() + ": " + type + ";\n";
    }
    out += events + "}\n\n";
    out += kTypeScriptProtocol;
    return out;
}

}
