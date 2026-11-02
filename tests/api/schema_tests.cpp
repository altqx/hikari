#include "hikari/api/schema.h"

#include <gtest/gtest.h>

using hikari::api::checkSchema;
using hikari::api::Json;
using hikari::api::validate;

namespace {

Json j(std::string_view text)
{
    auto value = Json::parse(text);
    EXPECT_TRUE(value) << text;
    return value ? *value : Json();
}

const Json kDefs = j(R"({"Id": {"type": "integer", "minimum": 1}})");

std::string failurePath(std::string_view schema, std::string_view value)
{
    const auto failure = validate(j(schema), j(value), kDefs);
    return failure ? failure->path : "none";
}

TEST(Schema, Types)
{
    EXPECT_EQ(failurePath(R"({"type": "integer"})", "3"), "none");
    EXPECT_EQ(failurePath(R"({"type": "integer"})", "3.0"), "");
    EXPECT_EQ(failurePath(R"({"type": "number"})", "3"), "none");
    EXPECT_EQ(failurePath(R"({"type": ["string", "null"]})", "null"), "none");
    EXPECT_EQ(failurePath(R"({"type": ["string", "null"]})", "1"), "");
}

TEST(Schema, ObjectsRequiredAndAdditional)
{
    const auto schema = R"({"type": "object", "properties": {"a": {"type": "string"}, "b/c": {"type": "boolean"}},
        "required": ["a"], "additionalProperties": false})";
    EXPECT_EQ(failurePath(schema, R"({"a": "x"})"), "none");
    EXPECT_EQ(failurePath(schema, R"({})"), "");
    EXPECT_EQ(failurePath(schema, R"({"a": 1})"), "/a");
    EXPECT_EQ(failurePath(schema, R"({"a": "x", "b/c": 1})"), "/b~1c");
    EXPECT_EQ(failurePath(schema, R"({"a": "x", "z": 1})"), "/z");
    EXPECT_EQ(failurePath(R"({"additionalProperties": {"type": "integer"}})", R"({"k": "v"})"), "/k");
}

TEST(Schema, ArraysAndBounds)
{
    const auto schema = R"({"type": "array", "items": {"$ref": "#/$defs/Id"}, "minItems": 1, "maxItems": 2})";
    EXPECT_EQ(failurePath(schema, "[1, 2]"), "none");
    EXPECT_EQ(failurePath(schema, "[]"), "");
    EXPECT_EQ(failurePath(schema, "[1, 2, 3]"), "");
    EXPECT_EQ(failurePath(schema, "[1, 0]"), "/1");
    EXPECT_EQ(failurePath(R"({"maximum": 5})", "6"), "");
    EXPECT_EQ(failurePath(R"({"minLength": 2})", "\"\xE3\x81\x82\""), "");
    EXPECT_EQ(failurePath(R"({"maxLength": 1})", "\"\xE3\x81\x82\""), "none");
}

TEST(Schema, EnumConstAndCombinators)
{
    EXPECT_EQ(failurePath(R"({"enum": ["a", 1]})", "1"), "none");
    EXPECT_EQ(failurePath(R"({"enum": ["a", 1]})", "\"b\""), "");
    EXPECT_EQ(failurePath(R"({"const": {"x": 1}})", R"({"x": 1})"), "none");
    EXPECT_EQ(failurePath(R"({"anyOf": [{"type": "string"}, {"type": "null"}]})", "null"), "none");
    EXPECT_EQ(failurePath(R"({"anyOf": [{"type": "string"}, {"type": "null"}]})", "1"), "");
    EXPECT_EQ(failurePath(R"({"oneOf": [{"type": "integer"}, {"type": "number"}]})", "1"), "");
    EXPECT_EQ(failurePath(R"({"oneOf": [{"type": "integer"}, {"type": "number"}]})", "1.5"), "none");
    EXPECT_EQ(failurePath("false", "1"), "");
    EXPECT_EQ(failurePath("true", "1"), "none");
}

TEST(Schema, CheckFindsUnsupportedOrBrokenSchemas)
{
    EXPECT_FALSE(checkSchema(j(R"({"type": "object", "properties": {"a": {"$ref": "#/$defs/Id"}},
        "required": ["a"], "additionalProperties": false, "description": "d"})"), kDefs));
    EXPECT_EQ(*checkSchema(j(R"({"properties": {"a": {"pattern": "x"}}})"), kDefs), "/properties/a/pattern: unsupported keyword");
    EXPECT_TRUE(checkSchema(j(R"({"$ref": "#/$defs/Nope"})"), kDefs));
    EXPECT_TRUE(checkSchema(j(R"({"type": "text"})"), kDefs));
    EXPECT_TRUE(checkSchema(j(R"({"minItems": -1})"), kDefs));
    EXPECT_TRUE(checkSchema(j(R"({"required": [1]})"), kDefs));
    EXPECT_TRUE(checkSchema(j(R"({"anyOf": []})"), kDefs));
    EXPECT_TRUE(checkSchema(j("3"), kDefs));
}

}
