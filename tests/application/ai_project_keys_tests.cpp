#include "hikari/application/ai_project_keys.h"

#include <gtest/gtest.h>

using namespace hikari::application::ai;

namespace {

EnvironmentLookup environmentOf(std::map<std::string, std::string, std::less<>> values)
{
    return [values = std::move(values)](std::string_view name) -> std::optional<std::string> {
        const auto found = values.find(name);
        return found == values.end() ? std::nullopt : std::optional(found->second);
    };
}

const std::map<std::string, std::string, std::less<>> kFile{{"openai", "from-file"}, {"blank", ""}};

}

TEST(ProjectKeys, TheEnvironmentComesBeforeTheFile)
{
    const auto key = resolveProjectKey({"OPENAI_API_KEY", "openai"}, environmentOf({{"OPENAI_API_KEY", "from-env"}}),
                                       kFile);
    ASSERT_TRUE(key);
    EXPECT_EQ(key->source, KeySource::Environment);
    EXPECT_EQ(key->from, "OPENAI_API_KEY");
    EXPECT_EQ(key->key, "from-env");
}

TEST(ProjectKeys, TheFileAnswersWhenTheVariableIsUnset)
{
    const auto key = resolveProjectKey({"OPENAI_API_KEY", "openai"}, environmentOf({}), kFile);
    ASSERT_TRUE(key);
    EXPECT_EQ(key->source, KeySource::CredentialsFile);
    EXPECT_EQ(key->from, "openai");
    EXPECT_EQ(key->key, "from-file");
}

TEST(ProjectKeys, EmptyValuesDontCount)
{
    EXPECT_FALSE(resolveProjectKey({"EMPTY", "blank"}, environmentOf({{"EMPTY", ""}}), kFile));
    EXPECT_FALSE(resolveProjectKey({"", "missing"}, environmentOf({}), kFile));
    EXPECT_FALSE(resolveProjectKey({}, environmentOf({{"", "x"}}), kFile));
}

TEST(ProjectKeys, ANameWithOnlyAVariableIgnoresTheFile)
{
    EXPECT_FALSE(resolveProjectKey({"OPENAI_API_KEY", ""}, environmentOf({}), kFile));
}
