// HIKARI_UPDATE_KARAOKE_GOLDENS=1 rewrites the goldens from the unchanged 3.2.2 script.
#include "hikari/application/automation.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/karaoke_presets.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

using hikari::application::HostService;
using hikari::application::HostServiceReply;
using hikari::application::HostServiceRequest;
using hikari::application::KaraokePreset;
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

QString presetDir()
{
    return QStringLiteral(HIKARI_KARAOKE_PRESETS);
}

QString fixtureDir()
{
    return QStringLiteral(HIKARI_KARAOKE_PRESET_FIXTURES);
}

QString pristineScript()
{
    return QStringLiteral(HIKARI_KARAOKE_CORPUS) + QStringLiteral("/kara-templater-aegisub-3.2.2.lua");
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

std::vector<std::byte> bytesOf(const std::string &text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::string narrow(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

// MUST KILL: a copy of karaoke_templater_tests' textExtents fake.
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

struct Preset {
    QString file;
    KaraokePreset preset;
};

std::vector<Preset> presets()
{
    std::vector<Preset> out;
    for (const QString &name : QDir(presetDir()).entryList({QStringLiteral("*.ass")}, QDir::Files, QDir::Name)) {
        const auto preset = hikari::application::readKaraokePreset(readBytes(presetDir() + QLatin1Char('/') + name));
        EXPECT_TRUE(preset.has_value()) << name.toStdString();
        if (preset)
            out.push_back({name, *preset});
    }
    return out;
}

std::vector<std::byte> fixtureWith(const KaraokePreset &preset)
{
    std::string text = textOf(readBytes(fixtureDir() + QStringLiteral("/fixture.ass")));
    std::string lines;
    for (const auto &l : preset.lines)
        lines += "Comment: " + std::to_string(l.layer) + ",0:00:00.00,0:00:00.00,Romaji,,0,0,0," + narrow(l.effect) +
                 "," + narrow(l.text) + "\n";
    const auto at = text.find("Dialogue: ");
    EXPECT_NE(at, std::string::npos);
    text.insert(at, lines);
    return bytesOf(text);
}

QString goldenOf(const QString &preset)
{
    return fixtureDir() + QLatin1Char('/') + QFileInfo(preset).completeBaseName() + QStringLiteral(".out.ass");
}

constexpr char kAll[] = "Apply karaoke template";
constexpr char kSelected[] = "Apply karaoke template to selected lines";

class KaraokePresets : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "karaoke_presets_tests";
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
        QObject::connect(host.get(), &LuaScriptHost::logged, [this](const QString &t) { log << t; });
        QObject::connect(host.get(), &LuaScriptHost::finished,
                         [this](LuaScriptHost::RunOutcome o, const QString &) { outcome = o; });
        host->load();
        waitFor([&] { return host->state() != LuaScriptHost::State::Loading; });
        return host;
    }

    bool apply(hikari::application::EditSession &session, const QString &script, const std::string &macroName)
    {
        auto host = load(script);
        if (host->state() != LuaScriptHost::State::Ready) {
            ADD_FAILURE() << host->lastError().toStdString();
            return false;
        }
        int index = -1;
        for (std::size_t i = 0; i < host->info().macros.size(); ++i)
            if (host->info().macros[i].name == macroName)
                index = static_cast<int>(i);
        const auto snapshot = hikari::application::snapshotForMacro(session);
        outcome.reset();
        log.clear();
        if (index < 0 || !snapshot || !host->run(index, *snapshot))
            return false;
        if (!waitFor([&] { return outcome.has_value(); }) || *outcome != LuaScriptHost::RunOutcome::Ok ||
            !host->lastResult()) {
            ADD_FAILURE() << "the run failed: " << log.join(QString()).toStdString();
            return false;
        }
        report = QJsonDocument::fromJson(QByteArray::fromStdString(host->lastResult()->report)).object();
        return hikari::application::applyMacroResult(session, *snapshot, *host->lastResult(), macroName).has_value();
    }

    std::string templated(const std::vector<std::byte> &bytes, const QString &script)
    {
        hikari::application::EditSession session(hikari::core::loadAss(bytes).document);
        if (!apply(session, script, kAll))
            return {};
        return textOf(hikari::core::encodeAss(session.document()));
    }

    std::optional<LuaScriptHost::RunOutcome> outcome;
    QStringList log;
    QJsonObject report;
};

TEST_F(KaraokePresets, TenPresetsReadWithTitlesAndDescriptions)
{
    const auto all = presets();
    EXPECT_GE(all.size(), 10u);
    std::set<std::u8string> titles;
    for (const auto &[file, preset] : all) {
        SCOPED_TRACE(file.toStdString());
        EXPECT_TRUE(titles.insert(preset.title).second) << "two presets are called " << narrow(preset.title);
        EXPECT_FALSE(preset.description.empty());
        EXPECT_FALSE(preset.lines.empty());
    }
}

TEST_F(KaraokePresets, OnlyTemplateAndCodeCommentsMakeAPreset)
{
    const std::string head = "[Script Info]\nTitle: T\nDescription: D\n\n[Events]\nFormat: Layer, Start, End, Style, "
                             "Name, MarginL, MarginR, MarginV, Effect, Text\n";
    const auto read = [&](const std::string &events) {
        return hikari::application::readKaraokePreset(bytesOf(head + events));
    };
    const auto ok = read("Comment: 1,0:00:00.00,0:00:00.00,Default,,0,0,0,template syl,{\\b1}\n"
                         "Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,code once,x = 1\n");
    ASSERT_TRUE(ok.has_value());
    ASSERT_EQ(ok->lines.size(), 2u);
    EXPECT_EQ(ok->lines[0].layer, 1);
    EXPECT_EQ(ok->lines[0].effect, u8"template syl");
    EXPECT_EQ(ok->lines[0].text, u8"{\\b1}");
    EXPECT_EQ(ok->lines[1].effect, u8"code once");
    EXPECT_EQ(ok->title, u8"T");
    EXPECT_EQ(ok->description, u8"D");
    EXPECT_FALSE(read(""));
    EXPECT_FALSE(read("Dialogue: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,template syl,{\\b1}\n"));
    EXPECT_FALSE(read("Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,karaoke,{\\k10}a\n"));
    EXPECT_FALSE(read("Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,templates syl,{\\b1}\n"));
    EXPECT_FALSE(hikari::application::readKaraokePreset(
        bytesOf("[Script Info]\nTitle: T\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
                "MarginV, Effect, Text\nComment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,template syl,x\n")));
}

TEST_F(KaraokePresets, EachPresetGeneratesItsGolden)
{
    const bool update = qEnvironmentVariableIsSet("HIKARI_UPDATE_KARAOKE_GOLDENS");
    for (const auto &[file, preset] : presets()) {
        SCOPED_TRACE(file.toStdString());
        const auto input = fixtureWith(preset);
        const std::string reference = templated(input, pristineScript());
        ASSERT_FALSE(reference.empty());
        if (update) {
            QFile out(goldenOf(file));
            ASSERT_TRUE(out.open(QIODevice::WriteOnly));
            out.write(reference.data(), static_cast<qint64>(reference.size()));
        }
        EXPECT_EQ(reference, textOf(readBytes(goldenOf(file))))
            << "the unchanged 2.1.7 script no longer gives the golden output";

        hikari::application::EditSession session(hikari::core::loadAss(input).document);
        ASSERT_TRUE(apply(session, bundledScript(), kAll));
        EXPECT_EQ(textOf(hikari::core::encodeAss(session.document())), reference);
        EXPECT_EQ(report.value(QStringLiteral("problems")).toInt(-1), 0) << log.join(QString()).toStdString();
        int generated = 0;
        for (const auto *l : session.document().lines()) {
            if (l->comment || l->effect != u8"fx")
                continue;
            ++generated;
            EXPECT_EQ(l->style, u8"Romaji");
            EXPECT_EQ(l->text.find(u8'$'), std::u8string::npos) << narrow(l->text);
            EXPECT_EQ(l->text.find(u8'!'), std::u8string::npos) << narrow(l->text);
        }
        EXPECT_GT(generated, 0);
        EXPECT_EQ(report.value(QStringLiteral("generated")).toInt(), generated);
    }
}

TEST_F(KaraokePresets, ScaledStylesKeepTheirScale)
{
    const struct {
        QString file;
        std::vector<std::u8string> scales;
    } cases[] = {
        {QStringLiteral("04-scale-pop.ass"), {u8"\\fscx104\\fscy156\\1c", u8"\\fscx80\\fscy120)"}},
        {QStringLiteral("08-echo.ass"), {u8"\\fscx144\\fscy216\\alpha"}},
    };
    const auto all = presets();
    for (const auto &c : cases) {
        SCOPED_TRACE(c.file.toStdString());
        const auto preset = std::ranges::find(all, c.file, &Preset::file);
        ASSERT_NE(preset, all.end());
        std::string text = textOf(fixtureWith(preset->preset));
        const std::string style =
            "Style: Romaji,Arial,48,&H00FFFFFF,&H00FF8C3C,&H00202020,&H80000000,0,0,0,0,100,100,";
        const auto at = text.find(style);
        ASSERT_NE(at, std::string::npos);
        text.replace(at + style.size() - 8, 8, "80,120,");
        const std::string reference = templated(bytesOf(text), pristineScript());
        hikari::application::EditSession session(hikari::core::loadAss(bytesOf(text)).document);
        ASSERT_TRUE(apply(session, bundledScript(), kAll));
        EXPECT_EQ(textOf(hikari::core::encodeAss(session.document())), reference);
        int scaled = 0;
        for (const auto *l : session.document().lines()) {
            if (l->comment || l->effect != u8"fx" || l->text.find(u8"\\fsc") == std::u8string::npos)
                continue;
            ++scaled;
            for (const auto &scale : c.scales)
                EXPECT_NE(l->text.find(scale), std::u8string::npos) << narrow(l->text);
        }
        EXPECT_GT(scaled, 0);
    }
}

TEST_F(KaraokePresets, RegeneratingOneLineChangesNothing)
{
    for (const auto &[file, preset] : presets()) {
        SCOPED_TRACE(file.toStdString());
        hikari::application::EditSession session(hikari::core::loadAss(fixtureWith(preset)).document);
        ASSERT_TRUE(apply(session, bundledScript(), kAll));
        const std::string whole = textOf(hikari::core::encodeAss(session.document()));
        std::vector<hikari::core::LineId> karaoke;
        for (const auto *l : session.document().lines())
            if (l->effect == u8"karaoke" && l->style == u8"Romaji")
                karaoke.push_back(l->id);
        ASSERT_EQ(karaoke.size(), 3u);
        for (const auto id : karaoke) {
            session.setSelection(hikari::application::Selection{id, {id}});
            ASSERT_TRUE(apply(session, bundledScript(), kSelected));
            EXPECT_EQ(report.value(QStringLiteral("problems")).toInt(-1), 0);
            EXPECT_EQ(textOf(hikari::core::encodeAss(session.document())), whole);
        }
    }
}

}
