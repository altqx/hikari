#include "hikari/application/motion_data.h"

#include "canonical_tags.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/extradata.h"
#include "hikari/json/json.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app = hikari::application;
namespace core = hikari::core;
using hikari::json::Json;

namespace {

const std::filesystem::path kCorpus = HIKARI_MOTION_CORPUS;

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string narrow(std::u8string_view text)
{
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

std::u8string widen(std::string_view text)
{
    return {reinterpret_cast<const char8_t *>(text.data()), text.size()};
}

Json sorted(const Json &value)
{
    if (value.isArray()) {
        Json::Array out;
        for (const auto &v : value.asArray())
            out.push_back(sorted(v));
        return out;
    }
    if (!value.isObject())
        return value;
    Json::Object out;
    for (const auto &m : value.asObject())
        out.push_back({m.key, sorted(m.value)});
    std::ranges::sort(out, {}, &Json::Member::key);
    return out;
}

app::AegisubFrameTimes corpusTimes()
{
    std::vector<std::int64_t> starts;
    for (std::int64_t i = 0; i < 2398; ++i)
        starts.push_back(i * 1001 / 24);
    return *app::AegisubFrameTimes::fromFrameStarts(starts);
}

app::EditSession corpusSession()
{
    const std::string data = readFile(kCorpus / "motion.ass");
    std::vector<std::byte> bytes(data.size());
    std::memcpy(bytes.data(), data.data(), bytes.size());
    return app::EditSession(core::loadAss(bytes).document);
}

Json extraJson(const core::LineRecord &line)
{
    Json::Object out;
    for (const auto &e : core::extradataByKey(line.extra)) {
        Json value = narrow(e.value);
        if (e.key == u8"a-mo")
            if (auto parsed = Json::parse(narrow(e.value)); parsed && parsed->isObject()) {
                value = *parsed;
                if (value.find("uuid"))
                    value["uuid"] = "<uuid>";
            }
        out.push_back({narrow(e.key), value});
    }
    return out;
}

Json eventsOf(const core::Document &doc)
{
    Json::Array out;
    for (const auto *l : doc.lines()) {
        Json e = Json::object({{"end", static_cast<std::int64_t>(l->end.value.microseconds() / 1000)},
                               {"layer", static_cast<std::int64_t>(l->layer.value)},
                               {"start", static_cast<std::int64_t>(l->start.value.microseconds() / 1000)},
                               {"style", narrow(l->style)},
                               {"text", hikari::testing::canonicalTags(narrow(l->text))}});
        if (l->comment)
            e["comment"] = true;
        if (!l->extra.empty())
            e["extra"] = extraJson(*l);
        out.push_back(sorted(e));
    }
    return out;
}

Json changes(const Json &before, const Json &after)
{
    Json::Array changed;
    std::int64_t kept = 0;
    for (const auto &e : after.asArray())
        std::ranges::contains(before.asArray(), e) ? void(++kept) : changed.push_back(e);
    return Json::object({{"events", changed}, {"outcome", "ok"}, {"unchanged", kept}});
}

struct Dialog {
    const Json *values = nullptr;
    bool flag(std::string_view name, bool fallback) const
    {
        const Json *v = values ? values->find(name) : nullptr;
        return v && v->isBool() ? v->asBool() : fallback;
    }
    double num(std::string_view name, double fallback) const
    {
        const Json *v = values ? values->find(name) : nullptr;
        return v && v->isNumber() ? v->asNumber() : fallback;
    }
    std::string text(std::string_view name) const
    {
        const Json *v = values ? values->find(name) : nullptr;
        if (!v || !v->isString())
            return {};
        const std::string s = v->asString();
        return s.starts_with('@') ? readFile(kCorpus / s.substr(1)) : s;
    }
};

int relativeFrame(const Dialog &d, int frames, std::int64_t firstFrame)
{
    const int value = static_cast<int>(d.num("startFrame", 5));
    if (!d.flag("relative", true))
        return value - static_cast<int>(firstFrame) + 1;
    return value == 0 ? 1 : value < 0 ? frames + value + 1 : value;
}

app::MotionFollow followOf(const Dialog &d, int frames, std::int64_t firstFrame)
{
    app::MotionFollow f;
    f.relativeFrame = relativeFrame(d, frames, firstFrame);
    f.x = d.flag("xPosition", true);
    f.y = d.flag("yPosition", true);
    f.scale = d.flag("xScale", true);
    f.rotation = d.flag("zRotation", false);
    f.rectClips = d.flag("rectClip", true);
    f.vectorClips = d.flag("vectClip", true);
    f.rectToVector = d.flag("rcToVc", false);
    return f;
}

void selectRows(app::EditSession &session, const Json &rows)
{
    app::Selection selection;
    int row = 0;
    for (const auto *l : session.document().lines()) {
        ++row;
        for (const auto &r : rows.asArray())
            if (r.asInt() == row)
                selection.selected.insert(l->id);
    }
    selection.active = *selection.selected.begin();
    session.setSelection(selection);
}

app::EditSession sessionWith(std::string_view events)
{
    std::string data = readFile(kCorpus / "motion.ass");
    const auto format = data.find("Format: Layer");
    data = data.substr(0, data.find('\n', format) + 1) + std::string(events);
    std::vector<std::byte> bytes(data.size());
    std::memcpy(bytes.data(), data.data(), bytes.size());
    return app::EditSession(core::loadAss(bytes).document);
}

void selectAll(app::EditSession &session)
{
    app::Selection selection;
    for (const auto *l : session.document().lines())
        selection.selected.insert(l->id);
    selection.active = *selection.selected.begin();
    session.setSelection(selection);
}

std::vector<std::string> textsOf(const core::Document &doc)
{
    std::vector<std::string> out;
    for (const auto *l : doc.lines())
        out.push_back(narrow(l->text));
    return out;
}

std::string lastStep(const app::EditSession &session)
{
    return session.history().back().name;
}

constexpr std::string_view kSign = "Dialogue: 0,0:00:04.17,0:00:04.30,Flat,,0,0,0,,{\\pos(100,50)\\fscx50\\fscy50}A\n";

app::MotionTrack trackOf(int frames, double dx, double scaleStep, double turnStep)
{
    app::MotionTrack track;
    track.fps = 23.976;
    track.width = 1280;
    track.height = 720;
    for (int i = 0; i < frames; ++i) {
        track.x.push_back(100 + dx * i);
        track.y.push_back(50 + 5 * i);
        track.scaleX.push_back(100 + scaleStep * i);
        track.scaleY.push_back(100 + scaleStep * i);
        track.rotation.push_back(-turnStep * i);
    }
    return track;
}

app::MotionOptions followAll()
{
    app::MotionOptions options;
    options.main.rotation = true;
    return options;
}

int signFrames(const app::AegisubFrameTimes &times)
{
    app::EditSession session = sessionWith(kSign);
    selectAll(session);
    return app::checkMotion(session, times, 0).frames;
}

app::EditSession trackedSign(const app::AegisubFrameTimes &times, const app::MotionTrack &track)
{
    app::EditSession session = sessionWith(kSign);
    selectAll(session);
    EXPECT_TRUE(app::applyMotion(session, times, track, followAll()));
    return session;
}

bool hasTag(const std::string &text, std::string_view tag)
{
    return text.find(tag) != std::string::npos;
}

Json runNative(const Json &spec, app::EditSession &session, const app::AegisubFrameTimes &times)
{
    selectRows(session, *spec.find("selection"));
    const auto &dialogs = spec.find("dialogs")->asArray();
    Dialog main{dialogs.front().find("values")};
    std::optional<Dialog> clip;
    if (dialogs.size() > 1)
        clip = Dialog{dialogs[1].find("values")};
    const auto track = app::parseMotionData(widen(main.text("data")));
    if (!track)
        return Json::object({{"outcome", "stopped"}});
    const app::MotionCheck check = app::checkMotion(session, times, track->frames());
    if (check.problem)
        return Json::object({{"outcome", "stopped"}});
    app::MotionOptions options;
    options.main = followOf(main, check.frames, check.firstFrame);
    options.absolute = main.flag("absPos", false);
    options.origin = main.flag("origin", false);
    options.border = main.flag("border", true);
    options.shadow = main.flag("shadow", true);
    options.blur = main.flag("blur", true);
    options.blurScale = main.num("blurScale", 1);
    options.clipsOnly = main.flag("clipOnly", false);
    options.bakeTransforms = main.flag("killTrans", true);
    options.linear = main.flag("linear", false);
    if (clip) {
        options.clipTrack = app::parseMotionData(widen(clip->text("data")));
        options.clip = followOf(*clip, check.frames, check.firstFrame);
    }
    const Json before = eventsOf(session.document());
    const auto steps = session.historySize();
    const auto applied = app::applyMotion(session, times, *track, options);
    if (!applied)
        return Json::object({{"outcome", "stopped"}});
    Json record = changes(before, eventsOf(session.document()));
    record["undo_steps"] = static_cast<std::int64_t>(session.historySize() - steps);
    if (const Json *then = spec.find("then")) {
        app::Selection tracked;
        for (const auto *l : session.document().lines())
            if (core::extradataValue(l->extra, u8"a-mo"))
                tracked.selected.insert(l->id);
        tracked.active = *tracked.selected.begin();
        session.setSelection(tracked);
        const Json mid = eventsOf(session.document());
        const auto revertSteps = session.historySize();
        EXPECT_TRUE(app::revertMotion(session));
        Json reverted = changes(mid, eventsOf(session.document()));
        reverted["undo_steps"] = static_cast<std::int64_t>(session.historySize() - revertSteps);
        return Json::object({{"apply", record}, {"then", reverted}});
    }
    return Json::object({{"apply", record}});
}

Json comparable(const Json &record)
{
    if (!record.isObject())
        return record;
    Json::Object out;
    for (const auto &m : record.asObject()) {
        if (m.key == "apply" || m.key == "then") {
            out.push_back({m.key, comparable(m.value)});
            continue;
        }
        if (m.key == "events" || m.key == "outcome" || m.key == "unchanged" || m.key == "undo_steps")
            out.push_back(m);
    }
    return sorted(out);
}

Json normalised(const Json &native)
{
    return native.find("apply") ? comparable(native) : comparable(Json::object({{"apply", native}}));
}

}

TEST(MotionData, NativeMatchesAegisubMotionOrItsLedger)
{
    const auto corpus = Json::parse(readFile(kCorpus / "cases.json"));
    ASSERT_TRUE(corpus);
    const bool update = std::getenv("HIKARI_UPDATE_MOTION_GOLDENS") != nullptr;
    const auto times = corpusTimes();
    std::filesystem::create_directories(kCorpus / "native");
    for (const auto &spec : corpus->find("cases")->asArray()) {
        const std::string name = spec.find("name")->asString();
        app::EditSession session = corpusSession();
        const Json native = normalised(runNative(spec, session, times));
        const auto amo = Json::parse(readFile(kCorpus / "amo" / (name + ".json")));
        ASSERT_TRUE(amo) << name;
        const Json *departure = spec.find("departure");
        if (const Json *variants = amo->find("variants"); variants && !departure) {
            bool matched = false;
            for (const auto &v : variants->asArray())
                matched = matched || comparable(v) == native;
            EXPECT_TRUE(matched) << name << ": native output is none of a-mo's variants\n" << native.dump(2);
        }
        if (!departure && !amo->find("variants")) {
            EXPECT_EQ(comparable(*amo).dump(2), native.dump(2)) << name;
            continue;
        }
        const auto path = kCorpus / "native" / (name + ".json");
        Json pinned = native;
        if (departure)
            pinned["departure"] = departure->asString();
        if (update) {
            std::ofstream(path, std::ios::binary) << pinned.dump(2) << "\n";
            continue;
        }
        EXPECT_EQ(readFile(path), pinned.dump(2) + "\n") << name;
    }
}

TEST(MotionData, WrittenDataParsesBackFrameForFrame)
{
    const app::MotionTrack track = trackOf(3, 12.5, 25, 7.25);
    const std::u8string text = app::writeMotionData(track);
    EXPECT_TRUE(narrow(text).starts_with("Adobe After Effects 6.0 Keyframe Data"));
    EXPECT_TRUE(narrow(text).ends_with("End of Keyframe Data\n"));
    const auto parsed = app::parseMotionData(text);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->x, track.x);
    EXPECT_EQ(parsed->y, track.y);
    EXPECT_EQ(parsed->scaleX, track.scaleX);
    EXPECT_EQ(parsed->scaleY, track.scaleY);
    EXPECT_EQ(parsed->rotation, track.rotation);
    EXPECT_DOUBLE_EQ(parsed->fps, 23.976);
    EXPECT_EQ(parsed->width, 1280);
    EXPECT_EQ(parsed->height, 720);
}

TEST(MotionData, ParseRefusesDataWithoutEveryEqualSection)
{
    const std::string text = narrow(app::writeMotionData(trackOf(3, 1, 0, 0)));
    ASSERT_TRUE(app::parseMotionData(widen(text)));
    EXPECT_FALSE(app::parseMotionData(widen(text.substr(text.find('\n')))));
    EXPECT_FALSE(app::parseMotionData(widen(text.substr(0, text.find("Rotation")) + "End of Keyframe Data\n")));
    std::string uneven = text;
    const auto row = uneven.find("\n\t2\t");
    uneven.erase(row, uneven.find('\n', row + 1) - row);
    EXPECT_FALSE(app::parseMotionData(widen(uneven)));
    EXPECT_FALSE(app::parseMotionData(u8""));
    EXPECT_TRUE(app::parseMotionData(widen(readFile(kCorpus / "static.txt"))));
}

TEST(MotionData, DerivedDataAppliedAgainGivesTheSameLines)
{
    const auto times = corpusTimes();
    const int frames = signFrames(times);
    ASSERT_GT(frames, 2);
    const app::MotionTrack track = trackOf(frames, 10, 100, 10);
    app::EditSession tracked = trackedSign(times, track);
    ASSERT_EQ(tracked.document().lines().size(), static_cast<std::size_t>(frames));
    selectAll(tracked);
    const auto derived = app::deriveMotion(tracked, times);
    ASSERT_TRUE(derived);
    EXPECT_EQ(derived->x, track.x);
    EXPECT_EQ(derived->y, track.y);
    EXPECT_EQ(derived->rotation, track.rotation);
    EXPECT_EQ(derived->scaleX.front(), 50);
    EXPECT_EQ(derived->scaleX.back(), 50 * frames);
    EXPECT_EQ(derived->width, 1280);
    EXPECT_EQ(derived->height, 720);
    EXPECT_DOUBLE_EQ(derived->fps, times.fps());

    app::EditSession again = trackedSign(times, *derived);
    EXPECT_EQ(textsOf(again.document()), textsOf(tracked.document()));
}

TEST(MotionData, DeriveRefusesLinesThatAreNotOneSignFrameByFrame)
{
    const auto times = corpusTimes();
    app::EditSession one = sessionWith(kSign);
    selectAll(one);
    EXPECT_EQ(app::deriveMotion(one, times).error().problem, app::DeriveProblem::TooFewLines);

    app::EditSession tracked = trackedSign(times, trackOf(signFrames(times), 10, 0, 0));
    selectAll(tracked);
    const core::LineId second = tracked.document().lines()[1]->id;
    ASSERT_TRUE(tracked.run(app::Command{"Edit", tracked.revision(), {second}, [&](core::Document &d) {
        return d.editLine(second, [](core::LineRecord &l) { l.text += u8"!"; });
    }}));
    selectAll(tracked);
    const auto changed = app::deriveMotion(tracked, times);
    ASSERT_FALSE(changed);
    EXPECT_EQ(changed.error().problem, app::DeriveProblem::NotFrameByFrame);
    EXPECT_EQ(changed.error().row, 2);

    app::EditSession unplaced = sessionWith("Dialogue: 0,0:00:04.17,0:00:04.21,Flat,,0,0,0,,A\n"
                                            "Dialogue: 0,0:00:04.21,0:00:04.25,Flat,,0,0,0,,A\n");
    selectAll(unplaced);
    const auto noPos = app::deriveMotion(unplaced, times);
    ASSERT_FALSE(noPos);
    EXPECT_EQ(noPos.error().problem, app::DeriveProblem::NoPosition);
    EXPECT_EQ(noPos.error().row, 1);
}

TEST(MotionData, RevertFromOneTrackedLineRestoresTheOriginalOverAllOfThemInOneStep)
{
    const auto times = corpusTimes();
    const app::EditSession original = sessionWith(kSign);
    const core::LineRecord &source = *original.document().lines().front();
    app::EditSession session = trackedSign(times, trackOf(signFrames(times), 10, 0, 0));
    EXPECT_EQ(lastStep(session), "Apply motion data");
    const core::LineId middle = session.document().lines()[1]->id;
    const auto firstStart = session.document().lines().front()->start.value;
    const auto lastEnd = session.document().lines().back()->end.value;
    app::Selection one;
    one.selected = {middle};
    one.active = middle;
    session.setSelection(one);
    const auto steps = session.historySize();
    ASSERT_TRUE(app::revertMotion(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(lastStep(session), "Revert motion data");
    ASSERT_EQ(session.document().lines().size(), 1u);
    const core::LineRecord &back = *session.document().lines().front();
    EXPECT_EQ(back.text, source.text);
    EXPECT_EQ(back.start.value, firstStart);
    EXPECT_EQ(back.end.value, lastEnd);
    EXPECT_TRUE(back.extra.empty());
    EXPECT_EQ(session.selection().active, back.id);
}

TEST(MotionData, ExtrapolateMoveCarriesItsSpeedToTheWholeLine)
{
    EXPECT_EQ(app::extrapolateMove(u8"{\\move(100,200,300,400,100,300)}A", 500),
              u8"{\\move(0.00,100.00,500.00,600.00)}A");
    EXPECT_EQ(app::extrapolateMove(u8"{\\move(0,0,10,0,0,30)\\blur1}A", 45), u8"{\\move(0.00,0.00,15.00,0.00)\\blur1}A");
    EXPECT_FALSE(app::extrapolateMove(u8"{\\move(100,200,300,400)}A", 500));
    EXPECT_FALSE(app::extrapolateMove(u8"{\\move(100,200,300,400,50,50)}A", 500));
    EXPECT_FALSE(app::extrapolateMove(u8"{\\pos(100,200)}A", 500));

    app::EditSession session = sessionWith("Dialogue: 0,0:00:01.00,0:00:01.50,Flat,,0,0,0,,{\\move(100,200,300,400,100,300)}A\n"
                                           "Dialogue: 0,0:00:01.00,0:00:01.50,Flat,,0,0,0,,{\\pos(1,2)}B\n");
    selectAll(session);
    const auto steps = session.historySize();
    EXPECT_EQ(app::extrapolateMoves(session), 1);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(lastStep(session), "Extrapolate \\move");
    EXPECT_EQ(textsOf(session.document()),
              (std::vector<std::string>{"{\\move(0.00,100.00,500.00,600.00)}A", "{\\pos(1,2)}B"}));

    app::EditSession still = sessionWith("Dialogue: 0,0:00:01.00,0:00:01.50,Flat,,0,0,0,,{\\pos(1,2)}B\n");
    selectAll(still);
    EXPECT_FALSE(app::extrapolateMoves(still));
}

TEST(MotionFit, ChoosesTheLowestDegreeThatFits)
{
    std::vector<double> line, square, cube;
    for (int x = 1; x <= 12; ++x) {
        line.push_back(2 * x + 1);
        square.push_back(x * x - 3 * x);
        cube.push_back(0.5 * x * x * x - x);
    }
    const app::PolynomialFit a = app::fitPolynomial(line);
    EXPECT_EQ(a.degree(), 1);
    EXPECT_NEAR(a.at(13), 27, 1e-9);
    EXPECT_EQ(app::fitPolynomial(square).degree(), 2);
    const app::PolynomialFit c = app::fitPolynomial(cube);
    EXPECT_EQ(c.degree(), 3);
    EXPECT_NEAR(c.at(13), 0.5 * 13 * 13 * 13 - 13, 1e-6);
    EXPECT_LT(c.rmse, 0.01);
    EXPECT_EQ(a.confidence(), app::FitConfidence::Good);
}

TEST(MotionFit, ConfidenceFallsPastTheRmseThresholds)
{
    EXPECT_EQ((app::PolynomialFit{{0}, 0.2}).confidence(), app::FitConfidence::Good);
    EXPECT_EQ((app::PolynomialFit{{0}, 0.21}).confidence(), app::FitConfidence::Low);
    EXPECT_EQ((app::PolynomialFit{{0}, 5}).confidence(), app::FitConfidence::Low);
    EXPECT_EQ((app::PolynomialFit{{0}, 5.01}).confidence(), app::FitConfidence::VeryLow);
}

TEST(MotionFit, ExtendsFromTheGivenEnd)
{
    std::vector<double> values;
    for (int x = 1; x <= 10; ++x)
        values.push_back(x);
    const app::Extension start = app::extendValues(values, 3, app::ExtendAt::Start);
    ASSERT_EQ(start.values.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_NEAR(start.values[i], static_cast<double>(i) - 2, 1e-9);
    const app::Extension end = app::extendValues(values, 3, app::ExtendAt::End);
    ASSERT_EQ(end.values.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_NEAR(end.values[i], 10 + static_cast<double>(i), 1e-9);
}

TEST(MotionFit, HalvesTowardsTheExtendedEndUntilItFits)
{
    std::vector<double> values;
    for (int x = 1; x <= 16; ++x)
        values.push_back(x <= 8 ? (x % 2 ? 50 : -50) : x);
    const app::Extension end = app::extendValues(values, 2, app::ExtendAt::End);
    EXPECT_LT(end.fit.rmse, 0.003);
    ASSERT_EQ(end.values.size(), 3u);
    EXPECT_NEAR(end.values[1], 17, 1e-9);
    EXPECT_NEAR(end.values[2], 18, 1e-9);

    std::ranges::reverse(values);
    const app::Extension start = app::extendValues(values, 1, app::ExtendAt::Start);
    EXPECT_LT(start.fit.rmse, 0.003);
    ASSERT_EQ(start.values.size(), 2u);
    EXPECT_NEAR(start.values[0], 17, 1e-9);
    EXPECT_NEAR(start.values[1], 16, 1e-9);
}

TEST(MotionData, ExtendTrackFitsEverySeriesOnBothEnds)
{
    const app::ExtendedTrack out = app::extendTrack(trackOf(10, 10, 0, 2), 2, 3);
    EXPECT_EQ(out.track.frames(), 15);
    EXPECT_EQ(out.track.rotation.size(), 15u);
    EXPECT_NEAR(out.track.x.front(), 80, 1e-9);
    EXPECT_NEAR(out.track.x[2], 100, 1e-9);
    EXPECT_NEAR(out.track.x.back(), 220, 1e-9);
    EXPECT_NEAR(out.track.y.back(), 110, 1e-9);
    EXPECT_NEAR(out.track.scaleX.back(), 100, 1e-9);
    EXPECT_NEAR(out.track.rotation.front(), 4, 1e-9);
    EXPECT_EQ(out.confidence, app::FitConfidence::Good);
}

TEST(MotionData, ExtendTrackedLinesGrowsTheEndLineAndTracksIt)
{
    const auto times = corpusTimes();
    const int frames = signFrames(times);
    for (const auto at : {app::ExtendAt::End, app::ExtendAt::Start}) {
        app::EditSession session = trackedSign(times, trackOf(frames, 10, 0, 0));
        selectAll(session);
        const auto steps = session.historySize();
        const auto confidence = app::extendTrackedLines(session, times, 2, at, false);
        ASSERT_TRUE(confidence);
        EXPECT_EQ(*confidence, app::FitConfidence::Good);
        EXPECT_EQ(session.historySize(), steps + 1);
        EXPECT_EQ(lastStep(session), "Extend tracked lines");
        const auto texts = textsOf(session.document());
        ASSERT_EQ(texts.size(), static_cast<std::size_t>(frames + 2));
        if (at == app::ExtendAt::End) {
            EXPECT_TRUE(hasTag(texts[frames], "\\pos(" + std::to_string(100 + 10 * frames) + ","));
            EXPECT_TRUE(hasTag(texts.back(), "\\pos(" + std::to_string(110 + 10 * frames) + ","));
        } else {
            EXPECT_TRUE(hasTag(texts[0], "\\pos(80,40)"));
            EXPECT_TRUE(hasTag(texts[1], "\\pos(90,45)"));
            EXPECT_TRUE(hasTag(texts[2], "\\pos(100,50)"));
        }
        selectAll(session);
        EXPECT_TRUE(app::deriveMotion(session, times));
    }
}

TEST(MotionData, ExtendTrackedLinesRefusesWhatItCannotFit)
{
    const auto times = corpusTimes();
    app::EditSession one = sessionWith(kSign);
    selectAll(one);
    EXPECT_EQ(app::extendTrackedLines(one, times, 2, app::ExtendAt::End, false).error().problem,
              app::ExtendProblem::TooFewLines);

    app::EditSession early = sessionWith("Dialogue: 0,0:00:00.04,0:00:00.08,Flat,,0,0,0,,{\\pos(1,2)}A\n"
                                         "Dialogue: 0,0:00:00.08,0:00:00.12,Flat,,0,0,0,,{\\pos(2,2)}A\n");
    selectAll(early);
    const auto before = app::extendTrackedLines(early, times, 5, app::ExtendAt::Start, false);
    ASSERT_FALSE(before);
    EXPECT_EQ(before.error().problem, app::ExtendProblem::BeforeVideo);

    app::EditSession moving = trackedSign(times, trackOf(signFrames(times), 10, 0, 0));
    const core::LineId first = moving.document().lines().front()->id;
    ASSERT_TRUE(moving.run(app::Command{"Edit", moving.revision(), {first}, [&](core::Document &d) {
        return d.editLine(first, [](core::LineRecord &l) { l.text = u8"{\\pos(1,2)\\t(\\blur3)}A"; });
    }}));
    selectAll(moving);
    const auto animated = app::extendTrackedLines(moving, times, 2, app::ExtendAt::End, false);
    ASSERT_FALSE(animated);
    EXPECT_EQ(animated.error().problem, app::ExtendProblem::Animated);
    EXPECT_EQ(animated.error().row, 1);
}
