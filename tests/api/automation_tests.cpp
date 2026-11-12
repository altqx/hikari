#include "fake_host.h"

#include <gtest/gtest.h>

using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

const Json kScript = Json::object({{"name", "Fade signs"}, {"script", "script_name = 'Fade'"}, {"macro", "Fade"}});

Result call(Api &api, const Caller &caller, const Json &params, FakeAutomation *finish = nullptr)
{
    std::optional<Result> out;
    api.call(caller, "automation.run", params, [&](Result r) { out = std::move(r); });
    if (finish)
        finish->finish();
    EXPECT_TRUE(out.has_value());
    return out ? *out : fail(errors::kUnavailable, "no answer");
}

Caller approved()
{
    Caller caller{"ai:t", "AI", false, {}, true};
    caller.scriptApproved = true;
    return caller;
}

}

TEST(AutomationRun, RefusesAScriptTheUserDidNotApprove)
{
    FakeHost fake;
    Api api(fake.host());
    for (Caller caller : {Caller{"plugin:x", "X", true}, Caller{"ai:t", "AI", false, {}, true, true}, Caller{"mcp:c", "C"}}) {
        const Result result = call(api, caller, kScript);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, errors::kNotGranted) << caller.id;
    }
    EXPECT_TRUE(fake.automation.runs.empty());
}

TEST(AutomationRun, RunsAnApprovedScriptAndAnswersWithItsOutcome)
{
    FakeHost fake;
    fake.automation.next = LuaRun{"failed", "attempt to index nil", "line 3", "Fade signs.lua"};
    Api api(fake.host());
    const Result result = call(api, approved(), kScript, &fake.automation);
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(fake.automation.runs.size(), 1u);
    EXPECT_EQ(fake.automation.runs[0].name, "Fade signs");
    EXPECT_EQ(fake.automation.runs[0].text, "script_name = 'Fade'");
    EXPECT_EQ(fake.automation.runs[0].macro, "Fade");
    EXPECT_EQ(*result->find("outcome"), Json("failed"));
    EXPECT_EQ(*result->find("message"), Json("attempt to index nil"));
    EXPECT_EQ(*result->find("file"), Json("Fade signs.lua"));
}

TEST(AutomationRun, ABusyHostOrNoDocumentRefuses)
{
    FakeHost fake;
    Api api(fake.host());
    fake.automation.start = LuaStart::Busy;
    EXPECT_EQ(call(api, approved(), kScript).error().code, errors::kBusy);
    fake.automation.start = LuaStart::NoDocument;
    EXPECT_EQ(call(api, approved(), kScript).error().code, errors::kRefused);
}

TEST(AutomationRun, ANameThatLeavesTheFolderIsInvalid)
{
    FakeHost fake;
    Api api(fake.host());
    for (const char *name : {"../autoload/x", "a/b", ".hidden", ""}) {
        Json params = kScript;
        params["name"] = name;
        EXPECT_EQ(call(api, approved(), params).error().code, errors::kInvalidParams) << name;
    }
    EXPECT_TRUE(fake.automation.runs.empty());
}
