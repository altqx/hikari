#include "hikari/plugins/manifest.h"

#include "hikari/api/schema.h"

#include <algorithm>
#include <charconv>
#include <set>

namespace hikari::plugins {

namespace {

using api::Json;
using Kind = ManifestProblem::Kind;

struct Schema {
    Json root;
    Json defs;
};

const Schema &schema()
{
    static const Schema loaded = [] {
        Schema s{*Json::parse(manifestSchema()), Json::object()};
        auto &members = s.root.asObject();
        for (auto it = members.begin(); it != members.end();) {
            if (it->key == "$defs")
                s.defs = it->value;
            if (it->key.starts_with('$'))
                it = members.erase(it);
            else
                ++it;
        }
        return s;
    }();
    return loaded;
}

std::string text(const Json &object, std::string_view key)
{
    const Json *value = object.find(key);
    return value ? value->asString() : std::string();
}

std::vector<Contribution> contributions(const Json *list)
{
    std::vector<Contribution> out;
    if (list)
        for (const Json &item : list->asArray())
            out.push_back({text(item, "id"), text(item, "title")});
    return out;
}

template <typename Items>
std::optional<ManifestProblem> repeated(const Items &items, const std::string &at)
{
    std::set<std::string> seen;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!seen.insert(items[i].id).second)
            return ManifestProblem{Kind::DuplicateId, at + "/" + std::to_string(i) + "/id", items[i].id, 0};
    return std::nullopt;
}

std::optional<ManifestProblem> badDefault(const Setting &setting, std::size_t index)
{
    const Json &d = setting.definition;
    const Json &value = *d.find("default");
    const std::string at = "/contributes/settings/" + std::to_string(index) + "/default";
    if (const Json *options = d.find("options")) {
        const auto &list = options->asArray();
        if (std::find(list.begin(), list.end(), value) == list.end())
            return ManifestProblem{Kind::BadDefault, at, "not one of the options", 0};
    }
    if (const Json *m = d.find("minimum"); m && value.asNumber() < m->asNumber())
        return ManifestProblem{Kind::BadDefault, at, "below the minimum", 0};
    if (const Json *m = d.find("maximum"); m && value.asNumber() > m->asNumber())
        return ManifestProblem{Kind::BadDefault, at, "above the maximum", 0};
    return std::nullopt;
}

struct Version {
    std::uint64_t core[3]{};
    std::vector<std::string_view> pre;
};

Version parseVersion(std::string_view v)
{
    Version out;
    v = v.substr(0, v.find('+'));
    const std::size_t dash = v.find('-');
    std::string_view core = v.substr(0, dash);
    for (auto &part : out.core) {
        const std::size_t dot = core.find('.');
        const std::string_view digits = core.substr(0, dot);
        std::from_chars(digits.data(), digits.data() + digits.size(), part);
        core.remove_prefix(dot == std::string_view::npos ? core.size() : dot + 1);
    }
    if (dash != std::string_view::npos) {
        std::string_view pre = v.substr(dash + 1);
        while (true) {
            const std::size_t dot = pre.find('.');
            out.pre.push_back(pre.substr(0, dot));
            if (dot == std::string_view::npos)
                break;
            pre.remove_prefix(dot + 1);
        }
    }
    return out;
}

bool numeric(std::string_view s)
{
    return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

int compareIdentifiers(std::string_view a, std::string_view b)
{
    const bool an = numeric(a), bn = numeric(b);
    if (an && bn)
        return a.size() != b.size() ? (a.size() < b.size() ? -1 : 1) : a.compare(b) < 0 ? -1 : a == b ? 0 : 1;
    if (an != bn)
        return an ? -1 : 1;
    return a < b ? -1 : a == b ? 0 : 1;
}

}

std::expected<Manifest, ManifestProblem> parseManifest(std::string_view source, std::int64_t apiVersion)
{
    auto parsed = Json::parse(source);
    if (!parsed)
        return std::unexpected(ManifestProblem{Kind::NotJson, "", parsed.error().message, 0});
    const Json &json = *parsed;
    if (auto failure = api::validate(schema().root, json, schema().defs))
        return std::unexpected(ManifestProblem{Kind::Schema, failure->path, failure->message, 0});

    Manifest m;
    m.id = text(json, "id");
    m.name = text(json, "name");
    m.version = text(json, "version");
    m.api = json.find("api")->asInt();
    m.main = text(json, "main");
    m.tier = text(json, "tier") == "unrestricted" ? Tier::Unrestricted : Tier::Restricted;
    for (const Json &author : json.find("authors")->asArray())
        m.authors.push_back(author.asString());
    m.description = text(json, "description");
    m.license = text(json, "license");
    m.homepage = text(json, "homepage");
    m.summary = text(json, "summary");
    m.icon = text(json, "icon");
    if (const Json *screenshots = json.find("screenshots"))
        for (const Json &shot : screenshots->asArray())
            m.screenshots.push_back({text(shot, "path"), text(shot, "caption")});
    if (const Json *key = json.find("publicKey")) {
        m.publicKey = minisign::parsePublicKey(key->asString());
        if (!m.publicKey)
            return std::unexpected(ManifestProblem{Kind::Schema, "/publicKey", "not a minisign public key", 0});
    }
    if (const Json *permissions = json.find("permissions")) {
        if (const Json *all = permissions->find("documents.all"))
            m.permissions.allDocuments = all->asString();
        if (const Json *network = permissions->find("network")) {
            for (const Json &host : network->find("hosts")->asArray())
                m.permissions.networkHosts.push_back(host.asString());
            m.permissions.networkReason = text(*network, "reason");
        }
        if (const Json *files = permissions->find("files"))
            for (const Json &f : files->asArray()) {
                const Json *writable = f.find("writable");
                m.permissions.files.push_back(
                    {text(f, "id"), text(f, "title"), writable && writable->asBool(), text(f, "reason")});
            }
    }
    if (const Json *contributes = json.find("contributes")) {
        m.commands = contributions(contributes->find("commands"));
        m.panels = contributions(contributes->find("panels"));
        if (const Json *settings = contributes->find("settings"))
            for (const Json &s : settings->asArray())
                m.settings.push_back({text(s, "id"), text(s, "title"), s});
    }

    for (auto problem : {repeated(m.permissions.files, "/permissions/files"),
                         repeated(m.commands, "/contributes/commands"), repeated(m.panels, "/contributes/panels"),
                         repeated(m.settings, "/contributes/settings")})
        if (problem)
            return std::unexpected(*problem);
    for (std::size_t i = 0; i < m.settings.size(); ++i)
        if (auto problem = badDefault(m.settings[i], i))
            return std::unexpected(*problem);
    if (m.api > apiVersion)
        return std::unexpected(ManifestProblem{Kind::NeedsNewerApi, "/api", "", m.api});
    return m;
}

int compareVersions(std::string_view a, std::string_view b)
{
    const Version x = parseVersion(a), y = parseVersion(b);
    for (int i = 0; i < 3; ++i)
        if (x.core[i] != y.core[i])
            return x.core[i] < y.core[i] ? -1 : 1;
    if (x.pre.empty() || y.pre.empty())
        return x.pre.empty() == y.pre.empty() ? 0 : x.pre.empty() ? 1 : -1;
    for (std::size_t i = 0; i < std::min(x.pre.size(), y.pre.size()); ++i)
        if (const int c = compareIdentifiers(x.pre[i], y.pre[i]))
            return c;
    return x.pre.size() == y.pre.size() ? 0 : x.pre.size() < y.pre.size() ? -1 : 1;
}

std::optional<std::string> settingRefusal(const Setting &setting, const Json &value)
{
    const Json &d = setting.definition;
    const std::string type = text(d, "type");
    const std::string name = "\xE2\x80\x9C" + setting.id + "\xE2\x80\x9D";
    if (type == "boolean" && !value.isBool())
        return name + " takes true or false";
    if (type == "integer" && !value.isInt())
        return name + " takes a whole number";
    if (type == "number" && !value.isNumber())
        return name + " takes a number";
    if ((type == "string" || type == "choice") && !value.isString())
        return name + " takes text";
    if (const Json *options = d.find("options")) {
        const auto &list = options->asArray();
        if (std::find(list.begin(), list.end(), value) == list.end()) {
            std::string choices;
            for (const Json &option : list)
                choices += (choices.empty() ? "" : ", ") + option.asString();
            return name + " must be one of: " + choices;
        }
    }
    if (const Json *m = d.find("minimum"); m && value.asNumber() < m->asNumber())
        return name + " must be at least " + m->dump();
    if (const Json *m = d.find("maximum"); m && value.asNumber() > m->asNumber())
        return name + " must be at most " + m->dump();
    return std::nullopt;
}

Json settingValues(const Manifest &manifest, const Json &stored)
{
    Json out = Json::object();
    for (const Setting &setting : manifest.settings) {
        const Json *value = stored.find(setting.id);
        out[setting.id] = value && !settingRefusal(setting, *value) ? *value : *setting.definition.find("default");
    }
    return out;
}

}
