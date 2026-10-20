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

} // namespace
