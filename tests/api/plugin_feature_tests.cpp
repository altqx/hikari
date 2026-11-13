#include "fake_host.h"
#include "script_client.h"

#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

class PluginFeatures : public ::testing::Test {
protected:
    Result call(std::string_view operation, std::string_view params, const Caller &caller = chains())
    {
        auto parsed = Json::parse(params);
        EXPECT_TRUE(parsed) << params;
        return client.call(caller, operation, *parsed);
    }

    void callLater(std::string_view operation, std::string_view params, std::optional<Result> &out,
                   const Caller &caller = chains())
    {
        api.call(caller, operation, *Json::parse(params), [&out](Result r) { out = std::move(r); });
    }

    static Caller chains() { return Caller{"plugin:example.chains", "Chains", false}; }

    FakeHost host;
    FakeMacros &macros = host.macros;
    Api api{host.host()};
    ScriptClient client{api};
};

const std::string &code(const Result &result)
{
    static const std::string none;
    return result ? none : result.error().code;
}

std::string reason(const Result &result)
{
    if (result)
        return {};
    const Json *r = result.error().data.find("reason");
    return r ? r->asString() : std::string();
}

std::string fromBase64(std::string_view text)
{
    constexpr std::string_view kDigits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned bits = 0;
    int count = 0;
    for (const char c : text) {
        const auto digit = kDigits.find(c);
        if (digit == std::string_view::npos)
            break;
        bits = (bits << 6) | unsigned(digit);
        if ((count += 6) >= 8)
            out += char((bits >> (count -= 8)) & 0xFF);
    }
    return out;
}

}

TEST_F(PluginFeatures, MacrosListsTheLoadedMacrosAndFiltersByQuery)
{
    const Result all = call("automation.macros", "{}");
    ASSERT_TRUE(all) << all.error().message;
    EXPECT_EQ(all->find("macros")->asArray().size(), 3u);
    const Result shift = call("automation.macros", R"({"query": "SHIFT"})", aiCaller());
    ASSERT_TRUE(shift) << shift.error().message;
    ASSERT_EQ(shift->find("macros")->asArray().size(), 1u);
    const Json &row = shift->find("macros")->asArray()[0];
    EXPECT_EQ(row.find("file")->asString(), "fade.lua");
    EXPECT_EQ(row.find("ordinal")->asInt(), 1);
    EXPECT_FALSE(row.find("enabled")->asBool());
    EXPECT_EQ(row.find("disabledReason")->asString(), "Not shown here.");
}

TEST_F(PluginFeatures, RunMacroIsForPluginsOnly)
{
    for (const Caller &caller : {aiCaller(), Caller{"mcp:c", "C"}}) {
        std::optional<Result> out;
        callLater("automation.run_macro", R"({"file": "fade.lua", "macro": "Fade"})", out, caller);
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(code(*out), errors::kNotGranted) << caller.id;
    }
    EXPECT_TRUE(macros.requests.empty());
}

TEST_F(PluginFeatures, RunMacroPassesTheAnswersAndAnswersWithTheOutcomeWhenTheMacroEnds)
{
    macros.next = LuaRun{"applied", "", "3 lines", "fade.lua"};
    std::optional<Result> out;
    callLater("automation.run_macro", R"({"file": "fade.lua", "macro": "Fade", "show": "prefilled",
        "answers": [{"key": "0123456789abcdef", "values": {"n": 3, "name": "x", "on": true, "f": 0.5}, "button": "OK"}]})",
              out);
    EXPECT_FALSE(out.has_value());
    ASSERT_EQ(macros.requests.size(), 1u);
    const MacroRequest &request = macros.requests[0];
    EXPECT_EQ(request.file, "fade.lua");
    EXPECT_EQ(request.macro, "Fade");
    EXPECT_TRUE(request.showAnswered);
    ASSERT_EQ(request.answers.size(), 1u);
    EXPECT_EQ(request.answers[0].key, "0123456789abcdef");
    EXPECT_EQ(request.answers[0].button, "OK");
    const application::NamedDialogValues expected{
        {"n", 3}, {"name", std::string("x")}, {"on", true}, {"f", 0.5}};
    EXPECT_EQ(request.answers[0].values, expected);
    macros.finish();
    ASSERT_TRUE(out.has_value());
    ASSERT_TRUE(*out) << (*out).error().message;
    EXPECT_EQ((*out)->find("outcome")->asString(), "applied");
    EXPECT_EQ((*out)->find("log")->asString(), "3 lines");
}

TEST_F(PluginFeatures, RunMacroRejectsAKeyThatIsNotADialogKey)
{
    EXPECT_EQ(code(call("automation.run_macro", R"({"file": "fade.lua", "macro": "Fade",
        "answers": [{"key": "not-a-key", "values": {}}]})")),
              errors::kInvalidParams);
}

TEST_F(PluginFeatures, RunMacroSaysWhyItCouldNotStart)
{
    const auto run = [&] { return call("automation.run_macro", R"({"file": "fade.lua", "macro": "Fade"})"); };
    macros.start = MacroStart::Busy;
    EXPECT_EQ(code(run()), errors::kBusy);
    macros.start = MacroStart::NotFound;
    EXPECT_EQ(code(run()), errors::kNotFound);
    for (const auto &[start, why] : {std::pair{MacroStart::NoDocument, "no_document"},
                                     std::pair{MacroStart::Ambiguous, "ambiguous"},
                                     std::pair{MacroStart::Disabled, "disabled"}}) {
        macros.start = start;
        const Result result = run();
        EXPECT_EQ(code(result), errors::kRefused) << why;
        EXPECT_EQ(reason(result), why);
    }
}

TEST_F(PluginFeatures, AMacroRunsOnTheSelectionThePluginSetJustBefore)
{
    std::set<core::LineId> seen;
    macros.onRun = [&] { seen = host.target()->selection().selected; };
    ASSERT_TRUE(call("selection.set", R"({"selected": [2]})"));
    std::optional<Result> out;
    callLater("automation.run_macro", R"({"file": "fade.lua", "macro": "Fade"})", out);
    EXPECT_EQ(seen, std::set<core::LineId>{core::LineId{2}});
    macros.finish();
    ASSERT_TRUE(out && *out);
    ASSERT_TRUE(call("selection.set", R"({"selected": [1, 3]})"));
    callLater("automation.run_macro", R"({"file": "fade.lua", "macro": "Fade"})", out);
    EXPECT_EQ(seen, (std::set<core::LineId>{core::LineId{1}, core::LineId{3}}));
}

TEST_F(PluginFeatures, MacroRanTellsSubscribersTheOutcomeAndTheAnswersToReplay)
{
    std::vector<Event> heard;
    api.subscribe(chains(), {"macro.ran"}, [&](const Event &e) { heard.push_back(e); });
    const auto document = *host.workspace().editingTarget();
    api.macroRan(document, "fade.lua", "Fade", "applied",
                 {DialogAnswer{"0123456789abcdef", {{"n", 3}, {"name", std::string("x")}}, "OK"}});
    ASSERT_EQ(heard.size(), 1u);
    const Json &data = heard[0].data;
    EXPECT_EQ(data.find("document")->asInt(), std::int64_t(document.value));
    EXPECT_EQ(data.find("file")->asString(), "fade.lua");
    EXPECT_EQ(data.find("macro")->asString(), "Fade");
    EXPECT_EQ(data.find("outcome")->asString(), "applied");
    EXPECT_TRUE(data.find("ok")->asBool());
    const Json &dialog = data.find("dialogs")->asArray().at(0);
    EXPECT_EQ(dialog.find("key")->asString(), "0123456789abcdef");
    EXPECT_EQ(dialog.find("values")->find("n")->asInt(), 3);
    EXPECT_EQ(dialog.find("values")->find("name")->asString(), "x");
    EXPECT_EQ(dialog.find("button")->asString(), "OK");
}

TEST_F(PluginFeatures, CommandsListGivesHotkeysMacrosAndPluginCommandsByKind)
{
    const Result result = call("commands.list", "{}");
    ASSERT_TRUE(result) << result.error().message;
    std::map<std::string, std::string> kinds;
    for (const Json &c : result->find("commands")->asArray())
        kinds[c.find("command")->asString()] = c.find("kind")->asString();
    EXPECT_EQ(kinds["GLOBAL_SAVE_SUBS"], "hotkey");
    EXPECT_EQ(kinds["macro:fade.lua:0"], "macro");
    EXPECT_EQ(kinds["plugin.org.example.word-count.count"], "plugin");

    const Result video = call("commands.list", R"({"window": "video"})");
    ASSERT_TRUE(video);
    for (const Json &c : video->find("commands")->asArray())
        EXPECT_EQ(c.find("kind")->asString(), "hotkey");
}

TEST_F(PluginFeatures, CommandsRunStartsAMacroOrAnotherPluginsCommand)
{
    ASSERT_TRUE(call("commands.run", R"({"command": "macro:fade.lua:0"})"));
    ASSERT_EQ(macros.requests.size(), 1u);
    EXPECT_EQ(macros.requests[0].macro, "Fade");
    macros.finish();
    ASSERT_TRUE(call("commands.run", R"({"command": "plugin.org.example.word-count.count"})"));
    EXPECT_EQ(host.fakePlugins.runs, std::vector<std::string>{"org.example.word-count/count"});
    EXPECT_EQ(code(call("commands.run", R"({"command": "plugin.org.example.word-count.count"})",
                        Caller{"plugin:org.example.word-count", "Word count", false})),
              errors::kRefused);
}

TEST_F(PluginFeatures, MarksBelongToTheCallingPlugin)
{
    const Result set = call("marks.set", R"({"marks": [{"line": 1, "colour": "warning", "tooltip": "Changed"},
        {"line": 3, "colour": "danger", "tooltip": "Removed"}]})");
    ASSERT_TRUE(set) << set.error().message;
    EXPECT_EQ(set->find("marked")->asInt(), 2);
    ASSERT_TRUE(call("marks.set", R"({"marks": [{"line": 1, "colour": "accent", "tooltip": "Mine"}]})",
                     Caller{"plugin:example.other", "Other", false}));

    const Result list = call("marks.list", "{}");
    ASSERT_TRUE(list);
    ASSERT_EQ(list->find("marks")->asArray().size(), 2u);
    EXPECT_EQ(list->find("marks")->asArray()[0].find("colour")->asString(), "warning");
    const auto document = *host.workspace().editingTarget();
    EXPECT_EQ(host.marks.shown(document).at(1).size(), 2u);

    ASSERT_TRUE(call("marks.clear", R"({"lines": [1]})"));
    EXPECT_EQ(call("marks.list", "{}")->find("marks")->asArray().size(), 1u);
    ASSERT_TRUE(call("marks.clear", "{}"));
    EXPECT_TRUE(call("marks.list", "{}")->find("marks")->asArray().empty());
    EXPECT_EQ(host.marks.shown(document).at(1).at(0).owner, "plugin:example.other");
}

TEST_F(PluginFeatures, MarksRefuseOtherCallersAndUnknownLines)
{
    EXPECT_EQ(code(call("marks.set", R"({"marks": [{"line": 1, "colour": "accent", "tooltip": "x"}]})", aiCaller())),
              errors::kNotGranted);
    EXPECT_EQ(code(call("marks.list", "{}", aiCaller())), errors::kNotGranted);
    EXPECT_EQ(code(call("marks.set", R"({"marks": [{"line": 99, "colour": "accent", "tooltip": "x"}]})")),
              errors::kNotFound);
    EXPECT_EQ(code(call("marks.set", R"({"marks": [{"line": 1, "colour": "pink", "tooltip": "x"}]})")),
              errors::kInvalidParams);
    EXPECT_EQ(code(call("marks.set", R"({"marks": [{"line": 1, "colour": "accent", "tooltip": ""}]})")),
              errors::kInvalidParams);
}

TEST_F(PluginFeatures, FramesComeScaledAsLumaOrBgra)
{
    const Result bgra = call("video.frames", R"({"from": 2, "to": 6, "stride": 2, "maxEdge": 64, "format": "bgra"})");
    ASSERT_TRUE(bgra) << bgra.error().message;
    EXPECT_EQ(bgra->find("width")->asInt(), 64);
    EXPECT_EQ(bgra->find("height")->asInt(), 36);
    const Json::Array &frames = bgra->find("frames")->asArray();
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[1].find("frame")->asInt(), 4);
    const std::string pixels = fromBase64(frames[1].find("data")->asString());
    ASSERT_EQ(pixels.size(), 64u * 36 * 4);
    EXPECT_EQ(pixels.substr(5 * 4, 4), std::string("\x05\x00\x04\xFF", 4));

    const Result luma = call("video.frames", R"({"from": 99, "to": 99})");
    ASSERT_TRUE(luma) << luma.error().message;
    const std::string grey = fromBase64(luma->find("frames")->asArray()[0].find("data")->asString());
    ASSERT_EQ(grey.size(), 64u * 36);
    EXPECT_EQ(std::uint8_t(grey[63]), (77 * 99 + 29 * 63 + 128) >> 8);

    const Result small = call("video.frames", R"({"from": 0, "to": 0, "maxEdge": 32})");
    ASSERT_TRUE(small);
    EXPECT_EQ(small->find("width")->asInt(), 32);
    EXPECT_EQ(small->find("height")->asInt(), 18);
    EXPECT_EQ(fromBase64(small->find("frames")->asArray()[0].find("data")->asString()).size(), 32u * 18);
}

TEST_F(PluginFeatures, FramesKeepToTheLimits)
{
    EXPECT_EQ(code(call("video.frames", R"({"from": 5, "to": 4})")), errors::kInvalidParams);
    EXPECT_EQ(code(call("video.frames", R"({"from": 0, "to": 256})")), errors::kInvalidParams);
    EXPECT_TRUE(call("video.frames", R"({"from": 0, "to": 99, "stride": 33})"));
    EXPECT_EQ(code(call("video.frames", R"({"from": 98, "to": 100})")), errors::kUnavailable);
    host.media.videoOpen = false;
    EXPECT_FALSE(call("video.frames", R"({"from": 0, "to": 0})"));
}

TEST_F(PluginFeatures, SetKeyframesSortsThemAndWritesAGrantedFile)
{
    const Result plain = call("video.set_keyframes", R"({"frames": [30, 0, 12, 30]})");
    ASSERT_TRUE(plain) << plain.error().message;
    EXPECT_EQ(plain->find("count")->asInt(), 3);
    EXPECT_EQ(host.keyframes.set, (std::vector<std::int64_t>{0, 12, 30}));
    EXPECT_FALSE(host.keyframes.written.has_value());

    const std::string writable = api.grantFile(chains().id, "/kf/ep01_keyframes.txt", true);
    const std::string readOnly = api.grantFile(chains().id, "/kf/old.txt", false);
    const Result written = call("video.set_keyframes", R"({"frames": [5], "file": ")" + writable + R"("})");
    ASSERT_TRUE(written) << written.error().message;
    EXPECT_EQ(written->find("file")->asString(), writable);
    EXPECT_EQ(host.keyframes.written, std::optional<std::string>("/kf/ep01_keyframes.txt"));
    EXPECT_EQ(code(call("video.set_keyframes", R"({"frames": [5], "file": ")" + readOnly + R"("})")), errors::kReadOnly);
    EXPECT_EQ(code(call("video.set_keyframes", R"({"frames": [5], "file": "f99"})")), errors::kNotGranted);

    host.keyframes.problem = "No video is open.";
    EXPECT_EQ(reason(call("video.set_keyframes", R"({"frames": [5]})")), "not_set");
}

TEST_F(PluginFeatures, AnyPluginReachesWhatTheOfficialPluginsUse)
{
    std::optional<Result> ran;
    callLater("automation.run_macro", R"({"file": "fade.lua", "macro": "Fade"})", ran);
    macros.finish();
    ASSERT_TRUE(ran && *ran);
    EXPECT_TRUE(call("marks.set", R"({"marks": [{"line": 2, "colour": "success", "tooltip": "Ok"}]})"));
    EXPECT_TRUE(call("video.frames", R"({"from": 0, "to": 3})"));
    EXPECT_TRUE(call("video.set_keyframes", R"({"frames": [0, 24]})"));
}

TEST(RowMarks, ShownMergesOwnersPerLineAndClearOwnerDropsOne)
{
    application::RowMarks marks;
    std::vector<std::uint64_t> changed;
    marks.setObserver([&](application::DocumentId d) { changed.push_back(d.value); });
    const application::DocumentId a{1}, b{2};
    using application::MarkColour;
    ASSERT_TRUE(marks.set("plugin:x", "X", a, {{core::LineId{5}, MarkColour::Warning, "w"}}));
    ASSERT_TRUE(marks.set("plugin:y", "Y", a, {{core::LineId{5}, MarkColour::Danger, "d"}}));
    ASSERT_TRUE(marks.set("plugin:x", "X", b, {{core::LineId{1}, MarkColour::Accent, "a"}}));
    EXPECT_EQ(changed, (std::vector<std::uint64_t>{1, 1, 2}));
    ASSERT_EQ(marks.shown(a).at(5).size(), 2u);
    EXPECT_EQ(marks.shown(a).at(5)[1].label, "Y");
    EXPECT_EQ(marks.counts("plugin:x"), (std::map<std::uint64_t, std::size_t>{{1, 1}, {2, 1}}));

    ASSERT_TRUE(marks.set("plugin:x", "X", a, {{core::LineId{5}, MarkColour::Success, "again"}}));
    EXPECT_EQ(marks.of("plugin:x", a).at(0).tooltip, "again");

    changed.clear();
    marks.clearOwner("plugin:x");
    EXPECT_EQ(changed, (std::vector<std::uint64_t>{1, 2}));
    EXPECT_EQ(marks.shown(a).at(5).size(), 1u);
    EXPECT_TRUE(marks.shown(b).empty());
}

TEST(RowMarks, KeepOnlyForgetsClosedDocumentsAndTheLimitCountsNewLinesOnly)
{
    application::RowMarks marks;
    const application::DocumentId a{1}, b{2};
    std::vector<application::RowMark> many;
    for (std::uint64_t i = 1; i <= application::RowMarks::kMostPerDocument; ++i)
        many.push_back({core::LineId{i}, application::MarkColour::Muted, "m"});
    ASSERT_TRUE(marks.set("plugin:x", "X", a, many));
    EXPECT_TRUE(marks.set("plugin:x", "X", a, {many.front()}));
    EXPECT_FALSE(marks.set("plugin:x", "X", a, {{core::LineId{999999}, application::MarkColour::Muted, "m"}}));
    EXPECT_TRUE(marks.set("plugin:y", "Y", a, {{core::LineId{999999}, application::MarkColour::Muted, "m"}}));
    ASSERT_TRUE(marks.set("plugin:x", "X", b, {many.front()}));
    marks.keepOnly({b});
    EXPECT_TRUE(marks.shown(a).empty());
    EXPECT_EQ(marks.counts("plugin:x"), (std::map<std::uint64_t, std::size_t>{{2, 1}}));
}
