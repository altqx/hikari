#include "hikari/application/export_steps.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

namespace app = hikari::application;
namespace core = hikari::core;
using Outcome = app::ExportStepOutcome;

core::Document documentWith(std::string_view events, std::string_view garbage = {})
{
    std::string data = "[Script Info]\nScriptType: v4.00+\n\n";
    if (!garbage.empty())
        data += "[Aegisub Project Garbage]\n" + std::string(garbage) + "\n\n";
    data += "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
            "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
            "Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
            "10,10,10,1\n"
            "Style: Sign,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
            "10,10,10,1\n\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    data += events;
    std::vector<std::byte> bytes(data.size());
    std::memcpy(bytes.data(), data.data(), bytes.size());
    return core::loadAss(bytes).document;
}

std::vector<std::string> field(const core::Document &d, std::u8string core::LineRecord::*member)
{
    std::vector<std::string> out;
    for (const auto *l : d.lines())
        out.emplace_back((l->*member).begin(), (l->*member).end());
    return out;
}

std::vector<std::int64_t> times(const core::Document &d)
{
    std::vector<std::int64_t> out;
    for (const auto *l : d.lines()) {
        out.push_back(l->start.value.microseconds() / 1000);
        out.push_back(l->end.value.microseconds() / 1000);
    }
    return out;
}

app::ScriptStatus script(std::string path, std::vector<app::ScriptFilter> filters,
                         app::ScriptStatus::State state = app::ScriptStatus::State::Ready)
{
    app::ScriptStatus s;
    s.path = std::move(path);
    s.state = state;
    s.info.filters = std::move(filters);
    return s;
}

std::vector<std::string> ids(const std::vector<app::AvailableStep> &steps)
{
    std::vector<std::string> out;
    for (const auto &s : steps)
        out.push_back(s.id);
    return out;
}

class FakeFilters : public app::ExportFilterPort {
public:
    bool available(const std::string &script, const std::string &filter) const override
    {
        return loaded.contains(script + "/" + filter);
    }
    bool run(const std::string &script, const std::string &filter, const app::MacroSnapshot &snapshot,
             const app::DialogRequest &settings, Done done) override
    {
        if (refuse)
            return false;
        ran.push_back(script + "/" + filter);
        lastSettings = settings;
        const auto it = loaded.find(script + "/" + filter);
        if (it->second.empty())
            return done(std::unexpected(std::string("sign blur broke on line 1"))), true;
        app::MacroResult result;
        result.info = snapshot.info;
        result.styles = snapshot.styles;
        result.dialogues = snapshot.dialogues;
        for (auto &d : result.dialogues)
            d.text = it->second + d.text;
        if (filter == "Karaoke template") {
            auto made = result.dialogues.front();
            made.id = 0;
            result.dialogues.push_back(made);
        }
        done(std::move(result));
        return true;
    }
    std::map<std::string, std::string> loaded; // empty value: the run fails
    bool refuse = false;
    std::vector<std::string> ran;
    app::DialogRequest lastSettings;
};

std::optional<app::ExportStepsResult> runSteps(core::Document document, const app::ExportRecipe &recipe,
                                               const std::vector<app::AvailableStep> &available,
                                               app::ExportStepsContext context)
{
    std::optional<app::ExportStepsResult> result;
    app::runExportSteps(std::move(document), recipe, available, context,
                        [&](app::ExportStepsResult r) { result = std::move(r); });
    return result;
}

TEST(ExportSteps, BuiltInsAndLuaFiltersComeInAegisubsPriorityOrder)
{
    const auto steps = app::availableSteps(
        {script("/shipped/cleantags-autoload.lua", {{"Clean Tags", "", 0, false}}),
         script("/project/sign-fx.lua", {{"Sign blur pass", "", 500, true}, {"Late", "", -10000, false}}),
         script("/shipped/kara-templater.lua", {{"Karaoke template", "", 2000, false}}),
         script("/broken.lua", {{"Never", "", 9000, false}}, app::ScriptStatus::State::LoadFailed)});
    EXPECT_EQ(ids(steps), (std::vector<std::string>{"karaoke-template", "transform-frame-rate",
                                                    "lua:sign-fx.lua/Sign blur pass", "remove-tags", "fix-styles",
                                                    "lua:sign-fx.lua/Late"}));
    EXPECT_EQ(steps[0].name, "Apply karaoke template");
    EXPECT_EQ(steps[0].script, "kara-templater.lua");
    EXPECT_EQ(steps[3].name, "Remove tags");
    EXPECT_TRUE(steps[3].script.empty());
    EXPECT_EQ(steps[4].name, "Set unknown styles to Default");
    EXPECT_EQ(steps[2].script, "sign-fx.lua");
    EXPECT_TRUE(steps[2].hasConfig);

    const auto list = app::newStepList(steps);
    ASSERT_EQ(list.steps.size(), steps.size());
    EXPECT_TRUE(std::ranges::none_of(list.steps, &app::ExportStep::on));
    EXPECT_EQ(list.steps[3].settings.at("cleanTags"), app::DialogValue(true));
}

TEST(ExportSteps, TheExportFiltersLineReadsAsARecipeAndIsWrittenBack)
{
    const auto document = documentWith("", "Export Filters: Fix Styles|Sign blur pass||Transform Framerate|Gone");
    const auto available = app::availableSteps({script("/p/sign-fx.lua", {{"Sign blur pass", "", 500, true}})});
    EXPECT_EQ(app::exportFilterNames(document),
              (std::vector<std::string>{"Fix Styles", "Sign blur pass", "Transform Framerate", "Gone"}));
    auto recipe = app::recipeFromExportFilters(app::exportFilterNames(document), available);
    std::vector<std::string> stepIds;
    for (const auto &s : recipe.steps)
        stepIds.push_back(s.id);
    EXPECT_EQ(stepIds, (std::vector<std::string>{"fix-styles", "lua:sign-fx.lua/Sign blur pass",
                                                 "transform-frame-rate", "lua:/Gone"}));
    EXPECT_TRUE(std::ranges::all_of(recipe.steps, &app::ExportStep::on));
    EXPECT_EQ(app::exportFiltersLine(recipe, available), "Fix Styles|Sign blur pass|Transform Framerate|Gone");
    recipe.steps[1].on = false;
    EXPECT_EQ(app::exportFiltersLine(recipe, available), "Fix Styles|Transform Framerate|Gone");
    EXPECT_TRUE(app::exportFilterNames(documentWith("")).empty());
}

TEST(ExportSteps, UnknownStylesBecomeDefaultIgnoringCase)
{
    auto document = documentWith("Dialogue: 0,0:00:01.00,0:00:02.00,sign,,0,0,0,,a\n"
                                 "Dialogue: 0,0:00:01.00,0:00:02.00,Missing,,0,0,0,,b\n"
                                 "Comment: 0,0:00:01.00,0:00:02.00,Gone,,0,0,0,,c\n");
    EXPECT_EQ(app::fixUnknownStyles(document), 2);
    EXPECT_EQ(field(document, &core::LineRecord::style), (std::vector<std::string>{"sign", "Default", "Default"}));
}

TEST(ExportSteps, TransformFrameRateMovesLineAndTagTimesAsAegisubDoes)
{
    auto document = documentWith(
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,"
        "{\\fad(200,400)\\move(0,0,10,10,100,300)\\t(0,500,\\fs20)}{\\k50}a{\\k30}b{\\kt5\\move(1,2,3,4)}\n");
    const auto output = app::constantFrameTimes(25);
    const auto input = app::constantFrameTimes(50);
    ASSERT_TRUE(output && input);
    EXPECT_EQ(app::transformFrameRate(document, {*input, *output}), 1);
    EXPECT_EQ(times(document), (std::vector<std::int64_t>{500, 1000}));
    EXPECT_EQ(field(document, &core::LineRecord::text)[0],
              "{\\fad(100,200)\\move(0,0,10,10,50,150)\\t(0,250,\\fs20)}{\\k25}a{\\k15}b{\\kt5\\move(1,2,3,4)}");
    EXPECT_FALSE(app::constantFrameTimes(0));
    EXPECT_FALSE(app::constantFrameTimes(1001));
}

TEST(ExportSteps, StepsRunInOrderOnTheCopyAndAMissingScriptIsSkipped)
{
    FakeFilters filters;
    filters.loaded = {{"sign-fx.lua/Sign blur pass", "> "}, {"kara.lua/Karaoke template", "k "}};
    const auto available =
        app::availableSteps({script("/p/sign-fx.lua", {{"Sign blur pass", "", 500, true}}),
                             script("/s/kara.lua", {{"Karaoke template", "", 2000, false}})});
    app::ExportRecipe recipe{{{"fix-styles", true, {}},
                              {"karaoke-template", true, {}},
                              {"lua:sign-fx.lua/Sign blur pass", true, {{"strength", 3}, {"label", std::monostate{}}}},
                              {"lua:gone.lua/Gone", true, {}},
                              {"remove-tags", false, {}}}};
    const auto result = runSteps(documentWith("Dialogue: 0,0:00:01.00,0:00:02.00,Missing,,0,0,0,,{\\b1}a\n"), recipe,
                                 available, {.filters = &filters});
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->failed);
    EXPECT_EQ(field(result->document, &core::LineRecord::text), (std::vector<std::string>{"> k {\\b1}a", "> k {\\b1}a"}));
    EXPECT_EQ(field(result->document, &core::LineRecord::style), (std::vector<std::string>{"Default", "Default"}));
    EXPECT_EQ(filters.ran, (std::vector<std::string>{"kara.lua/Karaoke template", "sign-fx.lua/Sign blur pass"}));
    ASSERT_EQ(filters.lastSettings.controls.size(), 1u);
    EXPECT_EQ(filters.lastSettings.controls[0].name, "strength");
    EXPECT_EQ(filters.lastSettings.controls[0].intValue, 3);
    EXPECT_EQ(result->steps,
              (std::vector<Outcome>{{"fix-styles", "Set unknown styles to Default", Outcome::State::Done, 1},
                                    {"karaoke-template", "Apply karaoke template", Outcome::State::Done, 1},
                                    {"lua:sign-fx.lua/Sign blur pass", "Sign blur pass", Outcome::State::Done},
                                    {"lua:gone.lua/Gone", "Gone", Outcome::State::Skipped}}));
}

TEST(ExportSteps, AFailingStepStopsTheRunAndTheRestDontRun)
{
    FakeFilters filters;
    filters.loaded = {{"sign-fx.lua/Sign blur pass", ""}};
    const auto available = app::availableSteps({script("/p/sign-fx.lua", {{"Sign blur pass", "", 500, true}})});
    app::ExportRecipe recipe{{{"lua:sign-fx.lua/Sign blur pass", true, {}},
                              {"fix-styles", true, {}},
                              {"karaoke-template", true, {}}}};
    const auto result =
        runSteps(documentWith("Dialogue: 0,0:00:01.00,0:00:02.00,Missing,,0,0,0,,a\n"), recipe, available,
                 {.filters = &filters});
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->failed);
    ASSERT_EQ(result->steps.size(), 3u);
    EXPECT_EQ(result->steps[0].state, Outcome::State::Failed);
    EXPECT_EQ(result->steps[0].problem, Outcome::Problem::ScriptError);
    EXPECT_EQ(result->steps[0].error, "sign blur broke on line 1");
    EXPECT_EQ(result->steps[1].state, Outcome::State::NotRun);
    EXPECT_EQ(result->steps[2].state, Outcome::State::NotRun);
    EXPECT_EQ(field(result->document, &core::LineRecord::style), (std::vector<std::string>{"Missing"}));
}

TEST(ExportSteps, AScriptThatCantStartFailsAsBusy)
{
    FakeFilters filters;
    filters.loaded = {{"sign-fx.lua/Sign blur pass", "x"}};
    filters.refuse = true;
    const auto available = app::availableSteps({script("/p/sign-fx.lua", {{"Sign blur pass", "", 500, false}})});
    const auto result = runSteps(documentWith("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"),
                                 {{{"lua:sign-fx.lua/Sign blur pass", true, {}}}}, available, {.filters = &filters});
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->failed);
    EXPECT_EQ(result->steps[0].problem, Outcome::Problem::Busy);
}

TEST(ExportSteps, TransformFrameRateNeedsTimecodesForAVariableOutput)
{
    app::ExportRecipe recipe{{{"transform-frame-rate", true, app::defaultSettings("transform-frame-rate")}}};
    const auto line = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n";
    auto result = runSteps(documentWith(line), recipe, app::availableSteps({}), {});
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->failed);
    EXPECT_EQ(result->steps[0].problem, Outcome::Problem::NoTimecodes);

    result = runSteps(documentWith(line), recipe, app::availableSteps({}), {.timecodes = app::constantFrameTimes(23.976)});
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->failed);

    recipe.steps[0].settings["output"] = std::string("constant");
    recipe.steps[0].settings["outputFps"] = 0.0;
    result = runSteps(documentWith(line), recipe, app::availableSteps({}), {});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->steps[0].problem, Outcome::Problem::BadFrameRate);
}

}
