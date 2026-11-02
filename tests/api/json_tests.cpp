#include "hikari/api/json.h"

#include <gtest/gtest.h>

#include <string>

using hikari::api::Json;

namespace {

Json parsed(std::string_view text)
{
    auto value = Json::parse(text);
    EXPECT_TRUE(value) << text << ": " << (value ? "" : value.error().message);
    return value ? *value : Json();
}

TEST(Json, ReadsEveryKind)
{
    const Json v = parsed(R"( {"a": null, "b": true, "c": -12, "d": 0.5, "e": "x", "f": [1, 2], "g": {}} )");
    ASSERT_TRUE(v.isObject());
    EXPECT_TRUE(v.find("a")->isNull());
    EXPECT_TRUE(v.find("b")->asBool());
    EXPECT_EQ(v.find("c")->asInt(), -12);
    EXPECT_DOUBLE_EQ(v.find("d")->asNumber(), 0.5);
    EXPECT_EQ(v.find("e")->asString(), "x");
    EXPECT_EQ(v.find("f")->asArray().size(), 2u);
    EXPECT_TRUE(v.find("g")->isObject());
    EXPECT_EQ(v.find("missing"), nullptr);
}

TEST(Json, KeepsKeyOrderAndRoundTrips)
{
    const std::string text = R"({"z":1,"a":[true,false,null],"m":{"k":"v"},"n":-0.25,"big":9007199254740993})";
    EXPECT_EQ(parsed(text).dump(), text);
}

TEST(Json, IntegersStayExactAndDiffersFromDouble)
{
    EXPECT_EQ(parsed("9223372036854775807").asInt(), 9223372036854775807);
    EXPECT_TRUE(parsed("1.0").type() == Json::Type::Double);
    EXPECT_FALSE(parsed("1") == parsed("1.0"));
    EXPECT_TRUE(parsed("1e2").type() == Json::Type::Double);
    EXPECT_TRUE(parsed("92233720368547758070").type() == Json::Type::Double);
}

TEST(Json, EscapesAndUnicode)
{
    EXPECT_EQ(parsed(R"("\u00e9\ud83d\ude00\n\"\\\/")").asString(), "\xC3\xA9\xF0\x9F\x98\x80\n\"\\/");
    EXPECT_EQ(Json(std::string("a\x01\"\n")).dump(), R"("a\u0001\"\n")");
    EXPECT_EQ(Json("\xE3\x81\x82").dump(), "\"\xE3\x81\x82\"");
}

TEST(Json, RefusesMalformedText)
{
    for (const char *bad : {"", "{", "[1,]", R"({"a":1,})", R"({"a" 1})", "01", "1.", "-", "tru", "\"\\x\"",
                            "\"\\ud800\"", "\"\\udc00\"", "\"a\nb\"", "\"\xC3\"", "\"\xC0\x80\"", "\"\xED\xA0\x80\"",
                            "1 2", R"({"a":1,"a":2})", "nan", "1e999"}) {
        EXPECT_FALSE(Json::parse(bad)) << bad;
    }
}

TEST(Json, RefusesDeepNesting)
{
    EXPECT_TRUE(Json::parse(std::string(Json::kMaxDepth, '[') + std::string(Json::kMaxDepth, ']')));
    EXPECT_FALSE(Json::parse(std::string(Json::kMaxDepth + 2, '[') + std::string(Json::kMaxDepth + 2, ']')));
}

TEST(Json, BuildsAndIndents)
{
    Json v;
    v["name"] = "x";
    v["list"].push(1);
    v["list"].push(Json::object({{"k", Json(false)}}));
    EXPECT_EQ(v.dump(), R"({"name":"x","list":[1,{"k":false}]})");
    EXPECT_EQ(v.dump(2), "{\n  \"name\": \"x\",\n  \"list\": [\n    1,\n    {\n      \"k\": false\n    }\n  ]\n}");
    v["name"] = "y";
    EXPECT_EQ(v.asObject().size(), 2u);
    EXPECT_EQ(Json::array().dump(2), "[]");
}

}
