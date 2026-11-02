#include "hikari/api/schema.h"
#include "hikari/plugins/manifest.h"

#include <gtest/gtest.h>

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
        "files": [{"id": "fonts", "title": "Fonts", "writable": true, "reason": "Reads fonts"}]})");
    json["contributes"] = *Json::parse(R"({"commands": [{"id": "greet", "title": "Greet"}],
        "panels": [{"id": "board", "title": "Board"}],
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
    ASSERT_EQ(m->commands.size(), 1u);
    EXPECT_EQ(m->commands[0].title, "Greet");
    EXPECT_EQ(m->panels[0].id, "board");
    EXPECT_EQ(m->settings.size(), 3u);
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

}
