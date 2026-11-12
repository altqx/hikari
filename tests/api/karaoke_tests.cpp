#include "fake_host.h"
#include "script_client.h"

#include "hikari/application/automation.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

std::string song()
{
    std::ifstream in(HIKARI_KARAOKE_FIXTURES "/song.ass", std::ios::binary);
    std::stringstream bytes;
    bytes << in.rdbuf();
    return bytes.str();
}

class FakeTemplater final : public application::KaraokeTemplaterPort {
public:
    bool available() const override { return loaded; }
    void preview(const application::MacroSnapshot &snapshot, PreviewDone done) override
    {
        ++previews;
        if (!error.empty())
            return done({std::nullopt, error});
        application::MacroResult result;
        result.info = snapshot.info;
        result.styles = snapshot.styles;
        result.dialogues = snapshot.dialogues;
        for (const int index : snapshot.selected) {
            const auto first = int(snapshot.info.size() + snapshot.styles.size()) + 1;
            const auto &from = snapshot.dialogues.at(std::size_t(index - first));
            application::MacroDialogueLine generated = from;
            generated.id = 0;
            generated.generatedFrom = from.id;
            generated.layer = 1;
            generated.effect = "fx";
            generated.text = "{\\fad(100,100)}" + from.text;
            result.dialogues.push_back(generated);
        }
        done({std::move(result), {}});
    }
    void cancelPreview() override {}
    bool apply(bool) override { return false; }

    bool loaded = true;
    std::string error;
    int previews = 0;
};

class FakeKaraoke final : public KaraokeHost {
public:
    application::KaraokeCharClass classes() const override { return application::KaraokeCharClass::ascii(); }
    const application::Hyphenation *hyphenation() const override { return nullptr; }
    application::ReadingAnalyzer readings() const override { return {}; }
    void onsets(std::function<void(std::optional<std::vector<int>>)> done) override { done(found); }
    application::KaraokeTemplaterPort *templater() override { return &fakeTemplater; }
    void align(const AlignRequest &request, std::function<void(AlignResult)> done) override
    {
        asked = request;
        done(aligned);
    }

    std::optional<std::vector<int>> found;
    FakeTemplater fakeTemplater;
    std::optional<AlignRequest> asked;
    AlignResult aligned;
};

class Karaoke : public ::testing::Test {
protected:
    Karaoke() { host.sessions.at(1) = std::make_unique<application::EditSession>(loadAss(song())); }

    Host withKaraoke()
    {
        host.karaoke = &karaoke;
        return host.host();
    }

    Result call(std::string_view operation, std::string_view params, Caller caller = aiCaller())
    {
        auto parsed = Json::parse(params);
        EXPECT_TRUE(parsed) << params;
        return client.call(caller, operation, *parsed);
    }

    std::map<std::uint64_t, std::string> lines(std::string_view operation, std::string_view params)
    {
        std::map<std::uint64_t, std::string> out;
        const Result result = call(operation, params);
        EXPECT_TRUE(result) << (result ? "" : result.error().message);
        if (result)
            for (const Json &line : result->find("lines")->asArray())
                out[std::uint64_t(line.find("line")->asInt())] = line.find("text")->asString();
        return out;
    }

    void write(const std::map<std::uint64_t, std::string> &texts)
    {
        Json edits = Json::array();
        for (const auto &[id, text] : texts)
            edits.push(Json::object({{"op", Json("set_line")}, {"line", Json(std::int64_t(id))},
                                     {"fields", Json::object({{"text", Json(text)}})}}));
        const Result applied = client.call(aiCaller(), "document.apply",
                                           Json::object({{"revision", Json(host.target()->revision())},
                                                         {"name", Json("Karaoke")}, {"edits", std::move(edits)}}));
        ASSERT_TRUE(applied) << applied.error().message;
    }

    std::string lineText(std::uint64_t id) const
    {
        for (const core::LineRecord *line : host.target()->document().lines())
            if (line->id.value == id)
                return std::string(line->text.begin(), line->text.end());
        return {};
    }

    FakeKaraoke karaoke;
    FakeHost host;
    Api api{withKaraoke()};
    ScriptClient client{api};
};

TEST_F(Karaoke, AFixtureSongIsSplitSnappedAndTimedThroughTheTools)
{
    const auto split = lines("karaoke.split", R"({"lines": [1, 2], "bySyllable": false})");
    ASSERT_EQ(split.size(), 2u);
    EXPECT_EQ(split.at(1), "{\\k100}Twinkle {\\k100}twinkle");
    EXPECT_EQ(split.at(2), "{\\k120}little {\\k80}star");
    write(split);

    karaoke.found = std::vector<int>{2000, 4250, 9000};
    const Result snapped = call("karaoke.snap", R"({"lines": [1, 2]})");
    ASSERT_TRUE(snapped) << snapped.error().message;
    const auto &moved = snapped->find("lines")->asArray();
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].find("line")->asInt(), 2);
    EXPECT_EQ(moved[0].find("text")->asString(), "{\\k125}little {\\k75}star");
    EXPECT_EQ(moved[0].find("moved")->asInt(), 1);

    const Result timed = call("karaoke.time", R"({"line": 1, "ends": [2000, 3000]})");
    ASSERT_TRUE(timed) << timed.error().message;
    EXPECT_EQ(timed->find("text")->asString(), "{\\k100}Twinkle {\\k100}twinkle");
    write({{1, timed->find("text")->asString()}, {2, moved[0].find("text")->asString()}});

    const Result syllables = call("karaoke.syllables", R"({"line": 2})");
    ASSERT_TRUE(syllables);
    const auto &list = syllables->find("syllables")->asArray();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].find("stripped")->asString(), "little ");
    EXPECT_EQ(list[0].find("start")->asInt(), 3000);
    EXPECT_EQ(list[0].find("end")->asInt(), 4250);
    EXPECT_EQ(list[1].find("end")->asInt(), 5000);

    const Result checked = call("karaoke.check", "{}");
    ASSERT_TRUE(checked);
    EXPECT_TRUE(checked->find("problems")->asArray().empty()) << checked->dump();
}

TEST_F(Karaoke, TimingNeedsOneAscendingEndPerSyllable)
{
    const Result count = call("karaoke.time", R"({"line": 3, "ends": [5500]})");
    ASSERT_FALSE(count);
    EXPECT_EQ(count.error().code, errors::kInvalidParams);
    const Result order = call("karaoke.time", R"({"line": 3, "ends": [5500, 5400, 6000, 7000]})");
    ASSERT_FALSE(order);
    EXPECT_EQ(order.error().code, errors::kInvalidParams);
    const Result whole = call("karaoke.time", R"({"line": 1, "ends": [2500]})");
    ASSERT_TRUE(whole) << "a Line without \\k tags is one syllable";
    EXPECT_EQ(whole->find("text")->asString(), "{\\k150}Twinkle twinkle");
}

TEST_F(Karaoke, SnappingSkipsOnsetsThatLeaveASyllableUnderTenMs)
{
    karaoke.found = std::vector<int>{5995};
    const auto snapped = lines("karaoke.snap", R"({"lines": [3], "window": 600})");
    EXPECT_EQ(snapped.at(3), "{\\k50}ki{\\k49}ra{\\k50}ki{\\k50}ra");
}

TEST_F(Karaoke, OnsetsNeedAudio)
{
    const Result none = call("karaoke.onsets", R"({"start": 0, "end": 10000})");
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error().data.find("reason")->asString(), "no_audio");
    karaoke.found = std::vector<int>{100, 2000, 4000};
    const Result some = call("karaoke.onsets", R"({"start": 1000, "end": 3000})");
    ASSERT_TRUE(some);
    ASSERT_EQ(some->find("onsets")->asArray().size(), 1u);
    EXPECT_EQ(some->find("onsets")->asArray()[0].asInt(), 2000);
}

TEST_F(Karaoke, TheKanjiTimerCopiesTheRomajiSyllables)
{
    const Result result = call("karaoke.kanji", R"({"romaji": 3, "kanji": 4})");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->find("line")->asInt(), 4);
    EXPECT_NE(result->find("text")->asString().find("\\k"), std::string::npos) << result->find("text")->asString();
}

TEST_F(Karaoke, FuriganaNeedsADictionary)
{
    const Result result = call("karaoke.furigana", R"({"line": 4})");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().data.find("reason")->asString(), "no_dictionary");
}

TEST_F(Karaoke, TheTemplatePreviewChangesNothing)
{
    const auto revision = host.target()->revision();
    const Result result = call("karaoke.template_preview", R"({"line": 3})");
    ASSERT_TRUE(result) << result.error().message;
    const auto &generated = result->find("lines")->asArray();
    ASSERT_EQ(generated.size(), 1u);
    EXPECT_EQ(generated[0].find("effect")->asString(), "fx");
    EXPECT_EQ(generated[0].find("text")->asString(), "{\\fad(100,100)}{\\k50}ki{\\k50}ra{\\k50}ki{\\k50}ra");
    EXPECT_EQ(host.target()->revision(), revision);
}

TEST_F(Karaoke, ApplyingTheTemplateIsOneReviewedStep)
{
    Caller reviewed = aiCaller();
    reviewed.reviewed = true;
    const auto lineCount = host.target()->document().lines().size();
    const Result result = call("karaoke.apply_template",
                               R"({"revision": )" + std::to_string(host.target()->revision()) + R"(, "lines": [3]})", reviewed);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_TRUE(result->find("changed")->asBool());
    EXPECT_EQ(host.target()->document().lines().size(), lineCount + 1);
    EXPECT_EQ(host.review.changes(*host.workspace().editingTarget()).size(), 1u);
}

TEST_F(Karaoke, ApplyingTheTemplateRefusesAStaleRevisionOrAFailedRun)
{
    const Result stale = call("karaoke.apply_template", R"({"revision": 999})");
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code, errors::kStaleRevision);
    EXPECT_EQ(karaoke.fakeTemplater.previews, 0);
    karaoke.fakeTemplater.error = "line 3: bad template";
    const Result failed =
        call("karaoke.apply_template", R"({"revision": )" + std::to_string(host.target()->revision()) + "}");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().data.find("reason")->asString(), "templater_failed");
    karaoke.fakeTemplater.loaded = false;
    const Result missing = call("karaoke.template_preview", R"({"line": 3})");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().data.find("reason")->asString(), "no_templater");
}

TEST_F(Karaoke, AlignSendsTheLinesWordsAndSpan)
{
    karaoke.aligned = {"aligned", {}, {{"Twinkle", 1100, 1800}, {"twinkle", 1900, 2900}}};
    const Result result = call("karaoke.align", R"({"lines": [1, 2], "program": "whisperx", "language": "en"})");
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_TRUE(karaoke.asked);
    EXPECT_EQ(karaoke.asked->startMs, 1000);
    EXPECT_EQ(karaoke.asked->endMs, 5000);
    EXPECT_EQ(karaoke.asked->text, "Twinkle twinkle\nlittle star");
    EXPECT_EQ(karaoke.asked->language, "en");
    ASSERT_EQ(result->find("words")->asArray().size(), 2u);
    EXPECT_EQ(result->find("words")->asArray()[1].find("start")->asInt(), 1900);
}

TEST_F(Karaoke, AlignNamesAMissingProgramAndChecksMfasModels)
{
    const Result mfa = call("karaoke.align", R"({"lines": [1], "program": "mfa"})");
    ASSERT_FALSE(mfa);
    EXPECT_EQ(mfa.error().code, errors::kInvalidParams);
    const Result flag = call("karaoke.align", R"({"lines": [1], "program": "whisperx", "language": "--model"})");
    ASSERT_FALSE(flag);
    EXPECT_EQ(flag.error().code, errors::kInvalidParams);
    karaoke.aligned = {"missing", "WhisperX is not installed.", {}};
    const Result missing = call("karaoke.align", R"({"lines": [1], "program": "whisperx"})");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, errors::kUnavailable);
    EXPECT_EQ(missing.error().data.find("reason")->asString(), "missing");
}

TEST_F(Karaoke, WithoutTheAppTheToolsThatNeedItAreUnavailable)
{
    host.karaoke = nullptr;
    Api bare{host.host()};
    ScriptClient bareClient{bare};
    const Result result = bareClient.call(aiCaller(), "karaoke.onsets", *Json::parse(R"({"start": 0, "end": 1})"));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kUnavailable);
    EXPECT_TRUE(bareClient.call(aiCaller(), "karaoke.split", *Json::parse(R"({"lines": [1]})")));
}

}
