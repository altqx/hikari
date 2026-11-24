#include "hikari/app/plugin_test_harness.h"

#include "hikari/api/api.h"
#include "hikari/app/application.h"
#include "hikari/app/plugin_manager.h"
#include "hikari/app/plugin_popups.h"
#include "hikari/app/plugin_runtime.h"
#include "hikari/app/plugin_ui.h"
#include "hikari/application/ai_providers.h"
#include "hikari/backends/plugin_process.h"
#include "hikari/core/ass_save.h"
#include "plugin_variant.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::app {

namespace {

using api::Json;
using backends::PluginProcess;

constexpr const char *kStepKinds[] = {"run", "event", "call", "complete", "hover", "export", "expectDocument", "answer"};

class NoKeys final : public application::ai::KeyStore {
public:
    void read(const std::string &, std::function<void(application::ai::KeyResult)> done) override
    {
        done(application::ai::KeyError::NotFound);
    }
    void write(const std::string &, const std::string &, std::function<void(std::optional<application::ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
    void remove(const std::string &, std::function<void(std::optional<application::ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
};

class NoProviders final : public application::ai::ConfigStore {
public:
    application::ai::Config load() override { return {}; }
    bool save(const application::ai::Config &) override { return true; }
};

QString qs(const std::string &text)
{
    return QString::fromStdString(text);
}

std::string str(const QString &text)
{
    return text.toStdString();
}

std::optional<QByteArray> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

QByteArray comparable(QByteArray text)
{
    if (text.startsWith("\xEF\xBB\xBF"))
        text.remove(0, 3);
    return text.replace("\r\n", "\n");
}

// The first line where the two texts differ, as "line <n>: <actual>, expected <expected>".
std::string firstDifference(const QByteArray &expected, const QByteArray &actual)
{
    const QList<QByteArray> want = expected.split('\n');
    const QList<QByteArray> got = actual.split('\n');
    for (qsizetype i = 0; i < std::max(want.size(), got.size()); ++i) {
        const QByteArray w = i < want.size() ? want[i] : QByteArray("(nothing)");
        const QByteArray g = i < got.size() ? got[i] : QByteArray("(nothing)");
        if (w != g)
            return "line " + std::to_string(i + 1) + ": \"" + g.toStdString() + "\", expected \"" + w.toStdString() + "\"";
    }
    return "they differ";
}

// Runs the event loop until `done` is true or `ms` have passed; false on a timeout.
template <typename Done>
bool waitUntil(Done done, int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        const qint64 left = ms - clock.elapsed();
        if (left <= 0)
            return false;
        QEventLoop loop;
        QTimer::singleShot(int(std::min<qint64>(left, 10)), &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

struct Step {
    int line = 0;
    std::string kind;
    Json json;
};

struct Failure {
    int line = 0;
    std::string kind;
    std::string message;
};

class Harness {
public:
    Harness(const application::PluginTestArguments &arguments, std::FILE *out) : m_args(arguments), m_out(out) {}

    int run()
    {
        if (!m_args.problem.empty())
            return setupError(m_args.problem);
        if (auto problem = readScript(); !problem.empty())
            return setupError(problem);
        const QFileInfo folder(qs(m_args.folder));
        if (!folder.isDir())
            return setupError("there is no plugin folder at " + m_args.folder);
        if (!m_args.document.empty() && !QFileInfo(qs(m_args.document)).isFile())
            return setupError("there is no fixture Document at " + m_args.document);
        if (!m_root.isValid())
            return setupError("couldn't make a temporary folder: " + str(m_root.errorString()));

        Application application(options());
        m_app = &application;
        if (auto problem = open(); !problem.empty())
            return setupError(problem);
        if (auto problem = install(folder.absoluteFilePath()); !problem.empty())
            return setupError(problem);
        answerPrompts();
        for (const Step &step : m_steps)
            runStep(step);
        m_app = nullptr;
        return report();
    }

private:
    std::string readScript()
    {
        QFile file(qs(m_args.script));
        if (!file.open(QIODevice::ReadOnly))
            return "there is no tests file at " + m_args.script;
        m_scriptDir = QFileInfo(file).absoluteDir();
        int number = 0;
        while (!file.atEnd()) {
            ++number;
            const QByteArray line = file.readLine().trimmed();
            if (line.isEmpty())
                continue;
            const std::string where = QFileInfo(file).fileName().toStdString() + ":" + std::to_string(number) + ": ";
            auto json = Json::parse(line.toStdString());
            if (!json || !json->isObject())
                return where + "a step must be one JSON object";
            Step step{number, {}, std::move(*json)};
            for (const char *kind : kStepKinds)
                if (step.json.find(kind)) {
                    if (!step.kind.empty())
                        return where + "a step has one of run, event, call, complete, hover, export, expectDocument "
                                       "and answer";
                    step.kind = kind;
                }
            if (step.kind.empty())
                return where + "a step needs one of run, event, call, complete, hover, export, expectDocument and answer";
            m_steps.push_back(std::move(step));
        }
        return {};
    }

    Application::Options options() const
    {
        const auto at = [&](const QString &name) { return m_root.filePath(name); };
        Application::Options o;
        o.settingsFile = at(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        o.updateFeed = {};
        o.legacySettingsFolders = QStringList();
        o.legacyAutosaveDir = at(QStringLiteral("legacy"));
        o.presence.candidates = {at(QStringLiteral("no-discord"))};
        o.externalAgents = false;
        AiFeatures::Ports ai;
        ai.keyStore = [] { return std::make_unique<NoKeys>(); };
        ai.configStore = [] { return std::make_unique<NoProviders>(); };
        o.aiPorts = std::move(ai);
        o.plugins = PluginManager::Folders{at(QStringLiteral("user/plugins")), at(QStringLiteral("user/plugin-data")),
                                           at(QStringLiteral("cache/plugins")), at(QStringLiteral("user/plugins.json")),
                                           at(QStringLiteral("user/plugin-programs"))};
        return o;
    }

    std::string open()
    {
        if (m_args.document.empty())
            return {};
        const QFileInfo fixture(qs(m_args.document));
        const QString copy = m_root.filePath(fixture.fileName());
        if (!QFile::copy(fixture.absoluteFilePath(), copy))
            return "couldn't copy " + m_args.document;
        QFile::setPermissions(copy, QFile::permissions(copy) | QFileDevice::WriteOwner);
        if (!m_app->openFile(copy))
            return "couldn't open " + m_args.document;
        return {};
    }

    std::string install(const QString &folder)
    {
        PluginManager &manager = *m_app->pluginManager();
        const QVariantMap review = manager.reviewDev(QUrl::fromLocalFile(folder));
        if (!review.value(QStringLiteral("ok")).toBool())
            return "the plugin can't be loaded: " + str(review.value(QStringLiteral("message")).toString());
        m_id = review.value(QStringLiteral("id")).toString();
        m_granted = review.value(QStringLiteral("permissions")).toStringList();
        if (const QString problem = manager.install(false); !problem.isEmpty())
            return "the plugin can't be installed: " + str(problem);
        PluginRuntime &runtime = *m_app->pluginRuntime();
        if (!waitUntil([&] { return runtime.process(m_id) != nullptr; }, m_args.stepTimeoutMs))
            return "the plugin didn't start";
        PluginProcess *process = runtime.process(m_id);
        if (process->state() == PluginProcess::State::Idle || process->state() == PluginProcess::State::Stopped)
            process->start();
        const auto settled = [&] {
            const auto state = runtime.process(m_id) ? runtime.process(m_id)->state() : PluginProcess::State::LoadFailed;
            return state == PluginProcess::State::Ready || state == PluginProcess::State::LoadFailed;
        };
        if (!waitUntil(settled, m_args.stepTimeoutMs))
            return "the plugin didn't load in " + std::to_string(m_args.stepTimeoutMs) + " ms";
        if (runtime.process(m_id)->state() == PluginProcess::State::LoadFailed)
            return "the plugin didn't load: " + str(runtime.process(m_id)->loadError());
        if (!m_args.json) {
            std::fprintf(m_out, "Testing %s %s (%s)\n", qPrintable(review.value(QStringLiteral("name")).toString()),
                         qPrintable(review.value(QStringLiteral("version")).toString()), qPrintable(m_id));
            if (!m_granted.isEmpty())
                std::fprintf(m_out, "Granted without asking:\n  %s\n", qPrintable(m_granted.join(QStringLiteral("\n  "))));
        }
        return {};
    }

    PluginProcess *process() const { return m_app->pluginRuntime()->process(m_id); }

    void answerPrompts()
    {
        PluginUi *ui = m_app->pluginRuntime()->ui();
        QObject::connect(ui, &PluginUi::dialogOpened, &m_context, [this, ui](PluginDialogState *dialog) {
            const QString key = dialog->key();
            QTimer::singleShot(0, &m_context, [this, ui, key] {
                const auto answer = nextAnswer("a dialog");
                if (!answer || answer->find("dismiss"))
                    return ui->dismiss(key);
                if (const Json *values = answer->find("values"); values && values->isObject())
                    for (const Json::Member &value : values->asObject())
                        ui->dialogEvent(key, {{QStringLiteral("kind"), QStringLiteral("change")},
                                              {QStringLiteral("widget"), qs(value.key)},
                                              {QStringLiteral("value"), toVariant(value.value)}});
                const Json *button = answer->find("button");
                ui->press(key, button && button->isString() ? qs(button->asString()) : QString());
            });
        });
        PluginPopups *popups = ui->popups();
        QObject::connect(popups, &PluginPopups::shownChanged, &m_context, [this, popups] {
            PluginPopup *shown = popups->shown();
            if (!shown)
                m_answered = nullptr;
            if (!shown || shown == m_answered)
                return;
            m_answered = shown;
            QTimer::singleShot(0, &m_context, [this, popups, shown] {
                if (popups->shown() != shown)
                    return;
                const auto answer = nextAnswer(shown->kind() == QLatin1String("input_box") ? "an input box" : "a quick pick");
                if (!answer || answer->find("dismiss"))
                    return shown->dismiss();
                if (answer->find("back"))
                    return shown->goBack();
                if (const Json *button = answer->find("button"); button && button->isString())
                    return shown->press(qs(button->asString()));
                if (const Json *text = answer->find("text"); text && text->isString())
                    shown->edit(qs(text->asString()));
                QStringList items;
                if (const Json *pick = answer->find("pick"); pick && pick->isArray())
                    for (const Json &item : pick->asArray())
                        if (item.isString())
                            items << qs(item.asString());
                shown->accept(items);
            });
        });
    }

    std::optional<Json> nextAnswer(const std::string &what)
    {
        if (m_answers.empty()) {
            fail(what + " opened with no answer step before it");
            return std::nullopt;
        }
        Json answer = std::move(m_answers.front());
        m_answers.pop_front();
        return answer;
    }

    void fail(std::string message) { m_failures.push_back({m_step ? m_step->line : 0, m_step ? m_step->kind : "", std::move(message)}); }

    template <typename Done>
    bool wait(Done done)
    {
        if (waitUntil(done, m_args.stepTimeoutMs))
            return true;
        fail("timed out after " + std::to_string(m_args.stepTimeoutMs) + " ms");
        if (PluginProcess *p = process())
            p->forceStop();
        return false;
    }

    void expectResult(const Json &actual)
    {
        if (const Json *error = m_step->json.find("error"))
            fail("succeeded with " + actual.dump() + ", expected the error " + error->dump());
        else if (const Json *expect = m_step->json.find("expect"))
            if (auto problem = api::mismatch(*expect, actual))
                fail(*problem);
    }

    void expectError(const std::string &code, const std::string &message)
    {
        const Json *error = m_step->json.find("error");
        if (!error)
            fail("failed: " + message);
        else if (error->isString() && error->asString() != code && message.find(error->asString()) == std::string::npos)
            fail("failed with \"" + message + "\", expected the error " + error->dump());
    }

    void runStep(const Step &step)
    {
        m_step = &step;
        if (step.kind == "answer")
            m_answers.push_back(*step.json.find("answer"));
        else if (step.kind == "run")
            runCommand();
        else if (step.kind == "event")
            pushEvent();
        else if (step.kind == "call")
            callOperation();
        else if (step.kind == "complete" || step.kind == "hover")
            askProvider();
        else if (step.kind == "export")
            exportFormat();
        else
            expectDocument();
        m_step = nullptr;
    }

    void runCommand()
    {
        const Json *command = m_step->json.find("run");
        const Json *args = m_step->json.find("args");
        PluginProcess *p = process();
        if (!command->isString() || !p)
            return fail(p ? "run needs a command id" : "the plugin isn't running");
        const Json call = Json::object({{"args", args && args->isObject() ? *args : Json::object()},
                                        {"context", Json::object({{"via", "palette"}})}});
        std::optional<std::expected<std::string, QString>> result;
        if (!p->run(command->asString(), call.dump(), [&](std::expected<std::string, QString> r) { result = std::move(r); }))
            return fail("the plugin didn't take the command");
        if (!wait([&] { return result.has_value(); }))
            return;
        if (!*result)
            return expectError({}, str(result->error()));
        auto actual = Json::parse(**result);
        expectResult(actual ? *actual : Json());
    }

    void pushEvent()
    {
        const Json *name = m_step->json.find("event");
        const Json *data = m_step->json.find("data");
        PluginProcess *p = process();
        if (!name->isString() || !p)
            return fail(p ? "event needs an event name" : "the plugin isn't running");
        bool ended = false;
        QObject context;
        QObject::connect(p, &PluginProcess::unitEnded, &context, [&] { ended = true; });
        QObject::connect(p, &PluginProcess::eventFailed, &context, [&](const QString &, const QString &message) {
            fail("the handler failed: " + str(message));
        });
        if (!p->push(name->asString(), (data ? *data : Json::object()).dump()))
            return fail("the plugin didn't take the event");
        wait([&] { return ended; });
    }

    void callOperation()
    {
        const Json *operation = m_step->json.find("call");
        const Json *params = m_step->json.find("params");
        if (!operation->isString())
            return fail("call needs an operation name");
        std::optional<api::Result> result;
        api::Caller caller{"plugin:" + str(m_id), str(m_id), true};
        m_app->apiHost()->api().call(caller, operation->asString(), params ? *params : Json::object(),
                                     [&](api::Result r) { result = std::move(r); });
        if (!wait([&] { return result.has_value(); }))
            return;
        if (!*result)
            return expectError((*result).error().code, (*result).error().code + ": " + (*result).error().message);
        expectResult(**result);
    }

    void askProvider()
    {
        const std::string &kind = m_step->kind;
        const Json *given = m_step->json.find(kind);
        if (!given->isObject())
            return fail(kind + " needs a request object");
        Json request = Json::object({{"document", Json(std::uint64_t(m_app->editingTargetId()))},
                                     {"line", Json(firstLine())},
                                     {"field", "text"},
                                     {"text", ""},
                                     {"offset", 0},
                                     {"trigger", Json()}});
        for (const Json::Member &m : given->asObject())
            request[m.key] = m.value;
        std::optional<std::expected<std::string, QString>> answer;
        const std::string provider = kind == "complete" ? "completion" : "hover";
        if (!m_app->pluginRuntime()->provide(str(m_id), provider, request,
                                             [&](std::expected<std::string, QString> a) { answer = std::move(a); }))
            return fail("the plugin has no " + provider + " provider");
        if (!wait([&] { return answer.has_value(); }))
            return;
        if (!*answer)
            return expectError({}, str(answer->error()));
        auto actual = Json::parse(**answer);
        expectResult(actual ? *actual : Json());
    }

    std::uint64_t firstLine()
    {
        const auto target = m_app->workspace().editingTarget();
        application::EditSession *session = target ? m_app->files().session(*target) : nullptr;
        if (!session || session->document().lines().empty())
            return 0;
        return session->document().lines().front()->id.value;
    }

    void exportFormat()
    {
        const Json *format = m_step->json.find("export");
        const Json *expected = m_step->json.find("expectFile");
        if (!format->isString())
            return fail("export needs a format id");
        std::optional<QVariantMap> result;
        QObject context;
        QObject::connect(m_app, &Application::pluginExportFinished, &context, [&](const QVariantMap &r) { result = r; });
        const QString formatId = QStringLiteral("plugin:%1/%2").arg(m_id, qs(format->asString()));
        const QVariantMap started =
            m_app->exportTo(QUrl::fromLocalFile(m_root.filePath(QStringLiteral("export-%1").arg(m_step->line))), formatId, {});
        if (!started.value(QStringLiteral("pending")).toBool())
            result = started;
        if (!wait([&] { return result.has_value(); }))
            return;
        if (!result->value(QStringLiteral("ok")).toBool())
            return fail("the export failed: " + str(result->value(QStringLiteral("problem")).toString()));
        if (!expected || !expected->isString())
            return;
        const auto want = readFile(m_scriptDir.absoluteFilePath(qs(expected->asString())));
        const auto got = readFile(result->value(QStringLiteral("file")).toUrl().toLocalFile());
        if (!want)
            return fail("there is no expected file at " + expected->asString());
        if (!got || *got != *want)
            fail("the file differs from " + expected->asString() + ": " + firstDifference(*want, got ? *got : QByteArray()));
    }

    void expectDocument()
    {
        const Json *fixture = m_step->json.find("expectDocument");
        if (!fixture->isString())
            return fail("expectDocument needs a fixture path");
        const auto want = readFile(m_scriptDir.absoluteFilePath(qs(fixture->asString())));
        if (!want)
            return fail("there is no fixture at " + fixture->asString());
        const auto target = m_app->workspace().editingTarget();
        application::EditSession *session = target ? m_app->files().session(*target) : nullptr;
        if (!session)
            return fail("no Document is open");
        const std::vector<std::byte> bytes = core::encodeAss(session->document());
        const QByteArray got(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
        if (comparable(got) != comparable(*want))
            fail("the Document differs from " + fixture->asString() + ": " + firstDifference(comparable(*want), comparable(got)));
    }

    int setupError(const std::string &problem)
    {
        if (m_args.json)
            std::fprintf(m_out, "%s\n", Json::object({{"ok", false}, {"error", problem}}).dump().c_str());
        else
            std::fprintf(m_out, "hikarisub --test-plugin: %s\n", problem.c_str());
        return 2;
    }

    int report()
    {
        const QString script = QFileInfo(qs(m_args.script)).fileName();
        if (m_args.json) {
            Json failed = Json::array();
            for (const Failure &f : m_failures)
                failed.push(Json::object({{"line", f.line}, {"step", f.kind}, {"message", f.message}}));
            Json granted = Json::array();
            for (const QString &line : m_granted)
                granted.push(str(line));
            std::fprintf(m_out, "%s\n",
                         Json::object({{"ok", m_failures.empty()},
                                       {"plugin", str(m_id)},
                                       {"granted", std::move(granted)},
                                       {"steps", int(m_steps.size())},
                                       {"failed", std::move(failed)}})
                             .dump()
                             .c_str());
        } else {
            for (const Failure &f : m_failures)
                std::fprintf(m_out, "%s:%d: %s: %s\n", qPrintable(script), f.line, f.kind.c_str(), f.message.c_str());
            std::fprintf(m_out, m_failures.empty() ? "All %zu steps passed.\n" : "%zu of %zu steps failed.\n",
                         m_failures.empty() ? m_steps.size() : failedSteps(), m_steps.size());
        }
        return m_failures.empty() ? 0 : 1;
    }

    std::size_t failedSteps() const
    {
        std::vector<int> lines;
        for (const Failure &f : m_failures)
            if (std::ranges::find(lines, f.line) == lines.end())
                lines.push_back(f.line);
        return lines.size();
    }

    const application::PluginTestArguments &m_args;
    std::FILE *m_out;
    QTemporaryDir m_root;
    QDir m_scriptDir;
    std::vector<Step> m_steps;
    Application *m_app = nullptr;
    QString m_id;
    QStringList m_granted;
    QObject m_context;
    const Step *m_step = nullptr;
    std::deque<Json> m_answers;
    PluginPopup *m_answered = nullptr;
    std::vector<Failure> m_failures;
};

}

int runPluginTests(const application::PluginTestArguments &arguments, std::FILE *out)
{
    return Harness(arguments, out).run();
}

}
