#include "hikari/app/ai_scripts.h"

#include "hikari/app/automation_shell.h"
#include "hikari/application/dependency_control.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

namespace hikari::app {

AiScripts::AiScripts(AutomationShell &shell, QString folder, std::function<bool()> hasDocument, QObject *parent)
    : QObject(parent), m_shell(shell), m_folder(std::move(folder)), m_hasDocument(std::move(hasDocument))
{
    connect(&m_shell.manager(), &backends::AutomationManager::changed, this, &AiScripts::scriptsChanged);
    connect(&m_shell.manager(), &backends::AutomationManager::loadFailed, this, [this](const QString &path) {
        if (m_pending && !m_pending->started && path.toStdString() == m_pending->path) {
            const auto *host = m_shell.manager().host(m_pending->path);
            finish("failed", host ? host->lastError() : QString());
        }
    });
    connect(&m_shell, &AutomationShell::runCompleted, this, [this](bool ok, const QString &message) {
        if (m_pending && m_pending->started)
            finish(ok ? "applied" : m_shell.lastOutcome().toStdString(), message);
    });
}

api::LuaStart AiScripts::run(const api::LuaScript &script, std::function<void(api::LuaRun)> done)
{
    if (m_pending || m_shell.busy())
        return api::LuaStart::Busy;
    if (!m_hasDocument || !m_hasDocument())
        return api::LuaStart::NoDocument;
    const QString file = QDir(m_folder).filePath(QString::fromStdString(script.name) + QStringLiteral(".lua"));
    QSaveFile out(file);
    if (m_folder.isEmpty() || !QDir().mkpath(m_folder) || !out.open(QIODevice::WriteOnly) ||
        out.write(script.text.data(), qint64(script.text.size())) != qint64(script.text.size()) || !out.commit())
        return api::LuaStart::NotSaved;
    const std::string path = QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath()).toStdString();
    m_pending = Pending{path, script.macro, std::move(done)};
    if (!m_shell.manager().host(path) || !m_shell.manager().reload(path))
        m_shell.manager().load(path);
    return api::LuaStart::Started;
}

namespace {

QString recordedVersion(const QJsonValue &version)
{
    if (version.isString())
        return version.toString();
    if (!version.isDouble())
        return {};
    const auto packed = static_cast<qint64>(version.toDouble());
    return QStringLiteral("%1.%2.%3").arg((packed >> 16) & 0xFF).arg((packed >> 8) & 0xFF).arg(packed & 0xFF);
}

}

api::Json AiScripts::needs(const std::string &script) const
{
    api::Json out = api::Json::array();
    const auto wanted = application::dependencyControlNeeds(script);
    if (wanted.empty())
        return out;
    QFile config(QDir(m_shell.userAutomationRoot()).filePath(QStringLiteral("config/l0.DependencyControl.json")));
    const QJsonObject recorded = config.open(QIODevice::ReadOnly)
                                     ? QJsonDocument::fromJson(config.readAll()).object().value(QStringLiteral("modules")).toObject()
                                     : QJsonObject();
    const QStringList includes = m_shell.includeFolders();
    for (const application::ModuleNeed &need : wanted) {
        const QString module = QString::fromStdString(need.module);
        const QString stem = QString(module).replace(QLatin1Char('.'), QLatin1Char('/'));
        const bool found = std::ranges::any_of(includes, [&](const QString &dir) {
            const QDir d(dir);
            return QFileInfo::exists(d.filePath(stem + QStringLiteral(".lua"))) ||
                   QFileInfo::exists(d.filePath(stem + QStringLiteral(".moon"))) ||
                   QFileInfo::exists(d.filePath(stem + QStringLiteral("/init.lua")));
        });
        api::Json entry = api::Json::object({{"module", need.module}});
        if (!need.version.empty())
            entry["version"] = need.version;
        if (found)
            entry["installed"] = recordedVersion(recorded.value(module).toObject().value(QStringLiteral("version"))).toStdString();
        out.push(std::move(entry));
    }
    return out;
}

void AiScripts::scriptsChanged()
{
    if (!m_pending || m_pending->started)
        return;
    for (const application::ScriptStatus &status : m_shell.manager().scripts()) {
        if (status.path != m_pending->path || status.state != application::ScriptStatus::State::Ready)
            continue;
        const auto &macros = status.info.macros;
        int ordinal = macros.empty() ? -1 : 0;
        if (!m_pending->macro.empty()) {
            const auto it = std::ranges::find_if(macros, [&](const auto &m) { return m.name == m_pending->macro; });
            ordinal = it == macros.end() ? -1 : int(it - macros.begin());
        }
        if (ordinal < 0)
            return finish("invalid", m_pending->macro.empty()
                                         ? QStringLiteral("The script registers no macro.")
                                         : QStringLiteral("The script registers no macro named \"%1\".")
                                               .arg(QString::fromStdString(m_pending->macro)));
        m_pending->started = true;
        if (!m_shell.run(m_pending->path, ordinal, AutomationShell::RunOrigin::Menu)) {
            m_pending->started = false;
            finish("failed", m_shell.lastMessage());
        }
        return;
    }
}

void AiScripts::finish(std::string outcome, QString message)
{
    Pending pending = std::move(*m_pending);
    m_pending.reset();
    QTimer::singleShot(0, this, [this, path = pending.path] {
        if (!m_pending || m_pending->path != path)
            m_shell.manager().unload(path);
    });
    pending.done(api::LuaRun{std::move(outcome), message.toStdString(), m_shell.log().toStdString(),
                             QFileInfo(QString::fromStdString(pending.path)).fileName().toStdString()});
}

}
