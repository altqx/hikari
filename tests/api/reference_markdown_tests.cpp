#include "hikari/api/api.h"
#include "hikari/api/reference.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
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

}

TEST(ApiReferenceMarkdown, TheCheckedInReferenceIsCurrent)
{
    const std::string now = referenceMarkdown(apiReference());
    if (const char *out = std::getenv("HIKARI_API_WRITE_MARKDOWN_REFERENCE")) {
        std::ofstream(out, std::ios::binary) << now;
        GTEST_SKIP() << "wrote " << out;
    }
    EXPECT_TRUE(readText(HIKARI_API_MARKDOWN_REFERENCE) == now)
        << "docs/api-reference.md is stale: rerun this test with HIKARI_API_WRITE_MARKDOWN_REFERENCE set to that path";
}

TEST(ApiReferenceMarkdown, EveryOperationEventAndTypeHasItsSection)
{
    const Json reference = apiReference();
    const std::string markdown = referenceMarkdown(reference);
    EXPECT_TRUE(markdown.starts_with("# Hikari API reference\n\nVersion " + std::to_string(kApiVersion) + " of the Hikari API"));
    for (const Json &op : reference.find("operations")->asArray())
        EXPECT_NE(markdown.find("\n### `" + op.find("name")->asString() + "`\n"), std::string::npos) << op.dump();
    for (const Json &event : reference.find("events")->asArray())
        EXPECT_NE(markdown.find("\n### `" + event.find("name")->asString() + "`\n"), std::string::npos) << event.dump();
    for (const Json::Member &def : reference.find("$defs")->asObject())
        EXPECT_NE(markdown.find("\n### " + def.key + "\n"), std::string::npos) << def.key;
    EXPECT_FALSE(markdown.ends_with("\n\n"));
    EXPECT_TRUE(markdown.ends_with("\n"));
}

TEST(ApiReferenceMarkdown, FieldsShowTheirTypeWhetherRequiredAndTheirLimits)
{
    const Json reference = tiny(R"({"version": 3, "$defs": {"LineId": {"type": "integer", "minimum": 1}},
        "operations": [{"name": "lines.pick", "access": "write", "summary": "Picks Lines.", "since": 2, "deprecatedIn": 3,
          "params": {"type": "object", "properties": {
            "lines": {"type": "array", "items": {"$ref": "#/$defs/LineId"}, "minItems": 1, "maxItems": 50},
            "active": {"anyOf": [{"$ref": "#/$defs/LineId"}, {"type": "null"}], "description": "The active Line."},
            "mode": {"enum": ["add", "only"]},
            "name": {"type": "string", "minLength": 1, "maxLength": 20, "pattern": "^[a-z]+$"},
            "tags": {"type": "object", "additionalProperties": {"type": "string"}},
            "found": {"type": "array", "items": {"type": "object", "properties": {"row": {"type": "integer", "maximum": 9}},
              "required": ["row"]}}},
            "required": ["lines"], "additionalProperties": false},
          "result": {"type": "object", "properties": {"ran": {"const": true}}, "required": ["ran"]}}],
        "events": [{"name": "lines.picked", "summary": "Lines were picked.", "since": 2, "data": {"type": "object"}}]})");
    const std::string markdown = referenceMarkdown(reference);
    EXPECT_TRUE(markdown.starts_with("# Hikari API reference\n\nVersion 3 of the Hikari API"));
    EXPECT_NE(markdown.find("| `lines.pick` | write | 2 | Picks Lines. |\n"), std::string::npos);
    EXPECT_NE(markdown.find("### `lines.pick`\n\nWrite. Since version 2. Deprecated in version 3.\n\nPicks Lines.\n\n"
                            "**Parameters**\n\n| Field | Type | Required | Meaning |\n| --- | --- | --- | --- |\n"),
              std::string::npos)
        << markdown;
    EXPECT_NE(markdown.find("| `lines` | array of [LineId](#lineid) | yes | 1 to 50 items. |\n"), std::string::npos) << markdown;
    EXPECT_NE(markdown.find("| `active` | [LineId](#lineid) or null |  | The active Line. |\n"), std::string::npos);
    EXPECT_NE(markdown.find("| `mode` | one of `add` or `only` |  |  |\n"), std::string::npos);
    EXPECT_NE(markdown.find("| `name` | string |  | 1 to 20 characters. `pattern`: `\"^[a-z]+$\"`. |\n"), std::string::npos)
        << markdown;
    EXPECT_NE(markdown.find("| `tags` | object of string by name |  |  |\n"), std::string::npos);
    EXPECT_NE(markdown.find("| `found` | array of objects |  |  |\n| `found[].row` | integer | yes | At most 9. |\n"),
              std::string::npos);
    EXPECT_NE(markdown.find("**Result**\n\n| Field | Type | Always | Meaning |\n| --- | --- | --- | --- |\n"
                            "| `ran` | `true` | yes |  |\n"),
              std::string::npos);
    EXPECT_NE(markdown.find("### `lines.picked`\n\nSince version 2.\n\nLines were picked.\n\n**Data**: none.\n"),
              std::string::npos);
    EXPECT_NE(markdown.find("### LineId\n\nType: integer. At least 1.\n"), std::string::npos) << markdown;
}

TEST(ApiReferenceMarkdown, AOneOfOfObjectsListsEachByItsConstantField)
{
    const Json reference = tiny(R"({"version": 1, "operations": [], "events": [], "$defs": {"Edit": {"oneOf": [
        {"type": "object", "properties": {"op": {"const": "delete_line"}, "line": {"type": "integer"}},
          "required": ["op", "line"]},
        {"type": "object", "description": "A null value removes the key.", "properties": {"op": {"const": "set_info"},
          "key": {"type": "string"}}, "required": ["op", "key"]}]},
        "Value": {"anyOf": [{"type": "boolean"}, {"type": "array", "items": {"type": "string"}}]}}})");
    const std::string markdown = referenceMarkdown(reference);
    EXPECT_NE(markdown.find("### Edit\n\nOne of:\n\n- `op` `delete_line`\n- `op` `set_info`: A null value removes the key.\n\n"
                            "*`op` `delete_line`*\n\n| Field | Type | Required | Meaning |\n| --- | --- | --- | --- |\n"
                            "| `op` | `\"delete_line\"` | yes |  |\n| `line` | integer | yes |  |\n"),
              std::string::npos)
        << markdown;
    EXPECT_NE(markdown.find("### Value\n\nType: boolean or array of string.\n"), std::string::npos) << markdown;
}

TEST(ApiReferenceMarkdown, PipesInTextDontBreakTheTable)
{
    const Json reference = tiny(R"({"version": 1, "events": [], "$defs": {}, "operations": [{"name": "a.b", "access": "read",
        "summary": "Either | or.", "since": 1, "params": {"type": "object"}, "result": {"type": "object",
        "properties": {"x": {"type": "string", "description": "One\nline | two."}}}}]})");
    const std::string markdown = referenceMarkdown(reference);
    EXPECT_NE(markdown.find("| `a.b` | read | 1 | Either \\| or. |\n"), std::string::npos) << markdown;
    EXPECT_NE(markdown.find("| `x` | string |  | One line \\| two. |\n"), std::string::npos) << markdown;
    EXPECT_NE(markdown.find("**Parameters**: none.\n"), std::string::npos);
}
