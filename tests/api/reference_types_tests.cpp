#include "hikari/api/api.h"
#include "hikari/api/reference.h"

#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

using namespace hikari::api;

namespace {

Json tiny(std::string_view text)
{
    auto parsed = Json::parse(text);
    EXPECT_TRUE(parsed.has_value()) << text;
    return parsed ? *parsed : Json();
}

std::string readText(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string identifierAt(const std::string &text, std::size_t at)
{
    std::size_t end = at;
    while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
        ++end;
    return text.substr(at, end - at);
}

std::vector<std::string> namesAfter(const std::string &text, const std::string &marker)
{
    std::vector<std::string> out;
    for (std::size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 1))
        out.push_back(identifierAt(text, at + marker.size()));
    return out;
}

void expectCurrent(const char *path, const std::string &now, const char *variable)
{
    if (const char *out = std::getenv(variable)) {
        std::ofstream(out, std::ios::binary) << now;
        GTEST_SKIP() << "wrote " << out;
    }
    EXPECT_TRUE(readText(path) == now) << path << " is stale: rerun this test with " << variable << " set to that path";
}

const Json kReference = tiny(R"({"$defs": {"LineId": {"type": "integer", "minimum": 1}},
    "operations": [{"name": "lines.find", "summary": "Finds Lines.",
        "params": {"type": "object", "properties": {"text": {"type": "string", "description": "What to look for."},
            "fields": {"type": "array", "items": {"enum": ["text", "style"]}}, "flags": {"type": "object",
            "additionalProperties": {"type": "boolean"}}}, "required": ["text"], "additionalProperties": false},
        "result": {"type": "object", "properties": {"lines": {"type": "array", "items": {"$ref": "#/$defs/LineId"}},
            "next": {"type": ["integer", "null"]}, "kind": {"anyOf": [{"const": "all"}, {"type": "number"}]}},
            "required": ["lines"], "additionalProperties": false}}],
    "events": [{"name": "document.saved", "summary": "A Document was saved.",
        "data": {"type": "object", "properties": {"document": {"type": "integer"}}, "required": ["document"]}}]})");

}

TEST(ApiTypeHints, TheCheckedInLuaAnnotationsAreCurrent)
{
    expectCurrent(HIKARI_API_LUA_TYPES, luaAnnotations(apiReference()), "HIKARI_API_WRITE_LUA_TYPES");
}

TEST(ApiTypeHints, TheCheckedInTypeScriptTypesAreCurrent)
{
    expectCurrent(HIKARI_API_TYPESCRIPT_TYPES, typeScriptTypes(apiReference()), "HIKARI_API_WRITE_TYPESCRIPT_TYPES");
}

TEST(ApiTypeHints, LuaGetsAClassPerTypeAndATypedCallPerOperation)
{
    const std::string lua = luaAnnotations(kReference);
    EXPECT_NE(lua.find("---@alias hikari.LineId integer\n"), std::string::npos);
    EXPECT_NE(lua.find("---@class hikari.LinesFindParams\n---@field text string What to look for.\n"
                       "---@field fields? (\"text\"|\"style\")[]\n---@field flags? table<string, boolean>\n"),
              std::string::npos);
    EXPECT_NE(lua.find("---@field lines hikari.LineId[]\n---@field next? integer|hikari.Null\n---@field kind? \"all\"|number\n"),
              std::string::npos);
    EXPECT_NE(lua.find("---@overload fun(operation: \"lines.find\", params: hikari.LinesFindParams): "
                       "hikari.LinesFindResult?, hikari.Error?\n"),
              std::string::npos);
    EXPECT_NE(lua.find("---@overload fun(name: \"document.saved\", fn: fun(data: hikari.DocumentSavedData))\n"),
              std::string::npos);
}

TEST(ApiTypeHints, TypeScriptGetsAnInterfacePerTypeAndTheOperationAndEventMaps)
{
    const std::string ts = typeScriptTypes(kReference);
    EXPECT_NE(ts.find("export type LineId = number;\n"), std::string::npos);
    EXPECT_NE(ts.find("export interface LinesFindParams {\n    /** What to look for. */\n    text: string;\n"
                      "    fields?: (\"text\" | \"style\")[];\n    flags?: { [key: string]: boolean };\n}\n"),
              std::string::npos);
    EXPECT_NE(ts.find("    lines: LineId[];\n    next?: number | null;\n    kind?: \"all\" | number;\n"), std::string::npos);
    EXPECT_NE(ts.find("    \"lines.find\": { params: LinesFindParams; result: LinesFindResult };\n"), std::string::npos);
    EXPECT_NE(ts.find("    \"document.saved\": DocumentSavedData;\n"), std::string::npos);
}

TEST(ApiTypeHints, EveryTypeTheLuaAnnotationsNameIsDeclared)
{
    const std::string lua = luaAnnotations(apiReference());
    std::set<std::string> declared;
    for (const char *marker : {"---@class hikari.", "---@alias hikari."})
        for (const std::string &name : namesAfter(lua, marker))
            declared.insert(name);
    for (const std::string &name : namesAfter(lua, "hikari."))
        if (!name.empty() && std::isupper(static_cast<unsigned char>(name.front())))
            EXPECT_TRUE(declared.contains(name)) << name;
}

TEST(ApiTypeHints, NoTypeNameRepeatsInTheTypeScriptTypes)
{
    const std::string ts = typeScriptTypes(apiReference());
    std::set<std::string> seen;
    for (const char *marker : {"\nexport interface ", "\nexport type "})
        for (const std::string &name : namesAfter(ts, marker))
            EXPECT_TRUE(seen.insert(name).second) << name;
}
