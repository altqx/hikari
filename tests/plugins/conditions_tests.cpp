#include "hikari/plugins/conditions.h"

#include <gtest/gtest.h>

using namespace hikari::plugins;

TEST(PluginConditions, EveryConditionMustHold)
{
    ConditionFacts facts;
    facts.document = true;
    facts.selectedLines = 1;
    EXPECT_EQ(failingCondition({}, facts), std::nullopt);
    EXPECT_EQ(failingCondition({"document", "selection"}, facts), std::nullopt);
    EXPECT_EQ(failingCondition({"document", "multipleLines"}, facts), "multipleLines");
    facts.selectedLines = 2;
    EXPECT_EQ(failingCondition({"multipleLines"}, facts), std::nullopt);
    facts.selectedLines = 0;
    EXPECT_EQ(failingCondition({"selection"}, facts), "selection");
}

TEST(PluginConditions, ABangNegatesOne)
{
    ConditionFacts facts;
    facts.video = true;
    EXPECT_EQ(failingCondition({"!audio", "!video"}, facts), "!video");
    EXPECT_EQ(failingCondition({"video", "!audio"}, facts), std::nullopt);
}

TEST(PluginConditions, EachFactBacksItsCondition)
{
    const std::pair<const char *, bool ConditionFacts::*> flags[] = {
        {"document", &ConditionFacts::document}, {"video", &ConditionFacts::video},
        {"audio", &ConditionFacts::audio},       {"karaoke", &ConditionFacts::karaoke},
        {"translation", &ConditionFacts::translation}};
    for (const auto &[name, flag] : flags) {
        ConditionFacts facts;
        EXPECT_EQ(failingCondition({name}, facts), name);
        facts.*flag = true;
        EXPECT_EQ(failingCondition({name}, facts), std::nullopt) << name;
    }
}
