#include "hikari/app/plugin_programs.h"

#include "hikari/backends/program_process.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStringDecoder>
#include <QTimer>

#include <algorithm>
#include <charconv>

namespace hikari::app {

namespace errors = api::errors;
using backends::ProgramProcess;
using backends::sandbox::Network;

namespace {

constexpr qsizetype kMaxOutput = qsizetype(16) << 20;
constexpr std::size_t kMaxStarted = 8;
constexpr int kLogLinesPerMinute = 20;

QString qs(const std::string &text)
{
    return QString::fromStdString(text);
}

std::unexpected<api::Error> refusal(std::string_view code, std::string message, std::string reason)
{
    return api::fail(code, std::move(message), api::Json::object({{"reason", api::Json(std::move(reason))}}));
}

QString settingText(const api::Json &value)
{
    if (value.isString())
        return qs(value.asString());
    if (value.isBool())
        return value.asBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isInt())
        return QString::number(value.asInt());
    if (value.isNumber())
        return QString::number(value.asNumber());
    return {};
}

QString pathOf(const std::string &utf8)
{
    return QString::fromUtf8(utf8.data(), qsizetype(utf8.size()));
}

struct Expansion {
    const PluginPrograms::Plugin &plugin;
    const std::vector<api::ProgramFile> *files = nullptr;

    std::optional<QString> operator()(const std::string &text) const
    {
        QString out;
        std::size_t at = 0;
        while (at < text.size()) {
            const std::size_t open = text.find("${", at);
            const std::size_t close = open == std::string::npos ? open : text.find('}', open);
            if (close == std::string::npos) {
                out += qs(text.substr(at));
                break;
            }
            out += qs(text.substr(at, open - at));
            const std::string name = text.substr(open + 2, close - open - 2);
            const auto value = valueOf(name);
            if (!value)
                return std::nullopt;
            out += value->value_or(qs(text.substr(open, close - open + 1)));
            at = close + 1;
        }
        return out;
    }

    std::optional<std::optional<QString>> valueOf(const std::string &name) const
    {
        if (name == "package")
            return QDir::toNativeSeparators(plugin.package);
        if (name == "data")
            return QDir::toNativeSeparators(plugin.data);
        if (name == "cache")
            return QDir::toNativeSeparators(plugin.cache);
        if (name.starts_with("setting:")) {
            const api::Json *value = plugin.settings.isObject() ? plugin.settings.find(name.substr(8)) : nullptr;
            return value ? settingText(*value) : QString();
        }
        if (files && name.starts_with("file:")) {
            std::size_t index = 0;
            const char *first = name.data() + 5;
            const char *last = name.data() + name.size();
            const auto [end, error] = std::from_chars(first, last, index);
            if (error != std::errc() || end != last || index >= files->size())
                return std::nullopt;
            return QDir::toNativeSeparators(pathOf((*files)[index].path));
        }
        return std::optional<QString>();
    }
};

QString profileName(const std::string &pluginId, const std::string &programId)
{
    const QByteArray hash =
        QCryptographicHash::hash(QByteArray::fromStdString(pluginId + "/" + programId), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("HikariSub.Program.") + QString::fromLatin1(hash.left(32));
}

QProcessEnvironment baseEnvironment(const QString &temp)
{
    const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment env;
#ifdef Q_OS_WIN
    // Windows programs fail to load without SystemRoot. An AppContainer doesn't start without LOCALAPPDATA
    // (ERROR_ENVVAR_NOT_FOUND); Windows then points it, TEMP and TMP at the container's own folders.
    for (const QString &name : {QStringLiteral("PATH"), QStringLiteral("USERPROFILE"), QStringLiteral("LANG"), QStringLiteral("SystemRoot"),
                                QStringLiteral("LOCALAPPDATA")})
#else
    for (const QString &name : {QStringLiteral("PATH"), QStringLiteral("HOME"), QStringLiteral("LANG")})
#endif
        if (system.contains(name))
            env.insert(name, system.value(name));
    env.insert(QStringLiteral("TEMP"), temp);
#ifdef Q_OS_WIN
    env.insert(QStringLiteral("TMP"), temp);
#else
    env.insert(QStringLiteral("TMPDIR"), temp);
#endif
    return env;
}

}

struct PluginPrograms::Running {
    std::string plugin;
    std::string id;
    std::unique_ptr<ProgramProcess> process;
    bool base64 = false;
    QStringDecoder out{QStringDecoder::Utf8};
    QStringDecoder err{QStringDecoder::Utf8};
    QByteArray errLine;
    QByteArray stdoutBytes, stderrBytes;
    std::optional<api::Reply> reply;
    std::unique_ptr<QTimer> timeout;
    bool timedOut = false;
    bool tooLarge = false;
};

PluginPrograms::PluginPrograms(QObject *parent) : QObject(parent) {}

PluginPrograms::~PluginPrograms() = default;

std::expected<std::unique_ptr<ProgramProcess>, api::Error> PluginPrograms::launch(const std::string &pluginId,
                                                                                const api::ProgramCall &call) const
{
    const std::optional<Plugin> plugin = m_wiring.plugin ? m_wiring.plugin(pluginId) : std::nullopt;
    if (!plugin || !plugin->manifest)
        return api::fail(errors::kUnavailable, "The plugin isn't running.");
    const plugins::Program *program = plugin->manifest->program(call.program);
    if (!program)
        return api::fail(errors::kNotFound, "The plugin declares no program \xE2\x80\x9C" + call.program + "\xE2\x80\x9D.");
    const auto run = program->platforms.find(std::string(plugins::thisPlatform()));
    if (run == program->platforms.end())
        return refusal(errors::kUnavailable, program->title + " isn't available for this computer.", "platform");
    QString file;
    QString folder;
    if (const auto &download = run->second.download) {
        file = m_wiring.downloaded ? m_wiring.downloaded(pluginId, run->second) : QString();
        if (file.isEmpty())
            return refusal(errors::kUnavailable, program->title + " isn't downloaded yet.", "not_downloaded");
        QDir kept = QFileInfo(file).dir();
        for (std::size_t i = 0; i < std::ranges::count(download->entry, '/'); ++i)
            kept.cdUp();
        folder = kept.absolutePath();
    } else {
        file = QDir(plugin->package).absoluteFilePath(qs(run->second.path));
        if (!QFileInfo(file).isFile())
            return refusal(errors::kUnavailable, program->title + " is missing from the plugin's package.", "missing");
        folder = QFileInfo(file).absolutePath();
    }

    const bool networkAllowed = m_wiring.networkAllowed && m_wiring.networkAllowed();
    backends::sandbox::Profile profile;
    profile.name = profileName(pluginId, program->id);
    profile.network = program->network && networkAllowed ? Network::Any : Network::None;
#ifdef Q_OS_WIN
    if (program->confinement == plugins::Confinement::LowIntegrity) {
        if (!networkAllowed)
            return refusal(errors::kUnavailable,
                           program->title + " needs \xE2\x80\x9CLet plugins connect to the internet\xE2\x80\x9D on.", "network_off");
        profile.lowRightsHelper = m_wiring.lowRightsHelper;
    }
#endif
    profile.readExecute = {folder};
    profile.readWrite = {plugin->data, plugin->cache};
    for (const api::ProgramFile &granted : call.files)
        (granted.writable ? profile.readWrite : profile.read).push_back(pathOf(granted.path));

    const Expansion declared{*plugin};
    const Expansion called{*plugin, &call.files};
    QStringList arguments;
    for (const std::string &arg : run->second.args)
        arguments << declared(arg).value_or(QString());
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const auto arg = called(call.args[i]);
        if (!arg)
            return api::fail(errors::kInvalidParams, "args names a file that files doesn't hold.",
                             api::Json::object({{"pointer", api::Json("/args/" + std::to_string(i))}}));
        arguments << *arg;
    }
    const QString temp = QDir(plugin->cache).filePath(QStringLiteral("tmp"));
    QDir().mkpath(plugin->data);
    QDir().mkpath(temp);
    QProcessEnvironment environment = baseEnvironment(QDir::toNativeSeparators(temp));
    for (const auto &[name, value] : run->second.env)
        environment.insert(qs(name), declared(value).value_or(QString()));

    return std::make_unique<ProgramProcess>(
        ProgramProcess::Options{file, arguments, environment, plugin->data, std::move(profile)});
}

std::expected<std::unique_ptr<ProgramProcess>, api::Error> PluginPrograms::prepare(const std::string &pluginId,
                                                                                 const std::string &programId) const
{
    api::ProgramCall call;
    call.program = programId;
    return launch(pluginId, call);
}

void PluginPrograms::run(const api::Caller &caller, api::ProgramCall call, api::Reply reply)
{
    const std::string pluginId = caller.id.substr(std::string_view("plugin:").size());
    auto process = launch(pluginId, call);
    if (!process)
        return reply(std::unexpected(process.error()));
    const std::uint64_t key = m_next++;
    auto running = std::make_unique<Running>();
    Running &r = *running;
    r.plugin = pluginId;
    r.process = std::move(*process);
    r.base64 = call.base64;
    r.reply = std::move(reply);
    connect(r.process.get(), &ProgramProcess::output, this, [this, key](const QByteArray &bytes) { onOutput(key, false, bytes); });
    connect(r.process.get(), &ProgramProcess::errorOutput, this, [this, key](const QByteArray &bytes) { onOutput(key, true, bytes); });
    connect(r.process.get(), &ProgramProcess::finished, this,
            [this, key](int exitCode, bool killed) { onFinished(key, exitCode, killed); }, Qt::QueuedConnection);
    m_running.emplace(key, std::move(running));
    if (!r.process->start()) {
        const api::Reply refuse = std::move(*r.reply);
        const QString why = r.process->refusal();
        m_running.erase(key);
        return refuse(refusal(errors::kUnavailable, "The sandbox can't apply here: " + why.toStdString(), "sandbox"));
    }
    r.timeout = std::make_unique<QTimer>();
    r.timeout->setSingleShot(true);
    connect(r.timeout.get(), &QTimer::timeout, this, [this, key] {
        if (const auto it = m_running.find(key); it != m_running.end()) {
            it->second->timedOut = true;
            it->second->process->kill();
        }
    });
    r.timeout->start(call.timeoutMs);
    r.process->write(QByteArray::fromStdString(call.input));
    r.process->closeInput();
}

api::Result PluginPrograms::start(const api::Caller &caller, api::ProgramCall call)
{
    const std::string pluginId = caller.id.substr(std::string_view("plugin:").size());
    if (started(pluginId) >= kMaxStarted)
        return refusal(errors::kRefused, "A plugin keeps at most 8 programs running.", "too_many");
    auto process = launch(pluginId, call);
    if (!process)
        return std::unexpected(process.error());
    const std::uint64_t key = m_next++;
    auto running = std::make_unique<Running>();
    Running &r = *running;
    r.plugin = pluginId;
    r.id = "p" + std::to_string(key);
    r.process = std::move(*process);
    r.base64 = call.base64;
    connect(r.process.get(), &ProgramProcess::output, this, [this, key](const QByteArray &bytes) { onOutput(key, false, bytes); });
    connect(r.process.get(), &ProgramProcess::errorOutput, this, [this, key](const QByteArray &bytes) { onOutput(key, true, bytes); });
    connect(r.process.get(), &ProgramProcess::finished, this,
            [this, key](int exitCode, bool killed) { onFinished(key, exitCode, killed); }, Qt::QueuedConnection);
    m_running.emplace(key, std::move(running));
    if (!r.process->start()) {
        const QString why = r.process->refusal();
        m_running.erase(key);
        return refusal(errors::kUnavailable, "The sandbox can't apply here: " + why.toStdString(), "sandbox");
    }
    return api::Json::object({{"process", api::Json(r.id)}});
}

PluginPrograms::Running *PluginPrograms::find(const api::Caller &caller, const std::string &process)
{
    const std::string pluginId = caller.id.substr(std::string_view("plugin:").size());
    for (auto &[key, running] : m_running)
        if (running->id == process && running->plugin == pluginId && running->process->running())
            return running.get();
    return nullptr;
}

api::Result PluginPrograms::send(const api::Caller &caller, const std::string &process, const std::string &bytes, bool close)
{
    Running *running = find(caller, process);
    if (!running)
        return api::fail(errors::kNotFound, "There is no running program " + process + ".");
    const QByteArray data = QByteArray::fromStdString(bytes);
    if (!running->base64)
        running->process->write(data);
    else if (const auto decoded = QByteArray::fromBase64Encoding(data, QByteArray::AbortOnBase64DecodingErrors))
        running->process->write(*decoded);
    else
        return api::fail(errors::kInvalidParams, "data isn't base64.", api::Json::object({{"pointer", api::Json("/data")}}));
    if (close)
        running->process->closeInput();
    return api::Json::object({{"process", api::Json(process)}});
}

api::Result PluginPrograms::stop(const api::Caller &caller, const std::string &process)
{
    Running *running = find(caller, process);
    if (!running)
        return api::fail(errors::kNotFound, "There is no running program " + process + ".");
    running->process->kill();
    return api::Json::object({{"process", api::Json(process)}});
}

void PluginPrograms::end(const std::string &pluginId)
{
    for (auto &[key, running] : m_running)
        if (running->plugin == pluginId)
            running->process->kill();
    m_logBudget.erase(pluginId);
}

std::size_t PluginPrograms::started(const std::string &pluginId) const
{
    return std::ranges::count_if(m_running, [&](const auto &entry) {
        return entry.second->plugin == pluginId && !entry.second->id.empty() && entry.second->process->running();
    });
}

void PluginPrograms::onOutput(std::uint64_t key, bool error, const QByteArray &bytes)
{
    const auto it = m_running.find(key);
    if (it == m_running.end())
        return;
    Running &r = *it->second;
    if (error) {
        r.errLine += bytes;
        for (qsizetype newline = r.errLine.indexOf('\n'); newline >= 0; newline = r.errLine.indexOf('\n')) {
            logLine(r.plugin, QString::fromUtf8(r.errLine.left(newline)).trimmed());
            r.errLine.remove(0, newline + 1);
        }
    }
    if (r.reply) {
        QByteArray &kept = error ? r.stderrBytes : r.stdoutBytes;
        if (kept.size() + bytes.size() > kMaxOutput) {
            r.tooLarge = true;
            r.process->kill();
            return;
        }
        kept += bytes;
        return;
    }
    if (!m_wiring.api)
        return;
    const std::string data = r.base64 ? bytes.toBase64().toStdString() : QString((error ? r.err : r.out)(bytes)).toStdString();
    if (data.empty())
        return;
    m_wiring.api->publishTo("plugin:" + r.plugin,
                            {"program.output", api::Json::object({{"process", api::Json(r.id)},
                                                                  {"stream", api::Json(error ? "stderr" : "stdout")},
                                                                  {"data", api::Json(data)},
                                                                  {"encoding", api::Json(r.base64 ? "base64" : "utf8")}})});
}

void PluginPrograms::onFinished(std::uint64_t key, int exitCode, bool killed)
{
    const auto it = m_running.find(key);
    if (it == m_running.end())
        return;
    std::unique_ptr<Running> r = std::move(it->second);
    m_running.erase(it);
    if (!r->errLine.isEmpty())
        logLine(r->plugin, QString::fromUtf8(r->errLine).trimmed());
    r->process.release()->deleteLater();
    if (r->reply) {
        const api::Reply reply = std::move(*r->reply);
        if (r->timedOut)
            return reply(refusal(errors::kRefused, "The program didn't end in time.", "timed_out"));
        if (r->tooLarge)
            return reply(refusal(errors::kRefused, "The program wrote more than 16 MiB.", "too_large"));
        auto text = [&](const QByteArray &bytes) {
            return r->base64 ? bytes.toBase64().toStdString() : QString::fromUtf8(bytes).toStdString();
        };
        return reply(api::Json::object({{"exitCode", api::Json(exitCode)},
                                        {"stdout", api::Json(text(r->stdoutBytes))},
                                        {"stderr", api::Json(text(r->stderrBytes))},
                                        {"encoding", api::Json(r->base64 ? "base64" : "utf8")}}));
    }
    if (m_wiring.api)
        m_wiring.api->publishTo("plugin:" + r->plugin,
                                {"program.exited", api::Json::object({{"process", api::Json(r->id)},
                                                                      {"exitCode", api::Json(exitCode)},
                                                                      {"stopped", api::Json(killed)}})});
}

void PluginPrograms::logLine(const std::string &pluginId, const QString &line)
{
    if (line.isEmpty() || !m_wiring.log)
        return;
    LogBudget &budget = m_logBudget[pluginId];
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!budget.since.isValid() || budget.since.secsTo(now) >= 60)
        budget = {now, 0};
    if (++budget.lines > kLogLinesPerMinute)
        return;
    m_wiring.log(pluginId, line);
}

}
