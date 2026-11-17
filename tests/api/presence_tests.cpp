#include "fake_host.h"
#include "script_client.h"

#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

class FakePresence final : public PresenceHost {
public:
    void setPluginText(std::string_view plugin, std::string_view text) override
    {
        set.emplace_back(std::string(plugin) + "=" + std::string(text));
    }
    std::vector<std::string> set;
};

Caller tracker(bool presence = true)
{
    Caller caller{"plugin:example.tracker", "Tracker", false};
    caller.presence = presence;
    return caller;
}

class Presence : public ::testing::Test {
protected:
    Result call(std::string_view params, const Caller &caller = tracker())
    {
        auto parsed = Json::parse(params);
        EXPECT_TRUE(parsed) << params;
        return client.call(caller, "presence.set_text", *parsed);
    }

    Host wired()
    {
        Host h = host.host();
        h.presence = &presence;
        return h;
    }

    FakeHost host;
    FakePresence presence;
    Api api{wired()};
    ScriptClient client{api};
};

std::string codeOf(const Result &result)
{
    return result ? "ok" : result.error().code;
}

TEST_F(Presence, APluginWithThePermissionSetsAndClearsItsText)
{
    EXPECT_EQ(codeOf(call(R"({"text": "Ep 5 of 12"})")), "ok");
    EXPECT_EQ(codeOf(call(R"({"text": ""})")), "ok");
    EXPECT_EQ(presence.set, (std::vector<std::string>{"example.tracker=Ep 5 of 12", "example.tracker="}));
}

TEST_F(Presence, OnlyAPluginWithThePermissionIsGranted)
{
    EXPECT_EQ(codeOf(call(R"({"text": "x"})", tracker(false))), "not_granted");
    Caller ai{"ai:agent", "Agent", false};
    ai.presence = true;
    EXPECT_EQ(codeOf(call(R"({"text": "x"})", ai)), "not_granted");
    EXPECT_TRUE(presence.set.empty());
}

TEST_F(Presence, TheTextIsShortPlainAndOneLine)
{
    EXPECT_EQ(codeOf(call(R"({"text": "line\nbreak"})")), "invalid_params");
    EXPECT_EQ(codeOf(call(R"({"text": "tab\there"})")), "invalid_params");
    EXPECT_EQ(codeOf(call("{\"text\": \"" + std::string(65, 'x') + "\"}")), "invalid_params");
    EXPECT_EQ(codeOf(call("{\"text\": \"" + std::string(64, 'x') + "\"}")), "ok");
    EXPECT_EQ(codeOf(call("{}")), "invalid_params");
    EXPECT_EQ(presence.set.size(), 1u);
}

}
