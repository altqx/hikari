#include "hikari/backends/ai_inline_run.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_translation_run.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::backends::ai;
namespace app = hikari::application::ai;
namespace in = hikari::application::ai_inline;
using hikari::api::Json;

namespace {

constexpr const char *kBuildId = "0.1.0+test";

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        if (done())
            return true;
        if (t.elapsed() >= ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

struct MemoryConfigs final : app::ConfigStore {
    app::Config stored;
    app::Config load() override { return stored; }
    bool save(const app::Config &config) override
    {
        stored = config;
        return true;
    }
};

struct Keys final : app::KeyStore {
    void read(const std::string &, std::function<void(app::KeyResult)> done) override { done(std::string("sk-test")); }
    void write(const std::string &, const std::string &, std::function<void(std::optional<app::KeyError>)> done) override
    {
        done(std::nullopt);
    }
    void remove(const std::string &, std::function<void(std::optional<app::KeyError>)> done) override
    {
        done(std::nullopt);
    }
};

std::string s(std::u8string_view v)
{
    return {reinterpret_cast<const char *>(v.data()), v.size()};
}

class InlineRunTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_inline_run_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication qapp(argc, argv);
    }

    void use(const std::string &model, std::optional<app::Price> price = app::Price{1.0, 0.0})
    {
        app::Provider p = app::presetProvider(app::Preset::OpenRouter, "or", "OpenRouter");
        p.models = {{.id = model, .name = model, .price = price}};
        configs.stored.providers = {p};
        configs.stored.roles[app::Role::Chat] = {"or", model};
        providers = std::make_unique<app::Providers>(configs, keys);
        requests = std::make_unique<AiRequests>(*providers, [this] { return sidecar(); });
    }

    AiSidecarHost *sidecar()
    {
        if (!host) {
            AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {QStringLiteral("--build-id"), QString::fromLatin1(kBuildId), QStringLiteral("--api-version"),
                           QString::number(hikari::api::kApiVersion)};
            o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
            o.buildId = kBuildId;
            o.shutdownMs = 600;
            host = std::make_unique<AiSidecarHost>(std::move(o), nullptr, hikari::api::Caller{"ai", "AI"});
        }
        return host.get();
    }

    std::vector<InlineRun::Job> jobs(std::initializer_list<std::u8string> texts)
    {
        std::vector<InlineRun::Job> out;
        std::uint64_t key = 1;
        for (const std::u8string &text : texts) {
            core::LineRecord line;
            line.text = text;
            line.end.value = core::DocumentTime(2'000'000);
            out.push_back({key++, in::prepare({in::Action::Rewrite, "louder"}, line, {})});
        }
        return out;
    }

    InlineRun::Wiring wiring()
    {
        InlineRun::Wiring w;
        w.requests = [this] { return requests.get(); };
        w.config = [this] { return &providers->config(); };
        w.spend = [this](double d) { spent += d; };
        w.overDailyCap = [this] { return cap > 0 && spent >= cap; };
        w.lineDone = [this](const InlineRun::Result &r) { results.push_back(r); };
        w.finished = [this](InlineRun::End e) { ended = e; };
        return w;
    }

    QTemporaryDir dir;
    MemoryConfigs configs;
    Keys keys;
    std::unique_ptr<app::Providers> providers;
    std::unique_ptr<AiRequests> requests;
    std::unique_ptr<AiSidecarHost> host;
    std::vector<InlineRun::Result> results;
    std::optional<InlineRun::End> ended;
    double spent = 0;
    double cap = 0;
};

}

TEST_F(InlineRunTest, EachLineIsItsOwnRequestAndKeepsItsTags)
{
    use("inline-upper");
    InlineRun run(app::Role::Chat, jobs({u8"{\\i1}one{\\i0} a", u8"two", u8"three", u8"four", u8"five"}), wiring());
    run.start();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(*ended, InlineRun::End::Done);
    ASSERT_EQ(results.size(), 5u);
    std::map<std::uint64_t, std::string> texts;
    for (const InlineRun::Result &r : results) {
        EXPECT_FALSE(r.error);
        EXPECT_EQ(r.answer.outcome, in::Outcome::Changed);
        texts[r.key] = s(r.answer.text);
    }
    EXPECT_EQ(texts[1], "{\\i1}ONE{\\i0} A");
    EXPECT_EQ(texts[5], "FIVE");
    EXPECT_EQ(run.done(), 5u);
    EXPECT_DOUBLE_EQ(run.cost(), 5.0);
    EXPECT_DOUBLE_EQ(spent, 5.0);
    EXPECT_TRUE(run.costKnown());
}

TEST_F(InlineRunTest, AnAnswerThatDropsATagIsntUsed)
{
    use("inline-drop");
    InlineRun run(app::Role::Chat, jobs({u8"{\\i1}one{\\i0}"}), wiring());
    run.start();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].answer.outcome, in::Outcome::DroppedTag);
}

TEST_F(InlineRunTest, AFailedRequestFailsOnlyItsLine)
{
    use("fail");
    InlineRun run(app::Role::Chat, jobs({u8"one", u8"two"}), wiring());
    run.start();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(*ended, InlineRun::End::Done);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].error->kind, app::ErrorKind::RateLimited);
}

TEST_F(InlineRunTest, StopCancelsTheRunningRequestsAndSendsNoMore)
{
    use("hang");
    InlineRun run(app::Role::Chat, jobs({u8"1", u8"2", u8"3", u8"4", u8"5", u8"6"}), wiring());
    run.start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    run.stop();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(*ended, InlineRun::End::Stopped);
    EXPECT_TRUE(results.empty());
    EXPECT_FALSE(run.running());
}

TEST_F(InlineRunTest, TheDailyCapStopsTheRun)
{
    use("inline-upper");
    cap = 2.0;
    InlineRun::Wiring w = wiring();
    w.parallel = 1;
    InlineRun run(app::Role::Chat, jobs({u8"1", u8"2", u8"3", u8"4"}), std::move(w));
    run.start();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(*ended, InlineRun::End::DailyCap);
    EXPECT_EQ(results.size(), 2u);
}

TEST_F(InlineRunTest, ADroppedLineIsNotSent)
{
    use("inline-upper");
    InlineRun::Wiring w = wiring();
    w.parallel = 1;
    InlineRun run(app::Role::Chat, jobs({u8"one", u8"two", u8"three"}), std::move(w));
    run.drop(2);
    run.start();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(run.total(), 2u);
    for (const InlineRun::Result &r : results)
        EXPECT_NE(r.key, 2u);
}

TEST_F(InlineRunTest, WithoutAModelNothingIsSent)
{
    use("inline-upper");
    configs.stored.roles.clear();
    requests.reset();
    providers = std::make_unique<app::Providers>(configs, keys);
    requests = std::make_unique<AiRequests>(*providers, [this] { return sidecar(); });
    InlineRun run(app::Role::Translator, jobs({u8"one"}), wiring());
    run.start();
    EXPECT_EQ(ended, InlineRun::End::NoModel);
    EXPECT_TRUE(results.empty());
}

TEST_F(InlineRunTest, AModelWithoutAPriceMarksTheCostUnknown)
{
    use("inline-upper", std::nullopt);
    InlineRun run(app::Role::Chat, jobs({u8"one"}), wiring());
    run.start();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_FALSE(run.costKnown());
    EXPECT_DOUBLE_EQ(spent, 0.0);
}

TEST(InlineRequest, CarriesNoToolsAndOnlyThePreparedText)
{
    core::LineRecord line;
    line.text = u8"Hi";
    const in::Prepared p = in::prepare({in::Action::Spelling}, line, {});
    const Json r = inlineRequest(p);
    EXPECT_EQ(r.find("tools"), nullptr);
    EXPECT_EQ(r.find("webSearch"), nullptr);
    EXPECT_EQ(r.find("system")->asString(), p.system);
    EXPECT_EQ(r.find("messages")->asArray().size(), 1u);
}

namespace {

std::vector<TranslationRun::Job> translationJobs(std::initializer_list<std::u8string> texts)
{
    std::vector<TranslationRun::Job> out;
    std::uint64_t key = 1;
    for (const std::u8string &text : texts) {
        core::LineRecord line;
        line.text = text;
        out.push_back({key, in::prepare({in::Action::Translate, {}, "English"}, line, {}), "Yui",
                       static_cast<std::int64_t>(key) * 1000, static_cast<std::int64_t>(key) * 1000 + 900});
        ++key;
    }
    return out;
}

}

TEST_F(InlineRunTest, ThePipelineSendsEveryLineMaskedInOneRequestAndTagsComeBack)
{
    use("t");
    TranslationRun::Wiring w;
    w.lines = wiring();
    w.document = 4;
    w.language = "English";
    w.translator = {"or", "t"};
    w.reviewer = {"or", "t"};
    std::vector<TranslationRun::ProjectFile> files;
    std::size_t progressed = 0;
    w.progressed = [&](std::size_t done, std::size_t) { progressed = done; };
    w.projectChanged = [&](std::vector<TranslationRun::ProjectFile> f) { files = std::move(f); };
    const Json params = translateParams(translationJobs({u8"{\\i1}one{\\i0} a", u8"two?"}), w);
    EXPECT_EQ(params.find("lines")->asArray()[0].find("text")->asString(), "\xE2\x9F\xA6" "1\xE2\x9F\xA7one\xE2\x9F\xA6" "2\xE2\x9F\xA7 a");
    EXPECT_EQ(params.find("lines")->asArray()[0].find("speaker")->asString(), "Yui");
    EXPECT_EQ(params.find("document")->asInt(), 4);
    EXPECT_EQ(params.find("sourceLanguage"), nullptr);

    TranslationRun run(translationJobs({u8"{\\i1}one{\\i0} a", u8"two?"}), std::move(w));
    run.start();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(*ended, InlineRun::End::Done);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(s(results[0].answer.text), "{\\i1}ONE{\\i0} A");
    EXPECT_FALSE(results[0].needsReview);
    EXPECT_TRUE(results[1].needsReview);
    EXPECT_EQ(results[1].issues, std::vector<std::string>{"Is this a question?"});
    EXPECT_EQ(progressed, 2u);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].name, "GLOSSARY.md");
    EXPECT_DOUBLE_EQ(run.cost(), 1.0);
    EXPECT_DOUBLE_EQ(spent, 1.0);
}

TEST_F(InlineRunTest, ThePipelineSendsTheSourceLanguageWhenOneIsSet)
{
    TranslationRun::Wiring w;
    w.language = "English";
    w.sourceLanguage = "Japanese";
    const Json params = translateParams(translationJobs({u8"one"}), w);
    EXPECT_EQ(params.find("language")->asString(), "English");
    EXPECT_EQ(params.find("sourceLanguage")->asString(), "Japanese");
}

TEST_F(InlineRunTest, StoppingThePipelineCancelsItsOneRequest)
{
    use("hang");
    TranslationRun::Wiring w;
    w.lines = wiring();
    w.language = "English";
    w.translator = {"or", "hang"};
    w.reviewer = {"or", "hang"};
    TranslationRun run(translationJobs({u8"one"}), std::move(w));
    run.start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    EXPECT_TRUE(run.running());
    run.stop();
    ASSERT_TRUE(waitFor([&] { return ended.has_value(); }));
    EXPECT_EQ(*ended, InlineRun::End::Stopped);
    EXPECT_TRUE(results.empty());
}

TEST_F(InlineRunTest, ThePipelineWithoutATranslatorModelEndsAtOnce)
{
    use("t");
    TranslationRun::Wiring w;
    w.lines = wiring();
    TranslationRun run(translationJobs({u8"one"}), std::move(w));
    run.start();
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(*ended, InlineRun::End::NoModel);
}
