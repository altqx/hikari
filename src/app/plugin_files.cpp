#include "hikari/app/plugin_files.h"

#include "hikari/app/plugin_ui.h"
#include "hikari/api/folder.h"

#include <algorithm>

namespace hikari::app {

namespace {

namespace fs = std::filesystem;
using api::Json;
namespace errors = api::errors;

constexpr std::size_t kMaxTitle = 120;
constexpr std::size_t kMaxFilters = 16;

std::string utf8(const fs::path &p)
{
    const std::u8string text = p.u8string();
    return std::string(text.begin(), text.end());
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string inQuotes(std::string_view s)
{
    return "\xE2\x80\x9C" + std::string(s) + "\xE2\x80\x9D";
}

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

std::unexpected<api::Error> badParam(std::string_view pointer, std::string message)
{
    return api::fail(errors::kInvalidParams, std::move(message), Json::object({{"pointer", std::string(pointer)}}));
}

bool goodPattern(std::string_view pattern)
{
    if (pattern == "*")
        return true;
    if (!pattern.starts_with("*.") || pattern.size() < 3 || pattern.size() > 18)
        return false;
    return std::ranges::all_of(pattern.substr(2), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-';
    });
}

std::expected<QStringList, api::Error> nameFilters(const Json *filters)
{
    QStringList out;
    if (!filters)
        return out;
    if (!filters->isArray() || filters->asArray().size() > kMaxFilters)
        return badParam("/filters", "filters must be a list of at most 16 {name, patterns}");
    for (std::size_t i = 0; i < filters->asArray().size(); ++i) {
        const Json &filter = filters->asArray()[i];
        const std::string at = "/filters/" + std::to_string(i);
        const Json *name = filter.isObject() ? filter.find("name") : nullptr;
        const Json *patterns = filter.isObject() ? filter.find("patterns") : nullptr;
        if (!name || !name->isString() || name->asString().empty() || name->asString().size() > kMaxTitle)
            return badParam(at + "/name", "each filter needs a name of at most 120 bytes");
        if (!patterns || !patterns->isArray() || patterns->asArray().empty() || patterns->asArray().size() > kMaxFilters)
            return badParam(at + "/patterns", "each filter needs 1 to 16 patterns");
        QStringList shown;
        for (const Json &pattern : patterns->asArray()) {
            if (!pattern.isString() || !goodPattern(pattern.asString()))
                return badParam(at + "/patterns", "a pattern is * or *.extension");
            shown << qs(pattern.asString());
        }
        out << qs(name->asString()) + QStringLiteral(" (") + shown.join(u' ') + u')';
    }
    return out;
}

std::expected<PluginUi::FileAsk, api::Error> pickRequest(const Json &params)
{
    PluginUi::FileAsk ask;
    for (const Json::Member &member : params.asObject())
        if (member.key != "title" && member.key != "save" && member.key != "name" && member.key != "filters")
            return badParam("/" + member.key, inQuotes(member.key) + " isn't an option of hikari.files.pick");
    const Json *save = params.find("save");
    if (save && !save->isBool())
        return badParam("/save", "save must be true or false");
    ask.kind = save && save->asBool() ? QStringLiteral("save") : QStringLiteral("open");
    if (const Json *title = params.find("title")) {
        if (!title->isString() || title->asString().size() > kMaxTitle)
            return badParam("/title", "the title must be text of at most 120 bytes");
        ask.title = qs(title->asString());
    }
    if (const Json *name = params.find("name")) {
        const std::string &text = name->isString() ? name->asString() : std::string();
        if (!name->isString() || text.size() > 255 || text.find_first_of("/\\") != std::string::npos)
            return badParam("/name", "the name must be a file name without folders");
        ask.name = qs(text);
    }
    auto filters = nameFilters(params.find("filters"));
    if (!filters)
        return std::unexpected(filters.error());
    ask.nameFilters = std::move(*filters);
    return ask;
}

std::optional<fs::path> projectPlace(const std::optional<api::ProjectState> &project)
{
    if (!project || !project->trusted)
        return std::nullopt;
    return pathOf(project->root);
}

const std::string *stringParam(const Json &params, std::string_view key)
{
    const Json *value = params.find(key);
    return value && value->isString() ? &value->asString() : nullptr;
}

}

PluginFiles::PluginFiles(api::Api *api, plugins::Installer *installer, PluginUi &ui,
                         std::function<std::optional<api::ProjectState>()> project)
    : m_api(api), m_installer(installer), m_ui(ui), m_project(std::move(project))
{
}

void PluginFiles::serve(const Plugin &plugin, const std::string &operation, const Json &params, api::Reply reply)
{
    if (!m_api) {
        reply(api::fail(errors::kUnavailable, "files are not available here"));
        return;
    }
    if (!params.isObject()) {
        reply(badParam("", "the parameters must be a table"));
        return;
    }
    if (operation == "files.pick") {
        pick(plugin, params, std::move(reply));
        return;
    }
    if (operation == "files.folder") {
        const std::string *id = stringParam(params, "folder");
        if (!id) {
            reply(badParam("/folder", "needs a folder id"));
            return;
        }
        folder(plugin, *id, [reply](std::expected<Place, api::Error> place) {
            const Json *reason = place ? nullptr : place.error().data.find("reason");
            if (!place && !(reason && reason->isString() && reason->asString() == "declined"))
                reply(std::unexpected(place.error()));
            else
                reply(Json::object({{"granted", place.has_value()}}));
        });
        return;
    }
    if (operation == "files.open" || operation == "files.entries") {
        const std::string *name = stringParam(params, "place");
        const std::string *path = stringParam(params, "path");
        if (!name || !path) {
            reply(badParam(name ? "/path" : "/place", "needs a place and a path"));
            return;
        }
        const bool opening = operation == "files.open";
        place(plugin, *name, [this, plugin, path = *path, opening, reply](std::expected<Place, api::Error> place) {
            if (!place)
                reply(std::unexpected(place.error()));
            else if (opening)
                open(plugin, *place, path, reply);
            else
                entries(*place, path, reply);
        });
        return;
    }
    reply(api::fail(errors::kUnknownOperation, "there is no operation " + inQuotes(operation)));
}

void PluginFiles::pick(const Plugin &plugin, const Json &params, api::Reply reply)
{
    auto ask = pickRequest(params);
    if (!ask) {
        reply(std::unexpected(ask.error()));
        return;
    }
    const bool save = ask->kind == QStringLiteral("save");
    m_ui.askForFile(plugin.id, std::move(*ask), [this, plugin, save, reply](std::optional<QString> chosen, bool) {
        if (!chosen) {
            reply(Json::object());
            return;
        }
        const fs::path path(chosen->toStdU16String());
        reply(grant(plugin, path, utf8(path.filename()), save));
    });
}

void PluginFiles::folder(const Plugin &plugin, const std::string &id, std::function<void(std::expected<Place, api::Error>)> done)
{
    const auto request = std::ranges::find(plugin.folders, id, &plugins::FileGrantRequest::id);
    if (request == plugin.folders.end()) {
        done(api::fail(errors::kNotGranted, inQuotes(id) + " isn't a folder in hikari-plugin.json", Json::object({{"folder", id}})));
        return;
    }
    const auto granted = [&]() -> std::optional<plugins::FileGrant> {
        if (const auto run = m_thisRun.find(plugin.id); run != m_thisRun.end())
            if (const auto it = run->second.find(id); it != run->second.end())
                return it->second;
        if (m_installer)
            if (const auto registry = m_installer->registry(); registry && registry->contains(plugin.id))
                if (const auto it = registry->at(plugin.id).grants.find(id); it != registry->at(plugin.id).grants.end())
                    return it->second;
        return std::nullopt;
    }();
    if (granted && fs::is_directory(pathOf(granted->path))) {
        done(Place{pathOf(granted->path), granted->writable && request->writable});
        return;
    }
    PluginUi::FileAsk ask;
    ask.kind = QStringLiteral("folder");
    ask.title = qs(request->title);
    ask.reason = qs(request->reason);
    ask.writable = request->writable;
    m_ui.askForFile(plugin.id, std::move(ask), [this, pluginId = plugin.id, asked = *request, done](std::optional<QString> chosen, bool remember) {
        if (!chosen) {
            done(api::fail(errors::kNotGranted, "no folder was chosen for " + inQuotes(asked.title),
                           Json::object({{"folder", asked.id}, {"reason", "declined"}})));
            return;
        }
        const fs::path path(chosen->toStdU16String());
        const plugins::FileGrant grant{utf8(path), asked.writable};
        if (!remember || !m_installer || !m_installer->setGrant(pluginId, asked.id, grant))
            m_thisRun[pluginId][asked.id] = grant;
        done(Place{path, asked.writable});
    });
}

void PluginFiles::place(const Plugin &plugin, const std::string &name, std::function<void(std::expected<Place, api::Error>)> done)
{
    if (name == "project") {
        if (const auto root = projectPlace(m_project ? m_project() : std::nullopt))
            done(Place{*root, true});
        else
            done(api::fail(errors::kNotFound, "no trusted project is open", Json::object({{"place", "project"}})));
        return;
    }
    if (name != "data" && name != "cache") {
        folder(plugin, name, std::move(done));
        return;
    }
    if (!m_installer) {
        done(api::fail(errors::kUnavailable, "the plugin folders are not available here"));
        return;
    }
    const fs::path base = name == "data" ? m_installer->folders().data : m_installer->folders().cache;
    const fs::path root = base / pathOf(plugin.id);
    std::error_code error;
    fs::create_directories(root, error);
    if (error) {
        done(api::fail(errors::kRefused, "HikariSub can't make the plugin's " + name + " folder",
                       Json::object({{"reason", "not_written"}})));
        return;
    }
    done(Place{root, true});
}

void PluginFiles::open(const Plugin &plugin, const Place &place, const std::string &path, api::Reply reply)
{
    if (path.empty()) {
        reply(badParam("/path", "name a file inside the folder"));
        return;
    }
    const auto real = api::pathInside(place.root, path);
    if (!real) {
        reply(api::fail(errors::kNotGranted, real.error(), Json::object({{"path", path}})));
        return;
    }
    if (fs::is_directory(*real)) {
        reply(api::fail(errors::kRefused, inQuotes(path) + " is a folder", Json::object({{"reason", "folder"}})));
        return;
    }
    if (place.writable) {
        std::error_code error;
        fs::create_directories(real->parent_path(), error);
    }
    reply(grant(plugin, *real, path, place.writable));
}

void PluginFiles::entries(const Place &place, const std::string &path, api::Reply reply)
{
    const auto real = api::pathInside(place.root, path);
    if (!real) {
        reply(api::fail(errors::kNotGranted, real.error(), Json::object({{"path", path}})));
        return;
    }
    const auto listed = api::folderEntries(place.root, *real);
    if (!listed) {
        reply(api::fail(errors::kNotFound, inQuotes(path) + " isn't a folder", Json::object({{"path", path}})));
        return;
    }
    Json list = Json::array();
    for (const api::FolderEntry &entry : listed->entries)
        list.push(Json::object({{"name", entry.name}, {"folder", entry.folder}}));
    Json out = Json::object({{"entries", std::move(list)}});
    if (listed->truncated)
        out["truncated"] = true;
    reply(std::move(out));
}

Json PluginFiles::grant(const Plugin &plugin, const fs::path &path, std::string name, bool writable)
{
    const std::string handle = m_api->grantFile(plugin.caller, utf8(path), writable);
    return Json::object({{"file", handle}, {"name", std::move(name)}, {"access", writable ? "write" : "read"}});
}

Json PluginFiles::grantDropped(const Plugin &plugin, const QString &path)
{
    const fs::path file(path.toStdU16String());
    if (!m_api || !fs::is_regular_file(file))
        return Json();
    return grant(plugin, file, utf8(file.filename()), false);
}

void PluginFiles::end(const std::string &pluginId, const std::string &caller)
{
    if (m_api)
        m_api->revokeFiles(caller);
    m_thisRun.erase(pluginId);
}

}
