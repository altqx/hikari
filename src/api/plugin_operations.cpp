#include "operations.h"

namespace hikari::api::detail {

namespace {

constexpr std::size_t kMaxPackageText = 1024 * 1024;

std::unexpected<Error> noPlugins()
{
    return fail(errors::kUnavailable, "Plugins are not available.");
}

Json commandsJson(const std::vector<PluginCommand> &commands)
{
    Json out = Json::array();
    for (const PluginCommand &c : commands)
        out.push(Json::object({{"command", Json(c.command)}, {"title", Json(c.title)}}));
    return out;
}

Result list(const Context &context, const Json &)
{
    if (!context.host.plugins)
        return noPlugins();
    Json plugins = Json::array();
    for (const PluginState &p : context.host.plugins->plugins())
        plugins.push(Json::object({{"id", Json(p.id)},
                                   {"name", Json(p.name)},
                                   {"version", Json(p.version)},
                                   {"description", Json(p.description)},
                                   {"source", Json(p.source)},
                                   {"signed", Json(p.signedPackage)},
                                   {"enabled", Json(p.enabled)},
                                   {"tier", Json(p.fullTrust ? "unrestricted" : "restricted")},
                                   {"revoked", Json(p.revoked)},
                                   {"commands", commandsJson(p.commands)}}));
    return Json::object({{"plugins", std::move(plugins)}});
}

Result manifest(const Context &context, const Json &params)
{
    if (!context.host.plugins)
        return noPlugins();
    const std::string &id = params.find("plugin")->asString();
    const auto text = context.host.plugins->manifest(id);
    if (!text)
        return fail(errors::kNotFound, "No plugin \"" + id + "\" is installed.", Json::object({{"plugin", Json(id)}}));
    auto parsed = Json::parse(*text);
    if (!parsed)
        return fail(errors::kRefused, "The plugin's hikari-plugin.json is not JSON.", Json::object({{"reason", Json("unreadable")}}));
    return Json::object({{"plugin", Json(id)}, {"manifest", std::move(*parsed)}});
}

Result run(const Context &context, const Json &params)
{
    if (!context.host.plugins)
        return noPlugins();
    const std::string &plugin = params.find("plugin")->asString();
    const std::string &command = params.find("command")->asString();
    switch (context.host.plugins->run(plugin, command)) {
    case PluginRun::Ran:
        return Json::object({{"ran", Json(true)}});
    case PluginRun::NotInstalled:
        return fail(errors::kNotFound, "No plugin \"" + plugin + "\" is installed.", Json::object({{"plugin", Json(plugin)}}));
    case PluginRun::UnknownCommand:
        return fail(errors::kNotFound, "The plugin has no command \"" + command + "\"; plugins.list gives its commands.",
                    Json::object({{"command", Json(command)}}));
    case PluginRun::NotRunning:
        break;
    }
    return fail(errors::kRefused, "The plugin isn't running; it may be off in Plugins.", Json::object({{"reason", Json("not_running")}}));
}

std::string luaString(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n')
            out += "\\n";
        else
            out += c;
    }
    return out + "\"";
}

std::string templateMain(const Json::Array &commands)
{
    std::string out;
    for (const Json &c : commands) {
        out += "hikari.commands.register(" + luaString(c.find("id")->asString()) + ", function()\n";
        out += "    local info = hikari.call(\"documents.get\", {})\n";
        out += "    hikari.notify(" + luaString(c.find("title")->asString() + ": ") + " .. info.title .. \" has \" .. "
               "info.lineCount .. \" lines.\")\n";
        out += "end)\n";
    }
    return out;
}

Json scaffoldManifest(const Context &context, const Json &params)
{
    Json manifest = Json::object({{"id", *params.find("plugin")},
                                  {"name", *params.find("name")},
                                  {"version", params.find("version") ? *params.find("version") : Json("0.1.0")},
                                  {"api", Json(kApiVersion)},
                                  {"main", Json("main.lua")},
                                  {"authors", Json::array({Json(context.caller.label.empty() ? "HikariSub AI" : context.caller.label)})}});
    if (const Json *description = params.find("description"))
        manifest["description"] = *description;
    if (const Json *tier = params.find("tier"); tier && tier->asString() == "unrestricted")
        manifest["tier"] = *tier;
    if (const Json *permissions = params.find("permissions"))
        manifest["permissions"] = *permissions;
    manifest["contributes"] = Json::object({{"commands", *params.find("commands")}});
    return manifest;
}

Result scaffold(const Context &context, const Json &params)
{
    if (!context.host.plugins)
        return noPlugins();
    const Json manifest = scaffoldManifest(context, params);
    std::vector<PluginFile> files{{"hikari-plugin.json", manifest.dump(2) + "\n"},
                                  {"main.lua", params.find("code") ? params.find("code")->asString()
                                                                   : templateMain(params.find("commands")->asArray())}};
    if (const Json *more = params.find("files"))
        for (const Json &f : more->asArray()) {
            const std::string &path = f.find("path")->asString();
            if (std::ranges::any_of(files, [&](const PluginFile &known) { return known.path == path; }))
                return fail(errors::kInvalidParams, "Two files are named " + path + ".", Json::object({{"path", Json(path)}}));
            files.push_back({path, f.find("text")->asString()});
        }
    std::size_t bytes = 0;
    for (const PluginFile &f : files)
        bytes += f.text.size();
    if (bytes > kMaxPackageText)
        return fail(errors::kInvalidParams, "The plugin is larger than 1 MiB.");
    const std::string &id = params.find("plugin")->asString();
    auto made = context.host.plugins->scaffold(id, files);
    if (!made)
        return fail(errors::kInvalidParams, made.error());
    Json paths = Json::array();
    for (const PluginFile &f : files)
        paths.push(Json(f.path));
    return Json::object({{"plugin", Json(id)}, {"folder", Json(made->folder)}, {"package", Json(made->package)},
                         {"files", std::move(paths)}, {"manifest", manifest}});
}

std::expected<std::string, Error> packageOf(const Context &context, const Json &params)
{
    const Json *plugin = params.find("plugin");
    const Json *file = params.find("file");
    if (bool(plugin) == bool(file))
        return fail(errors::kInvalidParams, "Give plugin (a scaffolded plugin) or file (a granted .hikariplugin), not both.");
    if (file) {
        for (const GrantedFile &granted : context.files)
            if (granted.handle == file->asString())
                return granted.path;
        return fail(errors::kNotGranted, "No file \"" + file->asString() + "\" was granted to this caller; see files.list.",
                    Json::object({{"file", *file}}));
    }
    if (auto package = context.host.plugins->scaffoldedPackage(plugin->asString()))
        return *package;
    return fail(errors::kNotFound, "Nothing was scaffolded as \"" + plugin->asString() + "\"; plugins.scaffold makes it.",
                Json::object({{"plugin", *plugin}}));
}

Json reviewJson(const PluginReview &review)
{
    Json files = Json::array();
    for (const PluginFile &f : review.files)
        files.push(Json::object({{"path", Json(f.path)}, {"text", Json(f.text)}}));
    Json permissions = Json::array();
    for (const std::string &p : review.permissions)
        permissions.push(Json(p));
    Json commands = Json::array();
    for (const std::string &c : review.commands)
        commands.push(Json(c));
    return Json::object({{"plugin", Json(review.id)},
                         {"name", Json(review.name)},
                         {"version", Json(review.version)},
                         {"change", Json(review.change)},
                         {"installedVersion", Json(review.installedVersion)},
                         {"tier", Json(review.fullTrust ? "unrestricted" : "restricted")},
                         {"needsRetrust", Json(review.needsRetrust)},
                         {"keyId", Json(review.keyId)},
                         {"pinnedKeyId", Json(review.pinnedKeyId)},
                         {"permissions", std::move(permissions)},
                         {"commands", std::move(commands)},
                         {"files", std::move(files)},
                         {"digest", Json(review.digest)}});
}

Result review(const Context &context, const Json &params)
{
    if (!context.host.plugins)
        return noPlugins();
    auto package = packageOf(context, params);
    if (!package)
        return std::unexpected(package.error());
    auto reviewed = context.host.plugins->review(*package);
    if (!reviewed)
        return fail(errors::kRefused, reviewed.error(), Json::object({{"reason", Json("not_installable")}}));
    return reviewJson(*reviewed);
}

Result install(const Context &context, const Json &params)
{
    if (context.caller.approvedPackage.empty())
        return fail(errors::kNotGranted, "Only a plugin the user approved in HikariSub installs.");
    if (!context.host.plugins)
        return noPlugins();
    auto package = packageOf(context, params);
    if (!package)
        return std::unexpected(package.error());
    auto installed = context.host.plugins->install(*package, context.caller.approvedPackage);
    if (!installed)
        return fail(errors::kRefused, installed.error(), Json::object({{"reason", Json("not_installed")}}));
    Json out = reviewJson(*installed);
    out["installed"] = Json(true);
    return out;
}

constexpr std::string_view kPackage = R"("plugin": {"type": "string", "description": "A plugin plugins.scaffold made."},
    "file": {"type": "string", "description": "A .hikariplugin the user granted, by its files.list handle."})";

constexpr std::string_view kReview = R"({"type": "object", "properties": {
    "plugin": {"type": "string"}, "name": {"type": "string"}, "version": {"type": "string"},
    "change": {"enum": ["install", "update", "reinstall", "downgrade"]}, "installedVersion": {"type": "string"},
    "tier": {"enum": ["restricted", "unrestricted"], "description": "unrestricted runs outside the sandbox and needs the user's full trust."},
    "needsRetrust": {"type": "boolean", "description": "The signing key changed, or an unsigned package replaces a signed one."},
    "keyId": {"type": "string", "description": "The signing key; empty when unsigned."}, "pinnedKeyId": {"type": "string"},
    "permissions": {"type": "array", "items": {"type": "string"}},
    "commands": {"type": "array", "items": {"type": "string"}, "description": "The titles of the commands it adds."},
    "files": {"type": "array", "items": {"type": "object", "properties": {"path": {"type": "string"}, "text": {"type": "string"}},
        "required": ["path", "text"], "additionalProperties": false}},
    "digest": {"type": "string"}@more},
    "required": ["plugin", "name", "version", "change", "installedVersion", "tier", "needsRetrust", "keyId", "pinnedKeyId",
        "permissions", "commands", "files", "digest"@required], "additionalProperties": false})";

std::string reviewSchema(std::string_view more, std::string_view required)
{
    std::string out(kReview);
    out.replace(out.find("@more"), 5, more);
    out.replace(out.find("@required"), 9, required);
    return out;
}

std::string withPackage(std::string_view text)
{
    std::string out(text);
    out.replace(out.find("@package"), 8, kPackage);
    return out;
}

void add(Registry &registry, Access access, std::string name, std::string summary, std::string_view params,
         std::string_view result, SyncHandler handler)
{
    addOperation(registry, access, std::move(name), std::move(summary), params, result, now(std::move(handler)));
}

}

void addPluginOperations(Registry &registry)
{
    add(registry, Access::Read, "plugins.list", "The installed plugins with their state, trust tier and commands.",
        R"({"type": "object", "additionalProperties": false})",
        R"({"type": "object", "properties": {"plugins": {"type": "array", "items": {"type": "object", "properties": {
            "id": {"type": "string"}, "name": {"type": "string"}, "version": {"type": "string"},
            "description": {"type": "string"}, "source": {"enum": ["file", "marketplace"]}, "signed": {"type": "boolean"},
            "enabled": {"type": "boolean"}, "tier": {"enum": ["restricted", "unrestricted"]}, "revoked": {"type": "boolean"},
            "commands": {"type": "array", "description": "While the plugin runs.", "items": {"type": "object", "properties": {
                "command": {"type": "string", "description": "plugin.<plugin id>.<command id>"}, "title": {"type": "string"}},
                "required": ["command", "title"], "additionalProperties": false}}},
            "required": ["id", "name", "version", "description", "source", "signed", "enabled", "tier", "revoked", "commands"],
            "additionalProperties": false}}}, "required": ["plugins"], "additionalProperties": false})",
        list);
    add(registry, Access::Read, "plugins.manifest", "An installed plugin's hikari-plugin.json.",
        R"({"type": "object", "properties": {"plugin": {"type": "string"}}, "required": ["plugin"], "additionalProperties": false})",
        R"({"type": "object", "properties": {"plugin": {"type": "string"}, "manifest": {"type": "object"}},
            "required": ["plugin", "manifest"], "additionalProperties": false})",
        manifest);
    add(registry, Access::Write, "plugins.scaffold",
        "Writes a plugin from the authoring guide's template into the AI plugins folder and packs it; nothing is installed.",
        R"({"type": "object", "properties": {
            "plugin": {"type": "string", "maxLength": 100, "pattern": "^[a-z0-9]+(-[a-z0-9]+)*(\\.[a-z0-9]+(-[a-z0-9]+)*)+$",
                "description": "Reverse-DNS, lowercase: com.example.sign-counter. An earlier scaffold of that id is replaced."},
            "name": {"type": "string", "minLength": 1, "maxLength": 80},
            "version": {"type": "string", "maxLength": 64, "description": "Semantic version; default 0.1.0."},
            "description": {"type": "string", "maxLength": 1000},
            "tier": {"enum": ["restricted", "unrestricted"], "description": "restricted (default) runs in the sandbox without ffi, io, os and debug; unrestricted needs the user's full trust at install."},
            "permissions": {"type": "object", "description": "The manifest's permissions: documents.all, network {hosts, reason}, files."},
            "commands": {"type": "array", "minItems": 1, "maxItems": 256, "items": {"type": "object", "properties": {
                "id": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$"}, "title": {"type": "string", "minLength": 1, "maxLength": 80}},
                "required": ["id", "title"], "additionalProperties": false}},
            "code": {"type": "string", "maxLength": 1048576, "description": "main.lua, registering each command with hikari.commands.register; the guide's first plugin when omitted."},
            "files": {"type": "array", "maxItems": 64, "items": {"type": "object", "properties": {
                "path": {"type": "string", "maxLength": 200, "pattern": "^([A-Za-z0-9_][A-Za-z0-9_.-]*/)*[A-Za-z0-9_][A-Za-z0-9_.-]*\\.(lua|json|md|txt)$"},
                "text": {"type": "string"}}, "required": ["path", "text"], "additionalProperties": false},
                "description": "More files beside main.lua and hikari-plugin.json."}},
            "required": ["plugin", "name", "commands"], "additionalProperties": false})",
        R"({"type": "object", "properties": {"plugin": {"type": "string"}, "folder": {"type": "string"},
            "package": {"type": "string", "description": "The packed .hikariplugin."},
            "files": {"type": "array", "items": {"type": "string"}}, "manifest": {"type": "object"}},
            "required": ["plugin", "folder", "package", "files", "manifest"], "additionalProperties": false})",
        scaffold);
    add(registry, Access::Read, "plugins.review",
        "What installing a package would do, as the install question shows it: its trust tier, signing, permissions, commands and files.",
        withPackage(R"({"type": "object", "properties": {@package}, "additionalProperties": false})"),
        reviewSchema("", ""), review);
    add(registry, Access::Write, "plugins.install",
        "Installs a scaffolded or granted plugin package and turns it on, once the user approves it in HikariSub; it asks every time.",
        withPackage(R"({"type": "object", "properties": {@package}, "additionalProperties": false})"),
        reviewSchema(R"(, "installed": {"const": true})", R"(, "installed")"), install);
    add(registry, Access::Write, "plugins.run", "Runs an installed plugin's command as its menu item would.",
        R"({"type": "object", "properties": {"plugin": {"type": "string"},
            "command": {"type": "string", "description": "The command's id within the plugin, as in its manifest."}},
            "required": ["plugin", "command"], "additionalProperties": false})",
        R"({"type": "object", "properties": {"ran": {"const": true}}, "required": ["ran"], "additionalProperties": false})",
        run);
}

}
