#include "hikari/application/automation_services.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"

#include "canonical_tags.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>

using hikari::backends::LuaScriptHost;
namespace app = hikari::application;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 60'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QString corpusDir() { return QStringLiteral(HIKARI_MOTION_CORPUS); }

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString str(const std::string &s) { return QString::fromStdString(s); }

struct NoClipboard : app::ClipboardPort {
    std::string text() const override { return {}; }
    bool setText(const std::string &) override { return false; }
};

struct CorpusVideo : app::MacroMediaPort {
    std::vector<std::int64_t> starts = [] {
        std::vector<std::int64_t> out;
        for (std::int64_t i = 0; i < 2398; ++i)
            out.push_back(i * 1001 / 24);
        return out;
    }();
    std::optional<std::int64_t> frameFromMs(std::int64_t) const override { return std::nullopt; }
    std::optional<std::int64_t> msFromFrame(std::int64_t) const override { return std::nullopt; }
    std::optional<app::AegisubFrameTimes> frameTimes() const override { return app::AegisubFrameTimes::fromFrameStarts(starts); }
    std::optional<VideoSize> videoSize() const override { return VideoSize{1920, 1080, 16, 9}; }
    std::optional<std::vector<std::int64_t>> keyframes() const override { return std::vector<std::int64_t>{}; }
    void frame(std::int64_t, bool, std::function<void(std::optional<Frame>)> reply) override { reply(std::nullopt); }
    std::optional<std::pair<std::int64_t, std::int64_t>> audioSelection() const override { return std::nullopt; }
    std::optional<Project> project() const override { return Project{104, "", "", "", "", 1}; }
    std::optional<std::string> fileName() const override { return std::nullopt; }
};

QJsonValue extraValue(const std::string &key, const std::string &value)
{
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(value), &error);
    if (key != "a-mo" || error.error != QJsonParseError::NoError || !doc.isObject())
        return str(value);
    QJsonObject o = doc.object();
    if (o.contains(QStringLiteral("uuid")))
        o.insert(QStringLiteral("uuid"), QStringLiteral("<uuid>"));
    return o;
}

QJsonArray events(const app::MacroSnapshot &s)
{
    QJsonArray out;
    for (const auto &d : s.dialogues) {
        QJsonObject event{{"layer", d.layer}, {"start", double(d.startMs)}, {"end", double(d.endMs)},
                          {"style", str(d.style)}, {"text", str(hikari::testing::canonicalTags(d.text))}};
        if (d.comment)
            event.insert("comment", true);
        if (!d.extra.empty()) {
            QJsonObject extra;
            for (const auto &[key, value] : d.extra)
                extra.insert(str(key), extraValue(key, value));
            event.insert("extra", extra);
        }
        out.append(event);
    }
    return out;
}

app::DialogResult answer(const app::DialogRequest &request, const QJsonObject &entry)
{
    app::DialogResult result = app::initialDialogResult(request);
    const QString button = entry.value("button").toString();
    for (std::size_t i = 0; i < request.buttons.size(); ++i)
        if (str(request.buttons[i]) == button)
            result.pressed = int(i);
    EXPECT_GE(result.pressed, 0) << "no button " << button.toStdString();
    const QJsonObject values = entry.value("values").toObject();
    for (std::size_t i = 0; i < request.controls.size(); ++i) {
        QJsonValue v = values.value(str(request.controls[i].name));
        if (v.isUndefined() || request.controls[i].name.empty())
            continue;
        if (v.isString() && v.toString().startsWith(QLatin1Char('@')))
            v = QString::fromUtf8(readFile(corpusDir() + QLatin1Char('/') + v.toString().mid(1)));
        switch (app::dialogValueType(request.controls[i].kind)) {
        case app::DialogValueType::Text: result.values[i] = v.toString().toStdString(); break;
        case app::DialogValueType::Integer: result.values[i] = v.toInt(); break;
        case app::DialogValueType::Number: result.values[i] = v.toDouble(); break;
        case app::DialogValueType::Boolean: result.values[i] = v.toBool(); break;
        case app::DialogValueType::None: break;
        }
    }
    return result;
}

app::EditSession corpusSession(const QString &name)
{
    const QByteArray data = readFile(corpusDir() + QLatin1Char('/') + name);
    std::vector<std::byte> bytes(static_cast<std::size_t>(data.size()));
    std::memcpy(bytes.data(), data.data(), bytes.size());
    return app::EditSession(hikari::core::loadAss(bytes).document);
}

class MotionParity : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "motion_parity_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    void freshAutomationFolder(const QString &name)
    {
        automation = work.filePath(name + QStringLiteral("/Automation"));
        for (const char *sub : {"log", "autosave", "temp", "config"})
            QDir().mkpath(automation + QLatin1Char('/') + QLatin1String(sub));
        QFile cfg(automation + QStringLiteral("/config/l0.DependencyControl.json"));
        ASSERT_TRUE(cfg.open(QIODevice::WriteOnly));
        cfg.write(R"({"config": {"updaterEnabled": false}})");
    }

    void SetUp() override
    {
        ASSERT_TRUE(work.isValid());
        router.setClipboard(&clipboard);
        router.setMedia(&video);
        router.setPathContext([this] {
            app::AutomationPathContext paths;
            paths.automationDir = automation.toStdString();
            paths.dictionaryDir = work.filePath(QStringLiteral("Dictionary")).toStdString();
            return paths;
        });
    }

    struct Run {
        QJsonObject record;
        app::Selection selection;
    };

    Run run(app::EditSession &session, const QString &macroName, const QJsonArray &answers)
    {
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_AMO_SCRIPT),
                           QStringLiteral(HIKARI_LUA_INCLUDE));
        std::optional<LuaScriptHost::RunOutcome> outcome;
        QString message;
        QStringList log;
        host.setServiceHandler([this](const app::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            router.handle(r, std::move(reply));
        });
        QObject::connect(&host, &LuaScriptHost::logged, [&](const QString &t) { log << t; });
        QObject::connect(&host, &LuaScriptHost::finished, [&](LuaScriptHost::RunOutcome o, const QString &m) {
            outcome = o;
            message = m;
        });
        host.load();
        waitFor([&] { return host.state() != LuaScriptHost::State::Loading; });
        Run out;
        if (host.state() != LuaScriptHost::State::Ready) {
            ADD_FAILURE() << host.lastError().toStdString();
            return out;
        }
        int index = -1;
        QStringList names;
        for (std::size_t i = 0; i < host.info().macros.size(); ++i) {
            names << str(host.info().macros[i].name);
            if (str(host.info().macros[i].name) == macroName)
                index = int(i);
        }
        if (index < 0) {
            ADD_FAILURE() << "no macro " << macroName.toStdString() << " in " << names.join(", ").toStdString();
            return out;
        }
        int dialogs = 0;
        QStringList refusals;
        host.setDialogHandler([&](const app::DialogRequest &request, LuaScriptHost::DialogReply reply) {
            const int n = dialogs++;
            if (n < answers.size())
                return reply(answer(request, answers[n].toObject()));
            for (const auto &c : request.controls)
                if (!c.label.empty() || !c.text.empty())
                    refusals << str(c.label.empty() ? c.text : c.label);
            reply(app::initialDialogResult(request));
        });
        const auto snapshot = app::snapshotForMacro(session);
        session.setReadOnly(true);
        if (!host.run(index, *snapshot) || !waitFor([&] { return outcome.has_value(); })) {
            ADD_FAILURE() << "the run did not finish";
            return out;
        }
        session.setReadOnly(false);
        const bool ok = *outcome == LuaScriptHost::RunOutcome::Ok;
        out.record.insert("outcome", ok ? QStringLiteral("ok") : QStringLiteral("stopped"));
        if (!log.isEmpty())
            out.record.insert("log", QJsonArray::fromStringList(log));
        if (!refusals.isEmpty())
            out.record.insert("refusal", refusals.join(QLatin1Char('\n')));
        if (!ok && !message.isEmpty())
            out.record.insert("message", QString(message).replace(QStringLiteral(HIKARI_AUTOMATION_STAGE_DIR), QStringLiteral("<stage>"))
                                             .section(QLatin1Char('\n'), 0, 1));
        if (ok && host.lastResult()) {
            const auto steps = session.historySize();
            EXPECT_TRUE(app::applyMacroResult(session, *snapshot, *host.lastResult(), macroName.toStdString()));
            out.record.insert("undo_steps", int(session.historySize() - steps));
            const auto after = app::snapshotForMacro(session);
            QJsonArray changed;
            const QJsonArray input = events(*snapshot);
            int kept = 0;
            for (const auto &e : events(*after))
                input.contains(e) ? void(++kept) : changed.append(e);
            out.record.insert("unchanged", kept);
            out.record.insert("events", changed);
            QJsonArray selected;
            for (std::size_t i = 0; i < after->dialogues.size(); ++i)
                if (session.selection().selected.contains(hikari::core::LineId{after->dialogues[i].id}))
                    selected.append(int(i) + 1);
            out.record.insert("selected", selected);
            out.selection = session.selection();
        }
        return out;
    }

    QJsonObject run(const QJsonObject &spec, const QJsonObject &corpus)
    {
        freshAutomationFolder(spec.value("name").toString() + QString::number(++runs));
        auto session = corpusSession(corpus.value("document").toString());
        const auto before = app::snapshotForMacro(session);
        app::Selection selection;
        for (const auto &o : spec.value("selection").toArray())
            selection.selected.insert(hikari::core::LineId{before->dialogues.at(o.toInt() - 1).id});
        selection.active = *selection.selected.begin();
        session.setSelection(selection);
        QJsonObject record{{"apply", run(session, QStringLiteral("Aegisub-Motion/Apply"), spec.value("dialogs").toArray()).record}};
        if (const QJsonObject then = spec.value("then").toObject(); !then.isEmpty()) {
            app::Selection tracked;
            for (const auto &d : app::snapshotForMacro(session)->dialogues)
                if (std::ranges::contains(d.extra, std::string("a-mo"), &std::pair<std::string, std::string>::first))
                    tracked.selected.insert(hikari::core::LineId{d.id});
            tracked.active = *tracked.selected.begin();
            session.setSelection(tracked);
            record.insert("then", run(session, then.value("macro").toString(), {}).record);
        }
        return record;
    }

    QTemporaryDir work;
    QString automation;
    int runs = 0;
    NoClipboard clipboard;
    CorpusVideo video;
    app::AutomationServiceRouter router;
};


TEST_F(MotionParity, AegisubMotionMatchesItsGoldens)
{
    const QJsonObject corpus = QJsonDocument::fromJson(readFile(corpusDir() + QStringLiteral("/cases.json"))).object();
    const bool update = qEnvironmentVariableIsSet("HIKARI_UPDATE_MOTION_GOLDENS");
    const QJsonArray cases = corpus.value("cases").toArray();
    ASSERT_FALSE(cases.isEmpty());
    for (const auto &value : cases) {
        const QJsonObject spec = value.toObject();
        const QString name = spec.value("name").toString();
        const QString path = corpusDir() + QStringLiteral("/amo/") + name + QStringLiteral(".json");
        const bool unstable = spec.contains(QStringLiteral("unstable"));
        if (!update) {
            if (unstable)
                continue;
            const QByteArray actual = QJsonDocument(run(spec, corpus)).toJson();
            EXPECT_EQ(QJsonDocument::fromJson(readFile(path)).toJson().toStdString(), actual.toStdString())
                << name.toStdString();
            continue;
        }
        QJsonObject golden = run(spec, corpus);
        if (unstable) {
            QJsonArray variants{golden};
            for (int i = 1; i < 40; ++i)
                if (const QJsonObject again = run(spec, corpus); !variants.contains(again))
                    variants.append(again);
            golden = {{"unstable", spec.value("unstable")}, {"variants", variants}};
        }
        QDir().mkpath(corpusDir() + QStringLiteral("/amo"));
        QFile out(path);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly)) << path.toStdString();
        out.write(QJsonDocument(golden).toJson());
    }
}

}
