#include "hikari/api/schema.h"
#include "hikari/plugins/manifest.h"

#include <gtest/gtest.h>

#include <tuple>

using namespace hikari::plugins;
using hikari::api::Json;
using Kind = ManifestProblem::Kind;

namespace {

constexpr std::string_view kMinimal = R"({"id": "com.example.hello", "name": "Hello", "version": "1.0.0", "api": 1,
    "main": "main.lua", "authors": ["Ana"]})";

Json minimal()
{
    return *Json::parse(kMinimal);
}

std::expected<Manifest, ManifestProblem> parse(const Json &json, std::int64_t api = 1)
{
    return parseManifest(json.dump(), api);
}

ManifestProblem problem(const Json &json, std::int64_t api = 1)
{
    const auto result = parse(json, api);
    EXPECT_FALSE(result) << json.dump();
    return result ? ManifestProblem{} : result.error();
}

TEST(PluginManifest, ThePublishedSchemaStaysInTheValidatorsSubset)
{
    auto root = *Json::parse(manifestSchema());
    Json defs = *root.find("$defs");
    std::erase_if(root.asObject(), [](const Json::Member &m) { return m.key.starts_with('$'); });
    EXPECT_EQ(hikari::api::checkSchema(root, defs), std::nullopt);
    for (const Json::Member &def : defs.asObject())
        EXPECT_EQ(hikari::api::checkSchema(def.value, defs), std::nullopt) << def.key;
}

TEST(PluginManifest, AMinimalManifestReadsWithDefaults)
{
    const auto m = parse(minimal());
    ASSERT_TRUE(m);
    EXPECT_EQ(m->id, "com.example.hello");
    EXPECT_EQ(m->version, "1.0.0");
    EXPECT_EQ(m->tier, Tier::Restricted);
    EXPECT_EQ(m->authors, std::vector<std::string>{"Ana"});
    EXPECT_FALSE(m->publicKey);
    EXPECT_FALSE(m->permissions.allDocuments);
}

TEST(PluginManifest, EveryFieldReads)
{
    Json json = minimal();
    json["tier"] = "unrestricted";
    json["description"] = "Says hello";
    json["license"] = "MIT";
    json["homepage"] = "https://example.com";
    json["publicKey"] = "RWQBI0VniavN7wOhB7/zzhC+HXDdGOdLwJln5NYwm6UNXx3chmQSVTG4";
    json["permissions"] = *Json::parse(R"({"documents.all": "Compares scripts",
        "network": {"hosts": ["api.example.com"], "reason": "Looks words up"},
        "files": [{"id": "fonts", "title": "Fonts", "writable": true, "reason": "Reads fonts"}],
        "vcs.read": "Marks Lines changed since a tag", "background": "Checks the feed",
        "presence": "Shows the episode the tracker knows"})");
    json["contributes"] = *Json::parse(R"({"commands": [{"id": "greet", "title": "Greet"}],
        "panels": [{"id": "board", "title": "Board"}, {"id": "notes", "title": "Notes", "area": "bottom"}],
        "settings": [{"id": "loud", "title": "Loud", "type": "boolean", "default": false},
                     {"id": "mode", "title": "Mode", "type": "choice", "options": ["a", "b"], "default": "b"},
                     {"id": "count", "title": "Count", "type": "integer", "default": 3, "minimum": 1}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << " " << m.error().detail;
    EXPECT_EQ(m->tier, Tier::Unrestricted);
    ASSERT_TRUE(m->publicKey);
    EXPECT_EQ(m->publicKey->idText(), "EFCDAB8967452301");
    EXPECT_EQ(m->permissions.allDocuments, "Compares scripts");
    EXPECT_EQ(m->permissions.networkHosts, std::vector<std::string>{"api.example.com"});
    ASSERT_EQ(m->permissions.files.size(), 1u);
    EXPECT_TRUE(m->permissions.files[0].writable);
    EXPECT_EQ(m->permissions.vcsRead, "Marks Lines changed since a tag");
    EXPECT_EQ(m->permissions.background, "Checks the feed");
    EXPECT_EQ(m->permissions.presence, "Shows the episode the tracker knows");
    ASSERT_EQ(m->commands.size(), 1u);
    EXPECT_EQ(m->commands[0].title, "Greet");
    ASSERT_EQ(m->panels.size(), 2u);
    EXPECT_EQ(m->panels[0].id, "board");
    EXPECT_EQ(m->panels[0].area, PanelArea::Right);
    EXPECT_EQ(m->panels[1].area, PanelArea::Bottom);
    EXPECT_EQ(m->settings.size(), 3u);
}

TEST(PluginManifest, ACommandsPaletteFieldsRead)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"commands": [
        {"id": "greet", "title": "Greet", "description": "Says hello to the selected Lines"},
        {"id": "status", "title": "Status", "palette": false}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << " " << m.error().detail;
    ASSERT_EQ(m->commands.size(), 2u);
    EXPECT_EQ(m->commands[0].description, "Says hello to the selected Lines");
    EXPECT_TRUE(m->commands[0].palette);
    EXPECT_EQ(m->commands[1].description, "");
    EXPECT_FALSE(m->commands[1].palette);
}

TEST(PluginManifest, APanelDocksInTheAreaItNames)
{
    for (const auto &[area, expected] : {std::pair{"left", PanelArea::Left}, std::pair{"right", PanelArea::Right},
                                         std::pair{"bottom", PanelArea::Bottom}}) {
        Json json = minimal();
        json["contributes"] = *Json::parse(
            std::string(R"({"panels": [{"id": "board", "title": "Board", "area": ")") + area + R"("}]})");
        const auto m = parse(json);
        ASSERT_TRUE(m) << area;
        EXPECT_EQ(m->panels[0].area, expected) << area;
    }
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"panels": [{"id": "board", "title": "Board", "area": "top"}]})");
    const ManifestProblem p = problem(json);
    EXPECT_EQ(p.kind, Kind::Schema);
    EXPECT_EQ(p.at, "/contributes/panels/0/area");
}

TEST(PluginManifest, FormatsReadWithTheirExtensionsAndDirections)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"formats": [
        {"id": "ebu-stl", "title": "EBU STL", "extensions": ["stl"], "import": true, "export": true},
        {"id": "csv", "title": "CSV cues", "extensions": ["csv", "tsv"], "export": true}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().detail;
    ASSERT_EQ(m->formats.size(), 2u);
    EXPECT_EQ(m->formats[0].id, "ebu-stl");
    EXPECT_EQ(m->formats[0].title, "EBU STL");
    EXPECT_TRUE(m->formats[0].imports);
    EXPECT_TRUE(m->formats[0].exports);
    EXPECT_EQ(m->formats[1].extensions, (std::vector<std::string>{"csv", "tsv"}));
    EXPECT_FALSE(m->formats[1].imports);
}

TEST(PluginManifest, AFormatNeedsADirectionAPlainExtensionAndAnIdOfItsOwn)
{
    const auto at = [](std::string_view formats) {
        Json json = minimal();
        json["contributes"] = *Json::parse(std::string(R"({"formats": )") + std::string(formats) + "}");
        return problem(json);
    };
    EXPECT_EQ(at(R"([{"id": "a", "title": "A", "extensions": ["a"]}])").at, "/contributes/formats/0");
    EXPECT_EQ(at(R"([{"id": "a", "title": "A", "extensions": ["a"], "import": false, "export": false}])").at,
              "/contributes/formats/0");
    EXPECT_EQ(at(R"([{"id": "a", "title": "A", "extensions": [".stl"], "export": true}])").at,
              "/contributes/formats/0/extensions/0");
    EXPECT_EQ(at(R"([{"id": "a", "title": "A", "extensions": [], "export": true}])").at, "/contributes/formats/0/extensions");
    const ManifestProblem repeated = at(R"([{"id": "a", "title": "A", "extensions": ["a"], "export": true},
                                            {"id": "a", "title": "B", "extensions": ["b"], "import": true}])");
    EXPECT_EQ(repeated.kind, Kind::DuplicateId);
    EXPECT_EQ(repeated.at, "/contributes/formats/1/id");
}

TEST(PluginManifest, OnlyAFullTrustManifestShipsAQmlPanel)
{
    Json json = minimal();
    json["tier"] = "unrestricted";
    json["contributes"] = *Json::parse(R"({"panels": [{"id": "board", "title": "Board"},
        {"id": "notes", "title": "Notes", "qml": "qml/Notes.qml"}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->panels[0].qml, "");
    EXPECT_EQ(m->panels[1].qml, "qml/Notes.qml");
    EXPECT_TRUE(m->hasQmlPanels());

    for (const char *tier : {"", "restricted"}) {
        Json restricted = json;
        if (*tier)
            restricted["tier"] = tier;
        else
            std::erase_if(restricted.asObject(), [](const Json::Member &m) { return m.key == "tier"; });
        const ManifestProblem p = problem(restricted);
        EXPECT_EQ(p.kind, Kind::QmlNeedsFullTrust) << tier;
        EXPECT_EQ(p.at, "/contributes/panels/1/qml") << tier;
    }

    for (const char *path : {"qml/Notes.js", "/qml/Notes.qml", "../Notes.qml", "qml/"}) {
        Json bad = json;
        bad["contributes"]["panels"].asArray()[1]["qml"] = path;
        const ManifestProblem p = problem(bad);
        EXPECT_EQ(p.kind, Kind::Schema) << path;
        EXPECT_EQ(p.at, "/contributes/panels/1/qml") << path;
    }
    EXPECT_FALSE(parse(minimal())->hasQmlPanels());
}

TEST(PluginManifest, SchemaFailuresNameTheField)
{
    Json missing = minimal();
    std::erase_if(missing.asObject(), [](const Json::Member &m) { return m.key == "name"; });
    EXPECT_EQ(problem(missing).detail, "missing \"name\"");

    for (const char *id : {"hello", "Com.Example", "com..example", "com.example-", "com.exa_mple"}) {
        Json json = minimal();
        json["id"] = id;
        const ManifestProblem p = problem(json);
        EXPECT_EQ(p.kind, Kind::Schema) << id;
        EXPECT_EQ(p.at, "/id") << id;
    }
    for (const char *version : {"1.0", "01.0.0", "1.0.0-", "v1.0.0"}) {
        Json json = minimal();
        json["version"] = version;
        EXPECT_EQ(problem(json).at, "/version") << version;
    }
    for (const char *main : {"../main.lua", "/main.lua", "main.py", "a\\main.lua"}) {
        Json json = minimal();
        json["main"] = main;
        EXPECT_EQ(problem(json).at, "/main") << main;
    }
    Json host = minimal();
    host["permissions"] = *Json::parse(R"({"network": {"hosts": ["https://example.com"], "reason": "r"}})");
    EXPECT_EQ(problem(host).at, "/permissions/network/hosts/0");
    Json extra = minimal();
    extra["colour"] = "red";
    EXPECT_EQ(problem(extra).at, "/colour");
    Json key = minimal();
    key["publicKey"] = "RW" + std::string(54, 'A');
    EXPECT_EQ(problem(key).at, "/publicKey");
}

TEST(PluginManifest, MarketplaceFieldsRead)
{
    Json json = minimal();
    json["summary"] = "Greets every line";
    json["icon"] = "media/icon.svg";
    json["screenshots"] = *Json::parse(R"([{"path": "media/one.png", "caption": "The panel"},
        {"path": "two.jpg", "caption": "Afterwards"}])");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << " " << m.error().detail;
    EXPECT_EQ(m->summary, "Greets every line");
    EXPECT_EQ(m->icon, "media/icon.svg");
    ASSERT_EQ(m->screenshots.size(), 2u);
    EXPECT_EQ(m->screenshots[1].path, "two.jpg");
    EXPECT_EQ(m->screenshots[1].caption, "Afterwards");
    EXPECT_TRUE(parse(minimal())->screenshots.empty());
}

TEST(PluginManifest, MarketplaceFieldsFollowTheSchema)
{
    const auto at = [](std::string_view key, Json value) {
        Json json = minimal();
        json[std::string(key)] = std::move(value);
        return problem(json).at;
    };
    EXPECT_EQ(at("summary", ""), "/summary");
    EXPECT_EQ(at("summary", std::string(101, 'a')), "/summary");
    EXPECT_EQ(at("summary", "two\nlines"), "/summary");
    EXPECT_EQ(at("summary", "tab\there"), "/summary");
    EXPECT_EQ(at("summary", "del\x7F"), "/summary");
    for (const char *icon : {"icon.jpg", "icon.PNG", "../icon.png", "/icon.svg", "media/.png"})
        EXPECT_EQ(at("icon", icon), "/icon") << icon;
    EXPECT_EQ(at("icon", std::string(197, 'a') + ".png"), "/icon");
    EXPECT_EQ(at("screenshots", Json::array()), "/screenshots");
    Json seven = Json::array();
    for (int i = 0; i < 7; ++i)
        seven.asArray().push_back(*Json::parse(R"({"path": "s.png", "caption": "c"})"));
    EXPECT_EQ(at("screenshots", seven), "/screenshots");
    EXPECT_EQ(at("screenshots", *Json::parse(R"([{"path": "s.png"}])")), "/screenshots/0");
    EXPECT_EQ(at("screenshots", *Json::parse(R"([{"path": "s.jpeg", "caption": "c"}])")), "/screenshots/0/path");
    EXPECT_EQ(at("screenshots", *Json::parse(R"([{"path": "s.svg", "caption": "c"}])")), "/screenshots/0/path");
    EXPECT_EQ(at("screenshots", *Json::parse(R"([{"path": "s.png", "caption": ""}])")), "/screenshots/0/caption");
    EXPECT_EQ(at("screenshots", *Json::parse(R"([{"path": "s.png", "caption": "a\u0001"}])")), "/screenshots/0/caption");
    Json longCaption = *Json::parse(R"([{"path": "s.png"}])");
    longCaption.asArray()[0]["caption"] = std::string(121, 'c');
    EXPECT_EQ(at("screenshots", longCaption), "/screenshots/0/caption");
    EXPECT_EQ(at("screenshots", *Json::parse(R"([{"path": "s.png", "caption": "c", "alt": "x"}])")), "/screenshots/0/alt");
}

TEST(PluginManifest, ANewerApiIsRefusedWithTheNumberItNeeds)
{
    Json json = minimal();
    json["api"] = 3;
    const ManifestProblem p = problem(json, 2);
    EXPECT_EQ(p.kind, Kind::NeedsNewerApi);
    EXPECT_EQ(p.neededApi, 3);
    EXPECT_TRUE(parse(json, 3));
}

TEST(PluginManifest, RepeatedIdsAndBadDefaultsAreRefused)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"commands": [{"id": "a", "title": "A"}, {"id": "a", "title": "B"}]})");
    const ManifestProblem repeated = problem(json);
    EXPECT_EQ(repeated.kind, Kind::DuplicateId);
    EXPECT_EQ(repeated.at, "/contributes/commands/1/id");

    json["contributes"] = *Json::parse(R"({"settings": [{"id": "m", "title": "M", "type": "choice", "options": ["a"], "default": "z"}]})");
    EXPECT_EQ(problem(json).kind, Kind::BadDefault);
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "n", "title": "N", "type": "number", "default": 0.5, "minimum": 1}]})");
    EXPECT_EQ(problem(json).at, "/contributes/settings/0/default");
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "n", "title": "N", "type": "integer", "default": 0.5}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
}

TEST(PluginManifest, SecretAndListSettingsRead)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"settings": [
        {"id": "token", "title": "Token", "type": "secret", "projectable": false},
        {"id": "hosts", "title": "Hosts", "type": "list", "default": ["a", "b"], "maxItems": 3}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << ": " << m.error().detail;
    ASSERT_EQ(m->settings.size(), 2u);
    EXPECT_EQ(m->settings[0].type, SettingType::Secret);
    EXPECT_TRUE(m->settings[0].secret());
    EXPECT_TRUE(m->settings[0].fallback().isNull());
    EXPECT_EQ(m->settings[1].type, SettingType::List);
    EXPECT_EQ(m->settings[1].fallback(), *Json::parse(R"(["a", "b"])"));

    EXPECT_EQ(settingValues(*m, Json::object()), *Json::parse(R"({"token": null, "hosts": ["a", "b"]})"));
    EXPECT_EQ(settingValues(*m, *Json::parse(R"({"token": "kept in plugins.json", "hosts": ["c"]})")),
              *Json::parse(R"({"token": null, "hosts": ["c"]})"));
    EXPECT_EQ(settingValues(*m, *Json::parse(R"({"hosts": ["c", "d", "e", "f"]})")),
              *Json::parse(R"({"token": null, "hosts": ["a", "b"]})"));

    EXPECT_EQ(settingRefusal(m->settings[0], Json(4)), "“token” takes text");
    EXPECT_EQ(settingRefusal(m->settings[0], Json("s3cret")), std::nullopt);
    EXPECT_EQ(settingRefusal(m->settings[1], Json("a")), "“hosts” takes a list of text");
    EXPECT_EQ(settingRefusal(m->settings[1], *Json::parse(R"(["a", 1])")), "“hosts” takes a list of text");
    EXPECT_EQ(settingRefusal(m->settings[1], *Json::parse(R"(["a", "b", "c", "d"])")), "“hosts” takes at most 3 items");
    EXPECT_EQ(settingRefusal(m->settings[1], *Json::parse(R"(["a", "b", "c"])")), std::nullopt);
}

TEST(PluginManifest, SecretAndListSettingsFollowTheirRules)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "t", "title": "T", "type": "secret", "default": "x"}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "t", "title": "T", "type": "secret", "projectable": true}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "l", "title": "L", "type": "list", "default": ["a", "b"], "maxItems": 1}]})");
    const ManifestProblem tooMany = problem(json);
    EXPECT_EQ(tooMany.kind, Kind::BadDefault);
    EXPECT_EQ(tooMany.at, "/contributes/settings/0/default");
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "l", "title": "L", "type": "list", "default": [1]}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
}

TEST(PluginManifest, StyleCataloguesAndKaraokePresetsRead)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({
        "styles": [{"id": "signs", "title": "Signs", "path": "styles/signs.sty"}],
        "karaokePresets": [{"id": "glow", "path": "presets/glow.ass"}, {"id": "pop", "path": "presets/pop.ass"}],
        "graphs": [{"id": "sparkle", "title": "Sparkle", "path": "graphs/sparkle.json"}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << ": " << m.error().detail;
    ASSERT_EQ(m->styles.size(), 1u);
    EXPECT_EQ(m->styles[0].id, "signs");
    EXPECT_EQ(m->styles[0].title, "Signs");
    EXPECT_EQ(m->styles[0].path, "styles/signs.sty");
    ASSERT_EQ(m->karaokePresets.size(), 2u);
    EXPECT_EQ(m->karaokePresets[1].path, "presets/pop.ass");
    EXPECT_TRUE(m->karaokePresets[1].title.empty());
    ASSERT_EQ(m->graphs.size(), 1u);
    EXPECT_EQ(m->graphs[0].title, "Sparkle");
    EXPECT_EQ(m->graphs[0].path, "graphs/sparkle.json");

    json["contributes"] = *Json::parse(R"({"karaokePresets": [{"id": "glow", "path": "a.ass"}, {"id": "glow", "path": "b.ass"}]})");
    const ManifestProblem repeated = problem(json);
    EXPECT_EQ(repeated.kind, Kind::DuplicateId);
    EXPECT_EQ(repeated.at, "/contributes/karaokePresets/1/id");
    json["contributes"] = *Json::parse(R"({"graphs": [{"id": "g", "title": "A", "path": "a.json"}, {"id": "g", "title": "B", "path": "b.json"}]})");
    EXPECT_EQ(problem(json).at, "/contributes/graphs/1/id");
    json["contributes"] = *Json::parse(R"({"styles": [{"id": "signs", "title": "Signs", "path": "signs.ass"}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
}

TEST(PluginManifest, DictionariesReadAndALanguageRepeatsNot)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"dictionaries": [
        {"language": "ca_ES-valencia", "title": "Valencian", "aff": "dict/ca.aff", "dic": "dict/ca.dic"}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << ": " << m.error().detail;
    ASSERT_EQ(m->dictionaries.size(), 1u);
    EXPECT_EQ(m->dictionaries[0].language, "ca_ES-valencia");
    EXPECT_EQ(m->dictionaries[0].title, "Valencian");
    EXPECT_EQ(m->dictionaries[0].aff, "dict/ca.aff");
    EXPECT_EQ(m->dictionaries[0].dic, "dict/ca.dic");

    json["contributes"] = *Json::parse(R"({"dictionaries": [
        {"language": "pl", "title": "A", "aff": "a.aff", "dic": "a.dic"},
        {"language": "pl", "title": "B", "aff": "b.aff", "dic": "b.dic"}]})");
    const ManifestProblem repeated = problem(json);
    EXPECT_EQ(repeated.kind, Kind::DuplicateId);
    EXPECT_EQ(repeated.at, "/contributes/dictionaries/1/language");
    EXPECT_EQ(repeated.detail, "pl");
    json["contributes"] = *Json::parse(R"({"dictionaries": [{"language": "pl", "title": "A", "aff": "a.dic", "dic": "a.aff"}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
}

TEST(PluginManifest, ANodeReadsItsPortsAndDefaults)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"nodes": [{"id": "zigzag", "title": "Zigzag", "summary": "Moves up and down",
        "file": "nodes/zigzag.lua", "inputs": [{"name": "index", "type": "Number"}, {"name": "height", "type": "Number", "default": 10}],
        "outputs": [{"name": "offset", "type": "Number"}]}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << " " << m.error().detail;
    ASSERT_EQ(m->nodes.size(), 1u);
    const NodeContribution &node = m->nodes[0];
    EXPECT_EQ(node.id, "zigzag");
    EXPECT_EQ(node.file, "nodes/zigzag.lua");
    ASSERT_EQ(node.inputs.size(), 2u);
    EXPECT_TRUE(node.inputs[0].fallback.isNull());
    EXPECT_EQ(node.inputs[1].fallback.asNumber(), 10);
    ASSERT_EQ(node.outputs.size(), 1u);
    EXPECT_EQ(node.outputs[0].type, "Number");
}

TEST(PluginManifest, ANodesRepeatedPortsAndBadDefaultsAreRefused)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"nodes": [{"id": "a", "title": "A", "file": "a.lua",
        "inputs": [{"name": "x", "type": "Number"}], "outputs": [{"name": "x", "type": "Number"}]}]})");
    const ManifestProblem repeated = problem(json);
    EXPECT_EQ(repeated.kind, Kind::DuplicateId);
    EXPECT_EQ(repeated.at, "/contributes/nodes/0/outputs/0/name");

    json["contributes"] = *Json::parse(R"({"nodes": [{"id": "a", "title": "A", "file": "a.lua",
        "inputs": [{"name": "tint", "type": "Colour", "default": "red"}], "outputs": [{"name": "y", "type": "Number"}]}]})");
    const ManifestProblem bad = problem(json);
    EXPECT_EQ(bad.kind, Kind::BadDefault);
    EXPECT_EQ(bad.at, "/contributes/nodes/0/inputs/0/default");

    json["contributes"] = *Json::parse(R"({"nodes": [{"id": "a", "title": "A", "file": "a.lua", "outputs": []}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
    json["contributes"] = *Json::parse(R"({"nodes": [{"id": "a", "title": "A", "file": "a.lua",
        "outputs": [{"name": "y", "type": "Vector"}]}]})");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
    json["contributes"] = *Json::parse(R"({"nodes": [{"id": "a", "title": "A", "file": "a.lua", "outputs": [{"name": "y", "type": "Number"}]},
        {"id": "a", "title": "B", "file": "b.lua", "outputs": [{"name": "y", "type": "Number"}]}]})");
    EXPECT_EQ(problem(json).at, "/contributes/nodes/1/id");
}

TEST(PluginManifest, AFolderRequestCantTakeThePlacesEveryPluginHas)
{
    Json json = minimal();
    json["permissions"] = *Json::parse(R"({"files": [{"id": "out", "title": "Out", "reason": "r"}, {"id": "cache", "title": "C", "reason": "r"}]})");
    const ManifestProblem reserved = problem(json);
    EXPECT_EQ(reserved.kind, Kind::Schema);
    EXPECT_EQ(reserved.at, "/permissions/files/1/id");
    EXPECT_EQ(reserved.detail, "\xE2\x80\x9C" "cache\xE2\x80\x9D is a place every plugin has");
    json["permissions"] = *Json::parse(R"({"files": [{"id": "project", "title": "P", "reason": "r"}]})");
    EXPECT_EQ(problem(json).at, "/permissions/files/0/id");
}

TEST(PluginManifest, TextThatIsNotJsonIsRefused)
{
    const auto result = parseManifest("{\"id\": ", 1);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, Kind::NotJson);
    EXPECT_FALSE(result.error().detail.empty());
}

TEST(PluginManifest, VersionsCompareBySemver)
{
    EXPECT_LT(compareVersions("1.0.0", "1.0.1"), 0);
    EXPECT_GT(compareVersions("1.10.0", "1.9.9"), 0);
    EXPECT_EQ(compareVersions("1.0.0+a", "1.0.0+b"), 0);
    EXPECT_LT(compareVersions("1.0.0-rc.1", "1.0.0"), 0);
    EXPECT_LT(compareVersions("1.0.0-alpha", "1.0.0-alpha.1"), 0);
    EXPECT_LT(compareVersions("1.0.0-2", "1.0.0-10"), 0);
    EXPECT_LT(compareVersions("1.0.0-9", "1.0.0-a"), 0);
    EXPECT_GT(compareVersions("1.0.0-beta", "1.0.0-alpha.9"), 0);
}

TEST(PluginManifest, ListedPermissionsOfTheWrongShapeAreLeftOut)
{
    const Permissions p = readPermissions(*Json::parse(
        R"({"documents.all": 1, "network": {"hosts": ["api.example.com", 2], "reason": "Looks words up"},
            "files": [{"id": "fonts", "title": "Fonts", "writable": "yes", "reason": 3}], "vcs.read": true, "background": false})"));
    EXPECT_FALSE(p.allDocuments);
    EXPECT_FALSE(p.vcsRead);
    EXPECT_FALSE(p.background);
    EXPECT_EQ(p.networkHosts, std::vector<std::string>{"api.example.com"});
    EXPECT_EQ(p.networkReason, "Looks words up");
    ASSERT_EQ(p.files.size(), 1u);
    EXPECT_FALSE(p.files.front().writable);
    EXPECT_EQ(p.files.front().reason, "");
    EXPECT_TRUE(readPermissions(*Json::parse(R"({"network": {"hosts": "x"}, "files": {}})")).networkHosts.empty());
}

TEST(PluginManifest, AnUpdateAsksForMoreOnlyWhenItAddsAPermission)
{
    Manifest current;
    current.permissions.networkHosts = {"api.example.com"};
    current.permissions.files = {{"fonts", "Fonts", false, "r"}, {"out", "Out", true, "r"}};
    EXPECT_FALSE(permissionIncrease(current, current).any());

    Manifest fewer = current;
    fewer.permissions.networkHosts.clear();
    fewer.permissions.files = {{"fonts", "Fonts", false, "r"}, {"out", "Out", false, "r"}};
    EXPECT_FALSE(permissionIncrease(current, fewer).any());

    Manifest more = current;
    more.tier = Tier::Unrestricted;
    more.permissions.allDocuments = "Compares scripts";
    more.permissions.vcsRead = "Marks changed Lines";
    more.permissions.background = "Checks the feed";
    more.permissions.presence = "Shows the episode";
    more.permissions.networkHosts = {"api.example.com", "cdn.example.com"};
    more.permissions.files = {{"fonts", "Fonts", true, "r"}, {"out", "Out", true, "r"}, {"logs", "Logs", false, "r"}};
    const PermissionIncrease increase = permissionIncrease(current, more);
    EXPECT_TRUE(increase.any());
    EXPECT_TRUE(increase.unrestricted);
    EXPECT_TRUE(increase.allDocuments);
    EXPECT_TRUE(increase.vcsRead);
    EXPECT_TRUE(increase.background);
    EXPECT_TRUE(increase.presence);
    EXPECT_EQ(increase.networkHosts, std::vector<std::string>{"cdn.example.com"});
    EXPECT_EQ(increase.files, (std::vector<std::string>{"fonts", "logs"}));
    EXPECT_FALSE(permissionIncrease(more, more).any());
}


TEST(PluginManifest, ProgramsReadWithTheirPlatforms)
{
    Json json = minimal();
    json["permissions"] = *Json::parse(R"({"programs": "Checks grammar"})");
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "lang", "title": "Language", "type": "string", "default": "en"}],
        "programs": [{"id": "ltex", "title": "LTeX", "network": true, "confinement": "lowIntegrity", "platforms": {
            "linux-x64": {"path": "bin/ltex", "args": ["--stdio", "${setting:lang}"], "env": {"LTEX_HOME": "${data}"}},
            "windows-x64": {"download": {"url": "https://example.com/ltex.zip", "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 10,
                "archive": "zip", "entry": "ltex/bin/ltex.exe"}}}}]})");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << " " << m.error().detail;
    EXPECT_EQ(m->permissions.programs, "Checks grammar");
    const Program *ltex = m->program("ltex");
    ASSERT_NE(ltex, nullptr);
    EXPECT_EQ(ltex->title, "LTeX");
    EXPECT_TRUE(ltex->network);
    EXPECT_EQ(ltex->confinement, Confinement::LowIntegrity);
    const ProgramRun &linux = ltex->platforms.at("linux-x64");
    EXPECT_EQ(linux.path, "bin/ltex");
    EXPECT_EQ(linux.args, (std::vector<std::string>{"--stdio", "${setting:lang}"}));
    EXPECT_EQ(linux.env, (std::vector<std::pair<std::string, std::string>>{{"LTEX_HOME", "${data}"}}));
    const ProgramRun &windows = ltex->platforms.at("windows-x64");
    ASSERT_TRUE(windows.download);
    EXPECT_EQ(windows.download->size, 10);
    EXPECT_EQ(windows.download->entry, "ltex/bin/ltex.exe");
    EXPECT_EQ(m->program("nope"), nullptr);
}

TEST(PluginManifest, ProgramsAreRefusedWithoutThePermissionOrInAShapeTheyCantRun)
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"programs": [{"id": "x", "title": "X", "platforms": {"linux-x64": {"path": "x"}}}]})");
    EXPECT_EQ(problem(json).at, "/permissions/programs");

    json["permissions"] = *Json::parse(R"({"programs": "Runs X"})");
    json["contributes"] = *Json::parse(R"({"programs": [{"id": "x", "title": "X", "platforms": {"linux-x64": {"path": "x"}}},
        {"id": "x", "title": "Y", "platforms": {"linux-x64": {"path": "y"}}}]})");
    EXPECT_EQ(problem(json).kind, Kind::DuplicateId);

    json["contributes"] = *Json::parse(R"({"programs": [{"id": "x", "title": "X", "confinement": "lowIntegrity",
        "platforms": {"windows-x64": {"path": "x.exe"}}}]})");
    EXPECT_EQ(problem(json).at, "/contributes/programs/0");

    json["contributes"] = *Json::parse(std::string(R"({"programs": [{"id": "x", "title": "X", "platforms": {"linux-x64":
        {"download": {"url": "https://example.com/x.zip", "sha256": ")") + std::string(64, 'a') + R"(", "size": 1, "archive": "zip"}}}}]})");
    EXPECT_EQ(problem(json).at, "/contributes/programs/0/platforms/linux-x64/download");

    json["contributes"] = *Json::parse(R"({"settings": [{"id": "key", "title": "Key", "type": "string", "default": ""}],
        "programs": [{"id": "x", "title": "X", "platforms": {"linux-x64": {"path": "x", "args": ["${setting:key}"]}}}]})");
    EXPECT_TRUE(parse(json));

    json["contributes"] = *Json::parse(R"({"settings": [{"id": "key", "title": "Key", "type": "secret"}],
        "programs": [{"id": "x", "title": "X", "platforms": {"linux-x64": {"path": "x", "args": ["-k", "${setting:key}"]}}}]})");
    EXPECT_EQ(problem(json).at, "/contributes/programs/0/platforms/linux-x64/args/1");
    json["contributes"] = *Json::parse(R"({"settings": [{"id": "key", "title": "Key", "type": "secret"}],
        "programs": [{"id": "x", "title": "X", "platforms": {"linux-x64": {"path": "x", "env": {"KEY": "${setting:key}"}}}}]})");
    EXPECT_TRUE(parse(json));
}

TEST(PluginManifest, AnUpdateAsksAgainForANewProgramOrOneThatGainsTheNetwork)
{
    Manifest current;
    current.permissions.programs = "Runs tools";
    current.programs = {Program{"a", "A", {}, false, false, Confinement::Container}};
    EXPECT_FALSE(permissionIncrease(current, current).any());

    Manifest next = current;
    next.programs[0].network = true;
    next.programs.push_back(Program{"b", "B", {}, false, false, Confinement::Container});
    const PermissionIncrease increase = permissionIncrease(current, next);
    EXPECT_FALSE(increase.runPrograms);
    EXPECT_EQ(increase.programs, (std::vector<std::string>{"a", "b"}));

    Manifest none;
    EXPECT_TRUE(permissionIncrease(none, current).runPrograms);
}

}

namespace {

Json withCommands()
{
    Json json = minimal();
    json["contributes"] = *Json::parse(R"({"commands": [
        {"id": "count", "title": "Count", "icon": "plugins", "when": ["selection", "!video"]},
        {"id": "clean", "title": "Clean"}]})");
    return json;
}

}

TEST(PluginManifest, MenusStatusItemsAndKeybindingsRead)
{
    Json json = withCommands();
    Json &contributes = json["contributes"];
    contributes["menus"] = *Json::parse(R"([{"command": "count", "location": "grid.context", "group": "tools"},
        {"command": "count", "location": "lineEditor.context"}, {"command": "clean", "location": "menuBar"}])");
    contributes["statusItems"] = *Json::parse(R"([{"id": "words", "title": "Words", "text": "0 words",
        "tooltip": "Words in the Line", "command": "count"}, {"id": "dot", "title": "Dot", "icon": "plugins"}])");
    contributes["keybindings"] = *Json::parse(R"([{"command": "count", "key": "Ctrl+Alt+K"}])");
    const auto m = parse(json);
    ASSERT_TRUE(m) << m.error().at << ": " << m.error().detail;
    EXPECT_EQ(m->commands[0].icon, "plugins");
    EXPECT_EQ(m->commands[0].when, (std::vector<std::string>{"selection", "!video"}));
    EXPECT_TRUE(m->commands[1].when.empty());
    ASSERT_EQ(m->menus.size(), 3u);
    EXPECT_EQ(m->menus[0].location, MenuLocation::Grid);
    EXPECT_EQ(m->menus[0].group, "tools");
    EXPECT_EQ(m->menus[1].location, MenuLocation::LineEditor);
    EXPECT_EQ(m->menus[2].location, MenuLocation::MenuBar);
    EXPECT_EQ(m->menus[2].command, "clean");
    ASSERT_EQ(m->statusItems.size(), 2u);
    EXPECT_EQ(m->statusItems[0].text, "0 words");
    EXPECT_EQ(m->statusItems[0].command, "count");
    EXPECT_EQ(m->statusItems[1].icon, "plugins");
    EXPECT_TRUE(m->statusItems[1].command.empty());
    ASSERT_EQ(m->keybindings.size(), 1u);
    EXPECT_EQ(m->keybindings[0].key, "Ctrl+Alt+K");
}

TEST(PluginManifest, ACommandThatIsntDeclaredIsRefused)
{
    for (const auto &[list, entry, at] :
         {std::tuple{"menus", R"({"command": "nope", "location": "menuBar"})", "/contributes/menus/0/command"},
          std::tuple{"statusItems", R"({"id": "a", "title": "A", "text": "A", "command": "nope"})",
                     "/contributes/statusItems/0/command"},
          std::tuple{"keybindings", R"({"command": "nope", "key": "Ctrl+Alt+K"})", "/contributes/keybindings/0/command"}}) {
        Json json = withCommands();
        json["contributes"][list] = Json::array({*Json::parse(entry)});
        const ManifestProblem p = problem(json);
        EXPECT_EQ(p.kind, Kind::UnknownCommand) << list;
        EXPECT_EQ(p.at, at);
        EXPECT_EQ(p.detail, "nope");
    }
}

TEST(PluginManifest, RepeatsInMenusStatusItemsAndKeysAreRefused)
{
    Json json = withCommands();
    json["contributes"]["menus"] = *Json::parse(R"([{"command": "count", "location": "menuBar"},
        {"command": "count", "location": "grid.context"}, {"command": "count", "location": "menuBar", "group": "x"}])");
    ManifestProblem p = problem(json);
    EXPECT_EQ(p.kind, Kind::DuplicateId);
    EXPECT_EQ(p.at, "/contributes/menus/2/command");

    json = withCommands();
    json["contributes"]["statusItems"] = *Json::parse(R"([{"id": "a", "title": "A", "text": "A"},
        {"id": "a", "title": "B", "text": "B"}])");
    p = problem(json);
    EXPECT_EQ(p.kind, Kind::DuplicateId);
    EXPECT_EQ(p.at, "/contributes/statusItems/1/id");

    json = withCommands();
    json["contributes"]["keybindings"] = *Json::parse(R"([{"command": "count", "key": "Ctrl+Alt+K"},
        {"command": "clean", "key": "Ctrl+Alt+K"}])");
    p = problem(json);
    EXPECT_EQ(p.kind, Kind::DuplicateKey);
    EXPECT_EQ(p.at, "/contributes/keybindings/1/key");
}

TEST(PluginManifest, KeysAndConditionsFollowTheSchema)
{
    for (const char *key : {"K", "Shift+K", "Alt+Ctrl+K", "Ctrl+Alt+K+L", "Meta+K"}) {
        Json json = withCommands();
        json["contributes"]["keybindings"] = Json::array({Json::object({{"command", Json("count")}, {"key", Json(key)}})});
        EXPECT_EQ(problem(json).kind, Kind::Schema) << key;
    }
    for (const char *key : {"Ctrl+Alt+K", "Alt+Shift+F2", "F5", "Shift+F12", "Ctrl+Shift+Meta+Space"}) {
        Json json = withCommands();
        json["contributes"]["keybindings"] = Json::array({Json::object({{"command", Json("count")}, {"key", Json(key)}})});
        EXPECT_TRUE(parse(json)) << key;
    }
    Json json = withCommands();
    json["contributes"]["commands"] = *Json::parse(R"([{"id": "count", "title": "Count", "when": ["lines"]}])");
    EXPECT_EQ(problem(json).kind, Kind::Schema);
}
