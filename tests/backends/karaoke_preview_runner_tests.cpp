#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/application/template_preview.h"
#include "hikari/backends/karaoke_preview_runner.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace hikari;
using application::HostService;
using application::HostServiceReply;
using application::HostServiceRequest;
using application::KaraokeTemplaterPort;
using backends::KaraokePreviewRunner;
using backends::LuaScriptHost;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return done();
}

QString corpus(const char *name)
{
    return QStringLiteral(HIKARI_KARAOKE_CORPUS) + QLatin1Char('/') + QLatin1String(name);
}

core::Document loadFile(const QString &path)
{
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    const QByteArray data = f.readAll();
    std::vector<std::byte> bytes(static_cast<std::size_t>(data.size()));
    std::memcpy(bytes.data(), data.constData(), bytes.size());
    return core::loadAss(bytes).document;
}

core::Document loadText(const std::string &text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

// MUST KILL: a copy of karaoke_templater_tests' textExtents fake.
HostServiceReply textExtents(const HostServiceRequest &r)
{
    const auto number = [&](std::size_t i) { return i < r.style.size() ? std::stod(r.style[i]) : 0.0; };
    const double size = number(2), scaleX = number(11) / 100, scaleY = number(12) / 100, spacing = number(13);
    double ems = 0;
    int chars = 0;
    for (const char32_t c : QString::fromStdString(r.strings.at(0)).toUcs4()) {
        ems += c >= 0x2E80 ? 1.0 : 0.55;
        ++chars;
    }
    HostServiceReply out;
    out.numbers = {ems * size * scaleX + spacing * chars, size * scaleY, size * 0.21, 0};
    return out;
}

std::string lineText(const core::LineRecord &l)
{
    const auto text = core::legacy::assLineText(l);
    return std::string(text.begin(), text.end());
}

class KaraokePreview : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "karaoke_preview_runner_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    void SetUp() override
    {
        runner.setServiceHandler([this](const HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            asked.push_back(r.service);
            reply(r.service == HostService::TextExtents ? textExtents(r) : HostServiceReply::unavailable());
        });
        runner.setScript(QStringLiteral(HIKARI_KARAOKE_TEMPLATER));
    }

    std::optional<KaraokeTemplaterPort::Preview> preview(const core::Document &doc, core::LineId source)
    {
        const auto snapshot = application::templatePreviewSnapshot(doc, source);
        if (!snapshot)
            return std::nullopt;
        std::optional<KaraokeTemplaterPort::Preview> out;
        runner.run(*snapshot, [&](KaraokeTemplaterPort::Preview p) { out = std::move(p); });
        if (!waitFor([&] { return out.has_value(); }))
            return std::nullopt;
        return out;
    }

    std::map<core::LineId, std::vector<std::string>> applied(application::EditSession &session)
    {
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_KARAOKE_TEMPLATER),
                           QStringLiteral(HIKARI_LUA_INCLUDE));
        host.setServiceHandler([](const HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            reply(r.service == HostService::TextExtents ? textExtents(r) : HostServiceReply::unavailable());
        });
        std::optional<LuaScriptHost::RunOutcome> outcome;
        QObject::connect(&host, &LuaScriptHost::finished,
                         [&](LuaScriptHost::RunOutcome o, const QString &) { outcome = o; });
        host.load();
        EXPECT_TRUE(waitFor([&] { return host.state() == LuaScriptHost::State::Ready; }));
        const auto snapshot = application::snapshotForMacro(session);
        EXPECT_TRUE(snapshot && host.run(0, *snapshot));
        EXPECT_TRUE(waitFor([&] { return outcome.has_value(); }));
        EXPECT_EQ(outcome, LuaScriptHost::RunOutcome::Ok);
        EXPECT_TRUE(host.lastResult() &&
                    application::applyMacroResult(session, *snapshot, *host.lastResult(), "Apply").has_value());
        std::map<core::LineId, std::vector<std::string>> out;
        for (const auto *l : session.document().lines())
            if (l->generatedFrom.value != 0)
                out[l->generatedFrom].push_back(lineText(*l));
        return out;
    }

    KaraokePreviewRunner runner{QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_INCLUDE)};
    std::vector<HostService> asked;
};

TEST_F(KaraokePreview, APreviewIsWhatApplyWritesForTheLine)
{
    for (const char *name : {"stock-tutorials.ass", "km-minimal.ass", "stock-modifiers.ass", "stock-furigana.ass",
                             "stock-inline-fx.ass", "ke4.ass"}) {
        SCOPED_TRACE(name);
        const core::Document doc = loadFile(corpus(name));
        application::EditSession session(doc);
        const auto whole = applied(session);
        int previewed = 0;
        for (const auto *l : doc.lines()) {
            if (!application::isKaraokeLine(*l) || l->comment)
                continue;
            const auto p = preview(doc, l->id);
            ASSERT_TRUE(p && p->result) << (p ? p->error : "no preview");
            std::vector<std::string> lines;
            for (const auto &g : application::templatePreviewLines(*p->result, l->id))
                lines.push_back(lineText(g));
            const auto it = whole.find(l->id);
            EXPECT_EQ(lines, it == whole.end() ? std::vector<std::string>{} : it->second)
                << "Line " << lineText(*l);
            ++previewed;
        }
        EXPECT_GT(previewed, 0);
    }
    EXPECT_TRUE(std::ranges::all_of(asked, application::isPreviewService));
}

TEST_F(KaraokePreview, ANewerRunReplacesOneInFlight)
{
    const core::Document doc = loadFile(corpus("stock-tutorials.ass"));
    const auto source = application::templatePreviewSource(doc, std::nullopt, std::nullopt);
    ASSERT_TRUE(source);
    const auto snapshot = application::templatePreviewSnapshot(doc, *source);
    ASSERT_TRUE(snapshot);
    int first = 0, second = 0;
    ASSERT_TRUE(waitFor([&] { return runner.ready(); }));
    runner.run(*snapshot, [&](KaraokeTemplaterPort::Preview) { ++first; });
    runner.run(*snapshot, [&](KaraokeTemplaterPort::Preview p) {
        ++second;
        EXPECT_TRUE(p.result) << p.error;
    });
    ASSERT_TRUE(waitFor([&] { return second > 0; }));
    waitFor([] { return false; }, 300);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

TEST_F(KaraokePreview, ATemplateThatNeverEndsGivesWayToTheNextEdit)
{
    const std::string head =
        "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n\n[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: K,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,8,20,20,20,1\n\n"
        "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    const core::Document endless = loadText(
        head + "Comment: 0,0:00:00.00,0:00:05.00,K,,0,0,0,template syl,{\\fscx!(function() while true do end end)()!}\n"
               "Dialogue: 0,0:00:01.00,0:00:03.00,K,,0,0,0,,{\\k50}ha{\\k50}ru\n");
    const core::Document fixed = loadText(head + "Comment: 0,0:00:00.00,0:00:05.00,K,,0,0,0,template syl,{\\fscx120}\n"
                                                 "Dialogue: 0,0:00:01.00,0:00:03.00,K,,0,0,0,,{\\k50}ha{\\k50}ru\n");
    runner.setGracePeriod(200);
    ASSERT_TRUE(waitFor([&] { return runner.ready(); }));
    bool endlessDone = false;
    runner.run(*application::templatePreviewSnapshot(endless, endless.lines().back()->id),
               [&](KaraokeTemplaterPort::Preview) { endlessDone = true; });
    waitFor([] { return false; }, 200);
    const auto p = preview(fixed, fixed.lines().back()->id);
    ASSERT_TRUE(p && p->result) << (p ? p->error : "no preview");
    EXPECT_FALSE(application::templatePreviewLines(*p->result, fixed.lines().back()->id).empty());
    EXPECT_FALSE(endlessDone);
}

TEST_F(KaraokePreview, APreviewFitsInAFrame)
{
    const core::Document doc = loadFile(corpus("stock-tutorials.ass"));
    const auto source = application::templatePreviewSource(doc, std::nullopt, std::nullopt);
    ASSERT_TRUE(source);
    for (int warm = 0; warm < 3; ++warm)
        ASSERT_TRUE(preview(doc, *source));
    std::vector<qint64> times;
    for (int k = 0; k < 15; ++k) {
        QElapsedTimer t;
        t.start();
        const auto p = preview(doc, *source);
        times.push_back(t.nsecsElapsed());
        ASSERT_TRUE(p && p->result);
    }
    std::ranges::sort(times);
    const double median = static_cast<double>(times[times.size() / 2]) / 1e6;
    RecordProperty("median_ms", std::to_string(median));
    std::printf("preview median %.2f ms, slowest %.2f ms\n", median, static_cast<double>(times.back()) / 1e6);
    EXPECT_LT(median, 1000.0 / 23.976);
}

}
