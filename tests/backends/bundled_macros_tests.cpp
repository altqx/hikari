// HIKARI_UPDATE_MACRO_GOLDENS=1 rewrites the goldens from the bundled scripts.
#include "hikari/application/automation.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <QChar>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

using hikari::application::HostService;
using hikari::application::HostServiceReply;
using hikari::application::HostServiceRequest;
using hikari::backends::LuaScriptHost;

namespace {

constexpr char kBezier[] = "Bezier";
constexpr char kFsp[] = "Letter spacing to positioned clusters…";
constexpr int kFromLine = 0;
constexpr int kFromValue = 1;

QString corpus(const char *name)
{
    return QStringLiteral(HIKARI_MACRO_CORPUS "/") + QLatin1String(name);
}

QString bundled(const char *name)
{
    return QStringLiteral(HIKARI_AUTOLOAD_DIR "/") + QString::fromUtf8(name);
}

std::string readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll().toStdString();
}

std::vector<std::byte> bytesOf(const std::string &text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

bool waitFor(const std::function<bool()> &done, int ms = 120'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

bool isMark(char32_t c)
{
    const auto category = QChar::category(c);
    return category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining ||
           category == QChar::Mark_Enclosing;
}

// 0.55 em per character, marks take no room; the style's spacing follows each character.
HostServiceReply textExtents(const HostServiceRequest &r)
{
    const auto number = [&](std::size_t i) { return i < r.style.size() ? std::stod(r.style[i]) : 0.0; };
    const double size = number(2), scaleX = number(11) / 100, scaleY = number(12) / 100, spacing = number(13);
    double ems = 0;
    int chars = 0;
    for (const char32_t c : QString::fromStdString(r.strings.at(0)).toUcs4()) {
        if (!isMark(c))
            ems += 0.55;
        ++chars;
    }
    HostServiceReply out;
    out.numbers = {ems * size * scaleX + spacing * chars, size * scaleY, size * 0.21, 0};
    return out;
}

// Generated Lines whose text, override blocks removed, starts with a mark.
QStringList linesStartingWithAMark(const std::string &document)
{
    static const QRegularExpression block(QStringLiteral("\\{[^}]*\\}"));
    QStringList found;
    for (const QString &line : QString::fromStdString(document).split(u'\n')) {
        const qsizetype pos = line.indexOf(u"\\pos(");
        if (!line.startsWith(u"Dialogue:") || pos < 0)
            continue;
        const QList<uint> glyphs = line.mid(line.indexOf(u'}', pos) + 1).remove(block).toUcs4();
        if (!glyphs.isEmpty() && isMark(glyphs.front()))
            found << line;
    }
    return found;
}

class BundledMacros : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "bundled_macros_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    // Runs the macro on every Dialogue line; the dialog, if any, gets `button` and `spacing`.
    std::string applied(const std::string &document, const QString &script, const char *macroName, int button = -1,
                        double spacing = 0)
    {
        hikari::application::EditSession session(hikari::core::loadAss(bytesOf(document)).document);
        std::set<hikari::core::LineId> dialogue;
        for (const auto *line : session.document().lines())
            if (!line->comment)
                dialogue.insert(line->id);
        if (dialogue.empty())
            return {};
        session.setSelection(hikari::application::Selection{*dialogue.begin(), dialogue});

        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), script, QStringLiteral(HIKARI_LUA_INCLUDE));
        host.setServiceHandler([](const HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            reply(r.service == HostService::TextExtents ? textExtents(r) : HostServiceReply::unavailable());
        });
        host.setDialogHandler([&](const hikari::application::DialogRequest &request, LuaScriptHost::DialogReply reply) {
            auto result = hikari::application::initialDialogResult(request);
            result.pressed = button;
            for (std::size_t i = 0; i < request.controls.size(); ++i)
                if (request.controls[i].kind == "floatedit")
                    result.values[i] = spacing;
            reply(result);
        });
        std::optional<LuaScriptHost::RunOutcome> outcome;
        QString message;
        log.clear();
        QObject::connect(&host, &LuaScriptHost::logged, [&](const QString &t) { log << t; });
        QObject::connect(&host, &LuaScriptHost::finished, [&](LuaScriptHost::RunOutcome o, const QString &m) {
            outcome = o;
            message = m;
        });
        host.load();
        waitFor([&] { return host.state() != LuaScriptHost::State::Loading; });
        if (host.state() != LuaScriptHost::State::Ready) {
            ADD_FAILURE() << host.lastError().toStdString();
            return {};
        }
        int index = -1;
        for (std::size_t i = 0; i < host.info().macros.size(); ++i)
            if (host.info().macros[i].name == macroName)
                index = static_cast<int>(i);
        const auto snapshot = hikari::application::snapshotForMacro(session);
        if (index < 0 || !snapshot || !host.run(index, *snapshot) || !waitFor([&] { return outcome.has_value(); })) {
            ADD_FAILURE() << "the run did not start or end";
            return {};
        }
        if (*outcome != LuaScriptHost::RunOutcome::Ok || !host.lastResult()) {
            ADD_FAILURE() << message.toStdString() << log.join(QString()).toStdString();
            return {};
        }
        if (!hikari::application::applyMacroResult(session, *snapshot, *host.lastResult(), macroName))
            return {};
        const auto saved = hikari::core::encodeAss(session.document());
        return std::string(reinterpret_cast<const char *>(saved.data()), saved.size());
    }

    void matchesGolden(const std::string &output, const char *golden)
    {
        ASSERT_FALSE(output.empty());
        if (qEnvironmentVariableIsSet("HIKARI_UPDATE_MACRO_GOLDENS")) {
            QFile out(corpus(golden));
            ASSERT_TRUE(out.open(QIODevice::WriteOnly));
            out.write(output.data(), static_cast<qint64>(output.size()));
        }
        EXPECT_EQ(output, readText(corpus(golden))) << golden;
    }

    QStringList log;
};

TEST_F(BundledMacros, BezierLatinOutputMatchesVersionOne)
{
    const std::string document = readText(corpus("bezier-latin.ass"));
    const std::string output = applied(document, bundled("BezierToText.lua"), kBezier);
    EXPECT_EQ(output, applied(document, corpus("BezierToText-1.0.lua"), kBezier));
    matchesGolden(output, "bezier-latin.out.ass");
}

TEST_F(BundledMacros, BezierKeepsMarksOnTheirBaseCharacter)
{
    const std::string output = applied(readText(corpus("bezier-thai.ass")), bundled("BezierToText.lua"), kBezier);
    matchesGolden(output, "bezier-thai.out.ass");
    EXPECT_NE(output.find("}กิ\n"), std::string::npos);
    EXPECT_NE(output.find("}ข้\n"), std::string::npos);
    EXPECT_NE(output.find("}น้ำ\n"), std::string::npos);
    EXPECT_NE(output.find("}ก{\\c&H0000FF&}ิ\n"), std::string::npos) << "a mark after a tag joins its base's line";
    EXPECT_NE(output.find("}e\xcc\x81\n"), std::string::npos);
    EXPECT_EQ(linesStartingWithAMark(output), QStringList{});
}

TEST_F(BundledMacros, FspFromTheLineKeepsClustersWhole)
{
    const std::string output = applied(readText(corpus("fsp.ass")), bundled("fsp-to-pos.lua"), kFsp, kFromLine);
    matchesGolden(output, "fsp.out.ass");
    EXPECT_NE(output.find("}กิ\n"), std::string::npos);
    EXPECT_NE(output.find("}น้ำ\n"), std::string::npos);
    EXPECT_NE(output.find("\\fsp0}กิ\n"), std::string::npos) << "a Style's spacing would part a mark from its base";
    EXPECT_NE(output.find("Comment: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\fsp10}Hello world\n"),
              std::string::npos);
    EXPECT_NE(output.find("Dialogue: 0,0:00:30.00,0:00:35.00,Default,,0,0,0,,{\\move(0,0,100,100)\\fsp5}ไม่แตะ\n"),
              std::string::npos);
    EXPECT_EQ(log, QStringList{QStringLiteral("Lines with \\move or a drawing were left as they are.\n")});
    EXPECT_EQ(linesStartingWithAMark(output), QStringList{});
}

TEST_F(BundledMacros, FspFromTheDialogSpacesEveryLineAlike)
{
    const std::string output = applied(readText(corpus("fsp.ass")), bundled("fsp-to-pos.lua"), kFsp, kFromValue, 12.5);
    matchesGolden(output, "fsp-value.out.ass");
}

TEST_F(BundledMacros, FspCancelLeavesTheDocument)
{
    const std::string document = readText(corpus("fsp.ass"));
    hikari::application::EditSession session(hikari::core::loadAss(bytesOf(document)).document);
    const auto first = session.document().lines().front()->id;
    session.setSelection(hikari::application::Selection{first, {first}});
    LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), bundled("fsp-to-pos.lua"), QStringLiteral(HIKARI_LUA_INCLUDE));
    host.setDialogHandler([](const hikari::application::DialogRequest &request, LuaScriptHost::DialogReply reply) {
        auto result = hikari::application::initialDialogResult(request);
        result.pressed = 2;
        reply(result);
    });
    std::optional<LuaScriptHost::RunOutcome> outcome;
    QObject::connect(&host, &LuaScriptHost::finished, [&](LuaScriptHost::RunOutcome o, const QString &) { outcome = o; });
    host.load();
    ASSERT_TRUE(waitFor([&] { return host.state() != LuaScriptHost::State::Loading; }));
    const auto snapshot = hikari::application::snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    ASSERT_TRUE(host.run(0, *snapshot));
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    EXPECT_EQ(*outcome, LuaScriptHost::RunOutcome::Cancelled);
}

}
