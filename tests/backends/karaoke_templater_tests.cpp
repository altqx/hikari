// KE1: the bundled karaoke templater (Autoload/kara-templater.lua) against a
// corpus of real templates (karaoke-corpus/: the Aegisub templater docs'
// templates, Karaoke Mugen's, Kara Effector's ke4 functions, errors and
// karaoke tag variants). Each corpus Document is templated in the isolated
// Lua helper and saved; the saved bytes must equal both the committed
// golden file and what the unchanged 2.1.7 script, as Aegisub 3.2.2 shipped
// it (karaoke-corpus/kara-templater-aegisub-3.2.2.lua), produces in the same
// host. Text extents come from a deterministic fake, so the goldens do not
// depend on the fonts of the machine.
//
// HIKARI_UPDATE_KARAOKE_GOLDENS=1 rewrites the goldens from the unchanged
// script (never from the bundled one).
#include "hikari/application/automation.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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

bool waitFor(const std::function<bool()> &done, int ms = 50'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QString corpusDir()
{
    return QStringLiteral(HIKARI_KARAOKE_CORPUS);
}

QString pristineScript()
{
    return corpusDir() + QStringLiteral("/kara-templater-aegisub-3.2.2.lua");
}

QString bundledScript()
{
    return QStringLiteral(HIKARI_KARAOKE_TEMPLATER);
}

std::vector<std::byte> readBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(data.size()));
    std::memcpy(out.data(), data.constData(), out.size());
    return out;
}

std::string textOf(const std::vector<std::byte> &bytes)
{
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

// Deterministic text_extents: 0.55 em per character below U+2E80 and 1 em
// from there on (CJK), scaled by the style's ScaleX/ScaleY and Spacing
// (positional style fields, Name first).
HostServiceReply textExtents(const HostServiceRequest &r)
{
    const auto number = [&](std::size_t i) { return i < r.style.size() ? std::stod(r.style[i]) : 0.0; };
    const double size = number(2), scaleX = number(11) / 100, scaleY = number(12) / 100, spacing = number(13);
    const QString text = QString::fromStdString(r.strings.at(0));
    double ems = 0;
    int chars = 0;
    for (const char32_t c : text.toUcs4()) {
        ems += c >= 0x2E80 ? 1.0 : 0.55;
        ++chars;
    }
    HostServiceReply out;
    out.numbers = {ems * size * scaleX + spacing * chars, size * scaleY, size * 0.21, 0};
    return out;
}

struct Outcome {
    std::optional<LuaScriptHost::RunOutcome> outcome;
    QString message;
    QStringList log;
};

class KaraokeTemplater : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "karaoke_templater_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    std::unique_ptr<LuaScriptHost> load(const QString &script)
    {
        auto host = std::make_unique<LuaScriptHost>(QStringLiteral(HIKARI_LUA_HELPER), script,
                                                    QStringLiteral(HIKARI_LUA_INCLUDE));
        host->setServiceHandler([](const HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            reply(r.service == HostService::TextExtents ? textExtents(r) : HostServiceReply::unavailable());
        });
        QObject::connect(host.get(), &LuaScriptHost::logged, [this](const QString &t) { run.log << t; });
        QObject::connect(host.get(), &LuaScriptHost::finished,
                         [this](LuaScriptHost::RunOutcome o, const QString &m) {
                             run.outcome = o;
                             run.message = m;
                         });
        host->load();
        waitFor([&] { return host->state() != LuaScriptHost::State::Loading; });
        return host;
    }

    static int macro(const LuaScriptHost &host, const std::string &name)
    {
        for (std::size_t i = 0; i < host.info().macros.size(); ++i)
            if (host.info().macros[i].name == name)
                return static_cast<int>(i);
        ADD_FAILURE() << "no macro " << name;
        return -1;
    }

    // Runs `macroName` of `script` on the session's Document and applies its
    // result as the automation shell does.
    bool apply(hikari::application::EditSession &session, const QString &script, const std::string &macroName)
    {
        auto host = load(script);
        if (host->state() != LuaScriptHost::State::Ready) {
            ADD_FAILURE() << host->lastError().toStdString();
            return false;
        }
        const auto snapshot = hikari::application::snapshotForMacro(session);
        if (!snapshot)
            return false;
        run = {};
        if (!host->run(macro(*host, macroName), *snapshot))
            return false;
        if (!waitFor([&] { return run.outcome.has_value(); })) {
            ADD_FAILURE() << "the run did not end";
            return false;
        }
        if (*run.outcome != LuaScriptHost::RunOutcome::Ok || !host->lastResult()) {
            ADD_FAILURE() << run.message.toStdString();
            return false;
        }
        return hikari::application::applyMacroResult(session, *snapshot, *host->lastResult(), macroName).has_value();
    }

    std::string templated(const QString &document, const QString &script)
    {
        auto loaded = hikari::core::loadAss(readBytes(document));
        hikari::application::EditSession session(std::move(loaded.document));
        if (!apply(session, script, "Apply karaoke template"))
            return {};
        return textOf(hikari::core::encodeAss(session.document()));
    }

    Outcome run;
};

QStringList corpus()
{
    QStringList documents;
    for (const QString &name : QDir(corpusDir()).entryList({QStringLiteral("*.ass")}, QDir::Files, QDir::Name))
        if (!name.endsWith(QLatin1String(".out.ass")))
            documents << name;
    return documents;
}

QString goldenOf(const QString &document)
{
    return corpusDir() + QLatin1Char('/') + QFileInfo(document).completeBaseName() + QStringLiteral(".out.ass");
}

TEST_F(KaraokeTemplater, CorpusCoversEveryKind)
{
    const QStringList files = corpus();
    for (const char *name : {"stock-tutorials.ass", "stock-modifiers.ass", "stock-environment.ass",
                             "stock-furigana.ass", "stock-inline-fx.ass", "km-minimal.ass", "ke4.ass",
                             "errors.ass", "late-template.ass"})
        EXPECT_TRUE(files.contains(QLatin1String(name))) << name;
}

// The acceptance lock: byte-identical output before and after KE1.
TEST_F(KaraokeTemplater, BundledTemplaterMatchesAegisub322ByteForByte)
{
    const bool update = qEnvironmentVariableIsSet("HIKARI_UPDATE_KARAOKE_GOLDENS");
    for (const QString &name : corpus()) {
        SCOPED_TRACE(name.toStdString());
        const QString document = corpusDir() + QLatin1Char('/') + name;
        const std::string reference = templated(document, pristineScript());
        ASSERT_FALSE(reference.empty());
        if (update) {
            QFile out(goldenOf(document));
            ASSERT_TRUE(out.open(QIODevice::WriteOnly));
            out.write(reference.data(), static_cast<qint64>(reference.size()));
        }
        const std::string golden = textOf(readBytes(goldenOf(document)));
        EXPECT_EQ(reference, golden) << "the unchanged 2.1.7 script no longer gives the golden output";
        const std::string bundled = templated(document, bundledScript());
        EXPECT_EQ(bundled, golden);
    }
}


// KE1-no-50-line-gate: stock 2.1.7 offers the macro only when a template is
// among the first 50 dialogue lines; HikariSub looks at every Line.
TEST_F(KaraokeTemplater, MacroIsOfferedWhereverTheTemplateIs)
{
    const auto validates = [&](const QString &script) {
        auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/late-template.ass")));
        hikari::application::EditSession session(std::move(loaded.document));
        auto host = load(script);
        const auto snapshot = hikari::application::snapshotForMacro(session);
        run = {};
        EXPECT_TRUE(host->validate(macro(*host, "Apply karaoke template"), *snapshot));
        EXPECT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
        return run.outcome == LuaScriptHost::RunOutcome::Ok;
    };
    EXPECT_FALSE(validates(pristineScript())) << "stock refuses a template after the first 50 Lines";
    EXPECT_TRUE(validates(bundledScript()));
}

// Without any template the macro is still not offered.
TEST_F(KaraokeTemplater, MacroIsNotOfferedWithoutATemplate)
{
    std::string text = textOf(readBytes(corpusDir() + QStringLiteral("/late-template.ass")));
    const auto at = text.find("Comment: 0,0:00:00.00,0:00:05.00,Late,,0,0,0,template syl");
    ASSERT_NE(at, std::string::npos);
    text.erase(at);
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    hikari::application::EditSession session(hikari::core::loadAss(bytes).document);
    auto host = load(bundledScript());
    const auto snapshot = hikari::application::snapshotForMacro(session);
    run = {};
    ASSERT_TRUE(host->validate(macro(*host, "Apply karaoke template"), *snapshot));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
}


// KE1-template-problems: each problem with a template or code Line names the
// Line (its Grid number) after 2.1.7's message, and the macro returns those
// Lines as the selection, the first active. The output stays 3.2.2's.
TEST_F(KaraokeTemplater, TemplateProblemsNameAndSelectTheirLines)
{
    auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/errors.ass")));
    hikari::application::EditSession session(std::move(loaded.document));
    std::vector<hikari::core::LineId> ids;
    for (const auto *l : session.document().lines())
        ids.push_back(l->id);
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    // Lines 1-9 each have a problem; 10 and 11 (TT#141's 3.2-era code line
    // and its template) have none.
    std::set<hikari::core::LineId> problems(ids.begin(), ids.begin() + 9);
    EXPECT_EQ(session.selection().selected, problems);
    EXPECT_EQ(session.selection().active, ids.front());
    const QString log = run.log.join(QString());
    EXPECT_TRUE(log.contains(QStringLiteral("Failed to parse Lua code: ")));
    for (int number = 1; number <= 9; ++number) {
        const QString name = (number <= 2 ? QStringLiteral("Code Line: %1\n") : QStringLiteral("Template Line: %1\n"))
                                 .arg(number);
        EXPECT_TRUE(log.contains(name)) << name.toStdString();
    }
    EXPECT_FALSE(log.contains(QStringLiteral("Line: 10\n")));
    EXPECT_TRUE(log.contains(QStringLiteral("Runtime error in template expression: ")));
    EXPECT_TRUE(log.contains(QStringLiteral("Expression producing error: nosuch.field\nTemplate with expression: "
                                            "{\\pos(!nosuch.field!,$y)}\nTemplate Line: 3\n\n")));
    EXPECT_TRUE(log.contains(QStringLiteral("Unknown variable name: nosuchvar\nIn karaoke template: "
                                            "{\\pos($x,$nosuchvar)}\nTemplate Line: 5\n\n")));
}

// Stock 2.1.7 names no Line and leaves the selection as it was.
TEST_F(KaraokeTemplater, StockTemplaterNamesNoLine)
{
    auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/errors.ass")));
    hikari::application::EditSession session(std::move(loaded.document));
    const auto last = session.document().lines().back()->id;
    session.setSelection(hikari::application::Selection{last, {last}});
    ASSERT_TRUE(apply(session, pristineScript(), "Apply karaoke template"));
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{last}));
    EXPECT_FALSE(run.log.join(QString()).contains(QStringLiteral("Line: ")));
}

// A Document without problems keeps its selection.
TEST_F(KaraokeTemplater, CleanRunKeepsTheSelection)
{
    auto loaded = hikari::core::loadAss(readBytes(corpusDir() + QStringLiteral("/stock-tutorials.ass")));
    hikari::application::EditSession session(std::move(loaded.document));
    const auto last = session.document().lines().back()->id;
    session.setSelection(hikari::application::Selection{last, {last}});
    ASSERT_TRUE(apply(session, bundledScript(), "Apply karaoke template"));
    EXPECT_EQ(session.selection().active, last);
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{last}));
    EXPECT_TRUE(run.log.isEmpty()) << run.log.join(QString()).toStdString();
}

} // namespace
