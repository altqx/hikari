#include "hikari/api/api.h"
#include "hikari/api/reference.h"
#include "hikari/api/schema.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

using namespace hikari::api;

namespace {

Json readJson(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    auto parsed = Json::parse(text.str());
    EXPECT_TRUE(parsed) << path << ": " << (parsed ? "" : parsed.error().message);
    return parsed ? *parsed : Json();
}

Json withoutText(const Json &schema)
{
    if (schema.isArray()) {
        Json out = Json::array();
        for (const Json &item : schema.asArray())
            out.push(withoutText(item));
        return out;
    }
    if (!schema.isObject())
        return schema;
    Json out = Json::object();
    for (const Json::Member &m : schema.asObject())
        if (m.key != "description" && m.key != "title")
            out[m.key] = withoutText(m.value);
    return out;
}

std::set<std::string> requiredOf(const Json &schema)
{
    std::set<std::string> out;
    if (const Json *required = schema.find("required"))
        for (const Json &name : required->asArray())
            out.insert(name.asString());
    return out;
}

enum class Side { Params, Result, Defs };

bool contains(const Json &list, const Json &item)
{
    for (const Json &entry : list.asArray())
        if (withoutText(entry) == withoutText(item))
            return true;
    return false;
}

std::vector<std::string> breaks(const Json &then, const Json &now, Side side, const std::string &at)
{
    if (!then.isObject() || !now.isObject()) {
        if (withoutText(then) == withoutText(now))
            return {};
        return {at + " changed"};
    }
    std::vector<std::string> out;
    const auto append = [&](std::vector<std::string> more) {
        for (std::string &b : more)
            out.push_back(std::move(b));
    };
    static const Json kNone = Json::object();
    const Json *thenProps = then.find("properties");
    const Json *nowProps = now.find("properties");
    if (thenProps)
        for (const Json::Member &m : thenProps->asObject()) {
            const Json *kept = (nowProps ? *nowProps : kNone).find(m.key);
            if (kept)
                append(breaks(m.value, *kept, side, at + "/" + m.key));
            else
                out.push_back(at + "/" + m.key + " removed");
        }
    const auto thenRequired = requiredOf(then), nowRequired = requiredOf(now);
    if (side != Side::Result)
        for (const std::string &name : nowRequired)
            if (!thenRequired.contains(name))
                out.push_back(at + "/" + name + " became required");
    if (side != Side::Params)
        for (const std::string &name : thenRequired)
            if (!nowRequired.contains(name))
                out.push_back(at + "/" + name + " no longer always given");
    const Json *thenItems = then.find("items");
    const Json *nowItems = now.find("items");
    if ((thenItems == nullptr) != (nowItems == nullptr))
        out.push_back(at + " items changed");
    else if (thenItems)
        append(breaks(*thenItems, *nowItems, side, at + "/items"));
    for (const char *key : {"type", "additionalProperties", "const", "$ref", "minimum", "maximum", "minLength",
                            "maxLength", "minItems", "maxItems"}) {
        const Json *a = then.find(key), *b = now.find(key);
        if ((a == nullptr) != (b == nullptr) || (a && !(withoutText(*a) == withoutText(*b))))
            out.push_back(at + " " + key + " changed");
    }
    for (const char *key : {"enum", "oneOf", "anyOf"}) {
        const Json *a = then.find(key), *b = now.find(key);
        if ((a == nullptr) != (b == nullptr) || (a && side == Side::Result && !(withoutText(*a) == withoutText(*b))))
            out.push_back(at + " " + key + " changed");
        else if (a)
            for (const Json &alternative : a->asArray())
                if (!contains(*b, alternative))
                    out.push_back(at + " " + key + " lost " + alternative.dump());
    }
    return out;
}

const Json *named(const Json &list, const std::string &name)
{
    for (const Json &entry : list.asArray())
        if (entry.find("name")->asString() == name)
            return &entry;
    return nullptr;
}

TEST(ApiReference, IsComplete)
{
    const Json reference = apiReference();
    EXPECT_EQ(*reference.find("version"), Json(kApiVersion));
    EXPECT_EQ(reference.find("operations")->asArray().size(), Api::registry().operations().size());
    EXPECT_EQ(reference.find("events")->asArray().size(), Api::registry().events().size());
    for (const Json &op : reference.find("operations")->asArray()) {
        const Json *params = op.find("params");
        EXPECT_FALSE(checkSchema(*params, *reference.find("$defs"))) << op.find("name")->asString();
    }
}

// HIKARI_API_WRITE_REFERENCE=<path> writes the current reference there, to freeze a new version.
TEST(ApiReference, ReleasedVersionOneOnlyGrows)
{
    if (const char *out = std::getenv("HIKARI_API_WRITE_REFERENCE")) {
        std::ofstream(out, std::ios::binary) << apiReference().dump(2) << '\n';
        GTEST_SKIP() << "wrote " << out;
    }
    const Json frozen = readJson(HIKARI_API_REFERENCE);
    ASSERT_TRUE(frozen.isObject());
    EXPECT_EQ(*frozen.find("version"), Json(1));
    const Json now = apiReference();
    std::vector<std::string> problems;
    for (const Json &op : frozen.find("operations")->asArray()) {
        const std::string name = op.find("name")->asString();
        const Json *current = named(*now.find("operations"), name);
        if (!current) {
            problems.push_back(name + " removed");
            continue;
        }
        if (!(*current->find("access") == *op.find("access")))
            problems.push_back(name + " changed its read/write mark");
        for (auto &b : breaks(*op.find("params"), *current->find("params"), Side::Params, name + " params"))
            problems.push_back(std::move(b));
        for (auto &b : breaks(*op.find("result"), *current->find("result"), Side::Result, name + " result"))
            problems.push_back(std::move(b));
    }
    for (const Json &event : frozen.find("events")->asArray()) {
        const std::string name = event.find("name")->asString();
        const Json *current = named(*now.find("events"), name);
        if (!current) {
            problems.push_back(name + " removed");
            continue;
        }
        for (auto &b : breaks(*event.find("data"), *current->find("data"), Side::Result, name + " data"))
            problems.push_back(std::move(b));
    }
    for (const Json::Member &def : frozen.find("$defs")->asObject()) {
        const Json *current = now.find("$defs")->find(def.key);
        if (!current) {
            problems.push_back("$defs/" + def.key + " removed");
            continue;
        }
        for (auto &b : breaks(def.value, *current, Side::Defs, "$defs/" + def.key))
            problems.push_back(std::move(b));
    }
    for (const std::string &problem : problems)
        ADD_FAILURE() << problem;
}

TEST(ApiReference, TheSidecarsCopyIsCurrent)
{
    Json now = apiReference();
    std::erase_if(now.asObject(), [](const Json::Member &m) { return m.key == "nodes"; });
    if (const char *out = std::getenv("HIKARI_API_WRITE_SIDECAR_REFERENCE")) {
        std::ofstream(out, std::ios::binary) << now.dump(2) << '\n';
        GTEST_SKIP() << "wrote " << out;
    }
    EXPECT_TRUE(readJson(HIKARI_AI_SIDECAR_API_REFERENCE) == now)
        << "src/ai-sidecar/src/tools/api.json is stale: rerun this test with HIKARI_API_WRITE_SIDECAR_REFERENCE set to "
           "that path, then run `bun tools/gen-api.ts` in src/ai-sidecar";
}

TEST(ApiReference, TheGuardCatchesABreak)
{
    const Json then = *Json::parse(R"({"type": "object", "properties": {"a": {"type": "integer"}, "b": {"type": "string"}},
        "required": ["a"]})");
    EXPECT_TRUE(breaks(then, then, Side::Params, "x").empty());
    const Json added = *Json::parse(R"({"type": "object", "properties": {"a": {"type": "integer", "description": "new"},
        "b": {"type": "string"}, "c": {"type": "string"}}, "required": ["a"]})");
    EXPECT_TRUE(breaks(then, added, Side::Params, "x").empty());
    EXPECT_TRUE(breaks(then, added, Side::Result, "x").empty());
    const Json removed = *Json::parse(R"({"type": "object", "properties": {"a": {"type": "integer"}}, "required": ["a"]})");
    EXPECT_EQ(breaks(then, removed, Side::Params, "x"), std::vector<std::string>{"x/b removed"});
    const Json retyped = *Json::parse(R"({"type": "object", "properties": {"a": {"type": "string"}, "b": {"type": "string"}},
        "required": ["a"]})");
    EXPECT_EQ(breaks(then, retyped, Side::Params, "x"), std::vector<std::string>{"x/a type changed"});
    const Json required = *Json::parse(R"({"type": "object", "properties": {"a": {"type": "integer"}, "b": {"type": "string"}},
        "required": ["a", "b"]})");
    EXPECT_EQ(breaks(then, required, Side::Params, "x"), std::vector<std::string>{"x/b became required"});
    EXPECT_TRUE(breaks(then, required, Side::Result, "x").empty());
    const Json optional = *Json::parse(R"({"type": "object", "properties": {"a": {"type": "integer"}, "b": {"type": "string"}}})");
    EXPECT_EQ(breaks(then, optional, Side::Result, "x"), std::vector<std::string>{"x/a no longer always given"});
    const Json bare = *Json::parse(R"({"type": "object", "additionalProperties": false})");
    const Json firstOptional = *Json::parse(R"({"type": "object", "properties": {"c": {"type": "string"}},
        "additionalProperties": false})");
    EXPECT_TRUE(breaks(bare, firstOptional, Side::Params, "x").empty());
    const Json firstRequired = *Json::parse(R"({"type": "object", "properties": {"c": {"type": "string"}},
        "required": ["c"], "additionalProperties": false})");
    EXPECT_EQ(breaks(bare, firstRequired, Side::Params, "x"), std::vector<std::string>{"x/c became required"});
    EXPECT_EQ(breaks(then, *Json::parse(R"({"type": "object", "required": ["a"]})"), Side::Params, "x"),
              (std::vector<std::string>{"x/a removed", "x/b removed"}));
    const Json two = *Json::parse(R"({"enum": ["p", "q"]})"), three = *Json::parse(R"({"enum": ["p", "q", "r"]})");
    EXPECT_TRUE(breaks(two, three, Side::Params, "x").empty());
    EXPECT_TRUE(breaks(two, three, Side::Defs, "x").empty());
    EXPECT_EQ(breaks(two, three, Side::Result, "x"), std::vector<std::string>{"x enum changed"});
    EXPECT_EQ(breaks(three, two, Side::Params, "x"), std::vector<std::string>{"x enum lost \"r\""});
}

}
