// L6 (A33-compat): representative third-party automation scripts, unchanged.
//
// The corpus is tests/fixtures/automation-thirdparty/manifest.json: projects
// pinned to a commit with their licences and every file's sha256. Projects
// that may be redistributed are committed there; the others are fetched at
// test time. stage.cmake (the ctest fixture automation_thirdparty) verifies
// and stages them as a user installs them: Autoload/<script>, and an Include
// with the bundled modules (DependencyControl's native ones built) plus the
// projects' own. Offline, a fetched project is unavailable and its scripts are
// skipped with the reason.
//
// Each script loads in its own helper as Autoload loads it (registration,
// script_* globals, load errors), and the macros in runs.json run on
// fixture.ass through the host: snapshot, run (dialogs answered here),
// apply as one undo step. What they did is written to artifacts and compared
// with the reviewed results in expected.json. The capture probe then runs the
// same corpus and macros in the helper the way it runs in the legacy app
// (tools/legacy-capture), for compare_automation.py.
#include "hikari/application/automation_services.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/core/ass_load.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <optional>

using hikari::backends::LuaScriptHost;
namespace app = hikari::application;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QJsonDocument readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll());
}

void writeArtifact(const QString &name, const QJsonDocument &doc)
{
    QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
    QFile out(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/") + name);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly)) << name.toStdString();
    out.write(doc.toJson());
}

QString fixtureDir() { return QStringLiteral(HIKARI_THIRDPARTY_DIR); }
QString stageDir() { return QStringLiteral(HIKARI_THIRDPARTY_STAGE); }

struct Script {
    QString project, file;
    bool available = true;
    QString reason; // why the project is unavailable
};

// The manifest's scripts in order, with the staged projects' availability.
std::vector<Script> corpus()
{
    std::map<QString, QString> unavailable;
    for (const auto &v : readJson(stageDir() + QStringLiteral("/status.json")).array()) {
        const QJsonObject s = v.toObject();
        if (!s.value(QStringLiteral("available")).toBool())
            unavailable[s.value(QStringLiteral("id")).toString()] = s.value(QStringLiteral("reason")).toString();
    }
    std::vector<Script> out;
    for (const auto &p : readJson(fixtureDir() + QStringLiteral("/manifest.json")).object().value("projects").toArray()) {
        const QString id = p.toObject().value(QStringLiteral("id")).toString();
        for (const auto &f : p.toObject().value(QStringLiteral("files")).toArray()) {
            if (f.toObject().value(QStringLiteral("role")).toString() != QStringLiteral("script"))
                continue;
            Script s{id, f.toObject().value(QStringLiteral("install")).toString().section(QLatin1Char('/'), -1)};
            if (const auto it = unavailable.find(id); it != unavailable.end()) {
                s.available = false;
                s.reason = it->second;
            }
            out.push_back(s);
        }
    }
    return out;
}

// A clipboard and a deterministic text measure (no fonts in this test):
// width = size/2 per code point, height = size, descent = size/5.
struct MemoryClipboard : app::ClipboardPort {
    std::string value;
    std::string text() const override { return value; }
    bool setText(const std::string &t) override
    {
        value = t;
        return true;
    }
};

struct FixedMeasure : app::TextMeasurePort {
    std::optional<app::TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const double size = style.size() > 2 ? std::atof(style[2].c_str()) : 20;
        double points = 0;
        for (unsigned char c : text)
            points += (c & 0xC0) != 0x80;
        return app::TextExtents{points * size / 2, size, size / 5, 0};
    }
};

class ThirdPartyCorpus : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "automation_thirdparty_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    void SetUp() override
    {
        ASSERT_TRUE(QFile::exists(stageDir() + QStringLiteral("/status.json")))
            << "the corpus is not staged: run the automation-thirdparty-stage test (ctest fixture)";
        ASSERT_TRUE(work.isValid());
        // ?user and ?data resolve to an Automation directory beside the
        // scripts, as the package lays it out; DependencyControl's updater is
        // off so nothing here depends on the network or on live feeds.
        automation = work.filePath(QStringLiteral("Automation"));
        for (const char *sub : {"log", "autosave", "temp", "config"})
            QDir().mkpath(automation + QLatin1Char('/') + QLatin1String(sub));
        QFile cfg(automation + QStringLiteral("/config/l0.DependencyControl.json"));
        ASSERT_TRUE(cfg.open(QIODevice::WriteOnly));
        cfg.write(R"({"config": {"updaterEnabled": false}})");
        cfg.close();
        router.setClipboard(&clipboard);
        router.setTextMeasure(&measure);
        router.setPathContext([this] {
            app::AutomationPathContext paths;
            paths.automationDir = automation.toStdString();
            paths.dictionaryDir = work.filePath(QStringLiteral("Dictionary")).toStdString();
#ifdef _WIN32
            paths.windows = true;
#endif
            return paths;
        });
    }

    std::unique_ptr<LuaScriptHost> load(const QString &path)
    {
        auto host = std::make_unique<LuaScriptHost>(QStringLiteral(HIKARI_LUA_HELPER), path,
                                                    stageDir() + QStringLiteral("/Include"));
        host->setServiceHandler([this](const app::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            router.handle(r, std::move(reply));
        });
        QObject::connect(host.get(), &LuaScriptHost::logged, [this](const QString &t) { log << t; });
        QObject::connect(host.get(), &LuaScriptHost::finished, [this](LuaScriptHost::RunOutcome o, const QString &m) {
            outcome = o;
            message = m;
        });
        host->load();
        waitFor([&] { return host->state() != LuaScriptHost::State::Loading; });
        return host;
    }

    // Paths in messages name this run's directories; the pinned form does not.
    QString normalized(QString text) const
    {
        for (const QString &dir : {stageDir(), automation, fixtureDir()}) {
            text.replace(dir, QStringLiteral("<stage>"));
            text.replace(QDir::toNativeSeparators(dir), QStringLiteral("<stage>"));
        }
#ifdef _WIN32
        text.replace(QLatin1Char('\\'), QLatin1Char('/'));
#endif
        // A chunk name (Lua shortens a long one to its start, so it depends
        // on where the build is): [string "/home/..."].
        static const QRegularExpression chunk(QStringLiteral(R"(\[string "[^"]*"\])"));
        text.replace(chunk, QStringLiteral(R"([string "<chunk>"])"));
        return text;
    }

    QTemporaryDir work;
    QString automation;
    MemoryClipboard clipboard;
    FixedMeasure measure;
    app::AutomationServiceRouter router;
    QStringList log;
    std::optional<LuaScriptHost::RunOutcome> outcome;
    QString message;
};

// The pinned result for one entry, or nothing when this platform's result is
// recorded but not pinned (not yet observed there).
std::optional<QJsonValue> expected(const QJsonObject &all, const QString &section, const QString &key)
{
#ifdef _WIN32
    // Results not yet observed on a platform are recorded there, not pinned.
    if (all.value(QStringLiteral("unobserved_platforms")).toArray().contains(QStringLiteral("windows")))
        return std::nullopt;
    const QJsonValue platform = all.value(section + QStringLiteral("_windows")).toObject().value(key);
#else
    const QJsonValue platform = all.value(section + QStringLiteral("_linux")).toObject().value(key);
#endif
    if (platform.isString() && platform.toString() == QStringLiteral("unobserved"))
        return std::nullopt;
    if (!platform.isUndefined())
        return platform;
    return all.value(section).toObject().value(key);
}

QString compact(const QJsonValue &v)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact));
}

void compare(const QJsonObject &all, const QString &section, const QString &key, QJsonObject actual)
{
    actual.remove(QStringLiteral("run")); // the key itself
    const auto want = expected(all, section, key);
    if (!want) {
        std::fprintf(stderr, "%s %s: recorded, not pinned on this platform\n", qPrintable(section), qPrintable(key));
        return;
    }
    // An entry whose details depend on the network (expected.json
    // "loaded_only": the reason) pins only whether it loaded.
    const QJsonValue loadedOnly = all.value(QStringLiteral("loaded_only")).toObject().value(key);
    if (loadedOnly.isString()) {
        std::fprintf(stderr, "%s %s: only its load result is pinned: %s\n", qPrintable(section), qPrintable(key),
                     qPrintable(loadedOnly.toString()));
        EXPECT_EQ(want->toObject().value("loaded"), actual.value("loaded")) << key.toStdString();
        return;
    }
    // A script that registers macros in pairs() order (expected.json
    // "macros_unordered": the reason) pins them by name, not by order.
    QJsonValue pinned = *want;
    if (all.value(QStringLiteral("macros_unordered")).toObject().value(key).isString()) {
        const auto byName = [](QJsonObject o) {
            QJsonArray macros = o.value("macros").toArray();
            std::vector<QJsonValue> v(macros.begin(), macros.end());
            std::ranges::sort(v, {}, [](const QJsonValue &m) { return m.toObject().value("name").toString(); });
            o.insert("macros", QJsonArray::fromVariantList([&] {
                         QVariantList l;
                         for (const auto &m : v)
                             l << m.toVariant();
                         return l;
                     }()));
            return o;
        };
        pinned = byName(want->toObject());
        actual = byName(actual);
    }
    EXPECT_EQ(compact(pinned).toStdString(), compact(actual).toStdString())
        << section.toStdString() << " " << key.toStdString();
}

QJsonObject expectations() { return readJson(fixtureDir() + QStringLiteral("/expected.json")).object(); }

TEST_F(ThirdPartyCorpus, ScriptsLoadAsAutoloadDoes)
{
    const auto scripts = corpus();
    ASSERT_FALSE(scripts.empty());
    const QJsonObject all = expectations();
    QJsonObject artifact;
    for (const Script &s : scripts) {
        if (!s.available) {
            std::fprintf(stderr, "skipped %s (%s): %s\n", qPrintable(s.file), qPrintable(s.project), qPrintable(s.reason));
            artifact.insert(s.file, QJsonObject{{"skipped", s.reason}});
            continue;
        }
        auto host = load(stageDir() + QStringLiteral("/Autoload/") + s.file);
        const bool loaded = host->state() == LuaScriptHost::State::Ready;
        ASSERT_TRUE(loaded || host->state() == LuaScriptHost::State::LoadFailed) << s.file.toStdString();
        QJsonObject record{{"project", s.project}, {"loaded", loaded}};
        if (loaded) {
            const auto &info = host->info();
            record.insert("name", QString::fromStdString(info.name));
            record.insert("description", QString::fromStdString(info.description));
            record.insert("author", QString::fromStdString(info.author));
            record.insert("version", QString::fromStdString(info.version));
            QJsonArray macros;
            for (const auto &m : info.macros)
                macros.append(QJsonObject{{"name", QString::fromStdString(m.name)},
                                          {"description", QString::fromStdString(m.description)},
                                          {"validate", m.hasValidate},
                                          {"is_active", m.hasIsActive}});
            record.insert("macros", macros);
        } else {
            // The message up to its traceback.
            record.insert("error", normalized(host->lastError()).section(QStringLiteral("\nstack traceback:"), 0, 0));
        }
        std::fprintf(stderr, "corpus %s: %s\n", qPrintable(s.file),
                     loaded ? qPrintable(QStringLiteral("loaded, %1 macros").arg(host->info().macros.size()))
                            : qPrintable(record.value("error").toString().section(QLatin1Char('\n'), 0, 1)));
        artifact.insert(s.file, record);
        compare(all, QStringLiteral("load"), s.file, record);
    }
    writeArtifact(QStringLiteral("automation-thirdparty-corpus.json"), QJsonDocument(artifact));
}

TEST_F(ThirdPartyCorpus, FetchedProjectsAreAvailable)
{
    for (const Script &s : corpus())
        if (!s.available)
            GTEST_SKIP() << s.project.toStdString() << " is fetched at test time and is unavailable: "
                         << s.reason.toStdString();
}

hikari::application::EditSession fixtureSession()
{
    QFile f(fixtureDir() + QStringLiteral("/fixture.ass"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    std::vector<std::byte> bytes(data.size());
    std::memcpy(bytes.data(), data.data(), bytes.size());
    return app::EditSession(hikari::core::loadAss(bytes).document);
}

QString str(const std::string &s) { return QString::fromStdString(s); }

// The Document's events as the probe dumps them (event ordinals from 1).
QJsonArray events(const app::MacroSnapshot &s)
{
    QJsonArray out;
    for (const auto &d : s.dialogues)
        out.append(QJsonObject{{"comment", d.comment}, {"layer", d.layer}, {"start_time", double(d.startMs)},
                               {"end_time", double(d.endMs)}, {"style", str(d.style)}, {"actor", str(d.actor)},
                               {"margin_l", d.marginL}, {"margin_r", d.marginR}, {"margin_t", d.marginV},
                               {"effect", str(d.effect)}, {"text", str(d.text)}});
    return out;
}

// A dialog answered from the run's entry: the button by its label (OK and
// Cancel for the default pair) and the named controls' values; the other
// controls keep their initial values.
app::DialogResult answer(const app::DialogRequest &request, const QJsonObject &entry)
{
    app::DialogResult result = app::initialDialogResult(request);
    const QString button = entry.value("button").toString();
    const std::vector<std::string> labels =
        request.buttons.empty() ? std::vector<std::string>{"OK", "Cancel"} : request.buttons;
    for (std::size_t i = 0; i < labels.size(); ++i)
        if (str(labels[i]) == button)
            result.pressed = int(i);
    EXPECT_GE(result.pressed, 0) << "no button " << button.toStdString();
    const QJsonObject values = entry.value("values").toObject();
    for (std::size_t i = 0; i < request.controls.size(); ++i) {
        const QJsonValue v = values.value(str(request.controls[i].name));
        if (v.isUndefined() || request.controls[i].name.empty())
            continue;
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

const char *outcomeName(LuaScriptHost::RunOutcome o)
{
    switch (o) {
    case LuaScriptHost::RunOutcome::Ok: return "ok";
    case LuaScriptHost::RunOutcome::Failed: return "failed";
    case LuaScriptHost::RunOutcome::Cancelled: return "cancelled";
    case LuaScriptHost::RunOutcome::HelperLost: return "helper lost";
    case LuaScriptHost::RunOutcome::ForceStopped: return "force stopped";
    }
    return "?";
}

TEST_F(ThirdPartyCorpus, MacrosApplyToTheFixtureDocument)
{
    std::map<QString, Script> byFile;
    for (const Script &s : corpus())
        byFile[s.file] = s;
    const QJsonObject all = expectations();
    QJsonArray artifact;
    const QJsonArray runs = readJson(fixtureDir() + QStringLiteral("/runs.json")).object().value("runs").toArray();
    ASSERT_FALSE(runs.isEmpty());
    for (const auto &value : runs) {
        const QJsonObject spec = value.toObject();
        const QString file = spec.value("script").toString(), macroName = spec.value("macro").toString();
        const QString key = file + QStringLiteral(" | ") + macroName;
        ASSERT_TRUE(byFile.contains(file)) << file.toStdString();
        if (!byFile[file].available) {
            std::fprintf(stderr, "skipped %s: %s\n", qPrintable(key), qPrintable(byFile[file].reason));
            artifact.append(QJsonObject{{"run", key}, {"skipped", byFile[file].reason}});
            continue;
        }
        auto session = fixtureSession();
        const auto before = app::snapshotForMacro(session);
        ASSERT_TRUE(before);
        // Event ordinals to Lines.
        const auto lineAt = [&](int ordinal) { return hikari::core::LineId{before->dialogues.at(ordinal - 1).id}; };
        app::Selection selection;
        for (const auto &o : spec.value("selection").toArray())
            selection.selected.insert(lineAt(o.toInt()));
        selection.active = lineAt(spec.value("active").toInt());
        session.setSelection(selection);

        QJsonObject record{{"run", key}};
        auto host = load(stageDir() + QStringLiteral("/Autoload/") + file);
        if (host->state() != LuaScriptHost::State::Ready) {
            record.insert("error", normalized(host->lastError()).section(QLatin1Char('\n'), 0, 0));
            artifact.append(record);
            compare(all, QStringLiteral("runs"), key, record);
            continue;
        }
        int index = -1;
        for (std::size_t i = 0; i < host->info().macros.size(); ++i)
            if (str(host->info().macros[i].name) == macroName)
                index = int(i);
        ASSERT_GE(index, 0) << key.toStdString() << ": no such macro";
        QJsonArray dialogs;
        const QJsonArray answers = spec.value("dialogs").toArray();
        host->setDialogHandler([&](const app::DialogRequest &request, LuaScriptHost::DialogReply reply) {
            QJsonArray controls, buttons;
            for (const auto &c : request.controls)
                controls.append(str(c.kind) + QLatin1Char(':') + str(c.name));
            for (const auto &b : request.buttons)
                buttons.append(str(b));
            dialogs.append(QJsonObject{{"controls", controls}, {"buttons", buttons}});
            const int n = int(dialogs.size()) - 1;
            EXPECT_LT(n, answers.size()) << key.toStdString() << ": unexpected dialog";
            reply(n < answers.size() ? answer(request, answers[n].toObject()) : app::DialogResult{});
        });
        const auto snapshot = app::snapshotForMacro(session);
        ASSERT_TRUE(snapshot);
        log.clear();
        outcome.reset();
        session.setReadOnly(true);
        ASSERT_TRUE(host->run(index, *snapshot));
        ASSERT_TRUE(waitFor([&] { return outcome.has_value(); })) << key.toStdString();
        session.setReadOnly(false);
        record.insert("outcome", QString::fromLatin1(outcomeName(*outcome)));
        if (*outcome != LuaScriptHost::RunOutcome::Ok)
            record.insert("message", normalized(message));
        record.insert("log", QJsonArray::fromStringList(log));
        record.insert("dialogs", dialogs);
        if (*outcome == LuaScriptHost::RunOutcome::Ok && host->lastResult()) {
            const auto steps = session.historySize();
            const auto applied = app::applyMacroResult(session, *snapshot, *host->lastResult(), macroName.toStdString());
            record.insert("applied", bool(applied));
            record.insert("undo_steps", int(session.historySize() - steps));
            const auto after = app::snapshotForMacro(session);
            ASSERT_TRUE(after);
            record.insert("events", events(*after));
            // The selection afterwards, as event ordinals.
            QJsonArray selected;
            int active = 0;
            for (std::size_t i = 0; i < after->dialogues.size(); ++i) {
                const hikari::core::LineId id{after->dialogues[i].id};
                if (session.selection().selected.contains(id))
                    selected.append(int(i) + 1);
                if (session.selection().active == id)
                    active = int(i) + 1;
            }
            record.insert("selected", selected);
            record.insert("active", active);
            // One undo step brings the fixture back.
            if (session.historySize() > steps) {
                EXPECT_TRUE(session.undo());
                EXPECT_EQ(events(*app::snapshotForMacro(session)), events(*before)) << key.toStdString();
            }
        }
        std::fprintf(stderr, "run %s: %s\n", qPrintable(key), qPrintable(record.value("outcome").toString()));
        artifact.append(record);
        compare(all, QStringLiteral("runs"), key, record);
    }
    writeArtifact(QStringLiteral("automation-thirdparty-runs.json"), QJsonDocument(artifact));
}

// The capture probe (tools/legacy-capture/automation) over the same corpus
// and runs, as the legacy capture takes it: once while the host loads the
// probe, then from its corpus macro on fixture.ass. Its records are the
// rewrite side of compare_automation.py.
TEST_F(ThirdPartyCorpus, CaptureProbeRunsTheCorpus)
{
    const QString list = work.filePath(QStringLiteral("corpus.txt"));
    const QString output = work.filePath(QStringLiteral("capture.jsonl"));
    int count = 0;
    {
        QFile f(list);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        for (const Script &s : corpus())
            if (s.available) {
                f.write((stageDir() + QStringLiteral("/Autoload/") + s.file + QLatin1Char('\n')).toUtf8());
                ++count;
            }
    }
    qputenv("HIKARI_CAPTURE_OUT", output.toLocal8Bit());
    qputenv("HIKARI_CAPTURE_CORPUS", list.toLocal8Bit());
    qputenv("HIKARI_CAPTURE_RUNS", (fixtureDir() + QStringLiteral("/runs.json")).toLocal8Bit());
    qputenv("HIKARI_CAPTURE_AT_LOAD", "1");
    auto host = load(QStringLiteral(HIKARI_CAPTURE_PROBE));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    int index = -1;
    for (std::size_t i = 0; i < host->info().macros.size(); ++i)
        if (host->info().macros[i].name.ends_with(" corpus"))
            index = int(i);
    ASSERT_GE(index, 0);
    auto session = fixtureSession();
    const auto snapshot = app::snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    ASSERT_TRUE(host->run(index, *snapshot));
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }, 120'000));
    EXPECT_EQ(outcome, LuaScriptHost::RunOutcome::Ok) << message.toStdString();
    for (const char *name : {"HIKARI_CAPTURE_OUT", "HIKARI_CAPTURE_CORPUS", "HIKARI_CAPTURE_RUNS", "HIKARI_CAPTURE_AT_LOAD"})
        qunsetenv(name);
    QFile f(output);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray atLoad = f.readLine(), inMacro = f.readLine();
    for (const auto &[line, name] : {std::pair{atLoad, "automation-capture-thirdparty.json"},
                                     std::pair{inMacro, "automation-capture-thirdparty-macro.json"}}) {
        const QJsonObject record = QJsonDocument::fromJson(line).object();
        ASSERT_EQ(record.value("case").toString(), QStringLiteral("corpus")) << line.toStdString();
        ASSERT_FALSE(record.contains("error")) << line.toStdString();
        EXPECT_EQ(record.value("scripts").toArray().size(), count);
        writeArtifact(QString::fromLatin1(name), QJsonDocument(record));
    }
    // Every run the macro-time capture made, against the pinned result of the
    // host's own run of that macro (the probe answers dialogs itself, the host
    // run through the dialog handler): the same document afterwards.
    const QJsonObject all = expectations();
    for (const auto &script : QJsonDocument::fromJson(inMacro).object().value("scripts").toArray())
        for (const auto &r : script.toObject().value("runs").toArray()) {
            const QString key = script.toObject().value("file").toString() + QStringLiteral(" | ") +
                                r.toObject().value("macro").toString();
            const auto pinned = expected(all, QStringLiteral("runs"), key);
            if (!pinned || !pinned->toObject().contains("events"))
                continue;
            EXPECT_EQ(compact(r.toObject().value("document").toObject().value("events")),
                      compact(pinned->toObject().value("events")))
                << key.toStdString() << ": the probe's run and the host's differ";
        }
}

} // namespace
