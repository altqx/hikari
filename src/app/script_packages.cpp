#include "hikari/app/script_packages.h"

#include "hikari/app/automation_shell.h"
#include "hikari/backends/desktop_handoff.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace hikari::app {

namespace {

constexpr const char *kArchFeed =
    "https://raw.githubusercontent.com/TypesettingTools/arch1t3cht-Aegisub-Scripts/main/DependencyControl.json";
constexpr const char *kArchProject = "https://github.com/TypesettingTools/arch1t3cht-Aegisub-Scripts";

QString translated(const std::string &source)
{
    return QCoreApplication::translate("HikariSub.Automation", source.c_str());
}

Qt::CaseSensitivity fileCase()
{
#ifdef _WIN32
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool isFileOf(const RecommendedScript &script, const QString &path)
{
    const QFileInfo file(path);
    if (script.fileName.empty())
        return file.completeBaseName().compare(QString::fromStdString(script.nameSpace), fileCase()) == 0;
    return file.fileName().compare(QString::fromStdString(script.fileName), fileCase()) == 0;
}

QString luaString(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : text) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('"'))
            out += QLatin1Char('\\') + c;
        else if (c.unicode() < 0x20)
            out += QStringLiteral("\\%1").arg(int(c.unicode()), 3, 10, QLatin1Char('0'));
        else
            out += c;
    }
    return out + QLatin1Char('"');
}

QString installReason(int code)
{
    switch (code) {
    case -1: return QCoreApplication::translate("HikariSub.Automation", "DependencyControl’s updater is turned off.");
    case -5: return QCoreApplication::translate("HikariSub.Automation", "DependencyControl is busy with another update. Try again later.");
    case -6:
    case -17: return QCoreApplication::translate("HikariSub.Automation", "Its feed has no release that fits this system.");
    case -7: return QCoreApplication::translate("HikariSub.Automation", "Couldn’t read the feed: no network connection.");
    case -15: return QCoreApplication::translate("HikariSub.Automation", "A module it needs couldn’t be installed.");
    case -16: return QCoreApplication::translate("HikariSub.Automation", "DependencyControl doesn’t trust its feed.");
    case -18: return QCoreApplication::translate("HikariSub.Automation", "The install was cancelled.");
    case -19: return QCoreApplication::translate("HikariSub.Automation", "DependencyControl blocks its feed.");
    case -35: return QCoreApplication::translate("HikariSub.Automation", "A downloaded file didn’t match its feed.");
    case -50: return QCoreApplication::translate("HikariSub.Automation", "A file couldn’t be written. Close what uses it and try again.");
    case -140:
    case -245: return QCoreApplication::translate("HikariSub.Automation", "A file couldn’t be downloaded.");
    default: return QCoreApplication::translate("HikariSub.Automation", "DependencyControl couldn’t install it (status %1).").arg(code);
    }
}

}

const std::vector<RecommendedScript> &recommendedScripts()
{
    static const std::vector<RecommendedScript> scripts{
        {"arch.DerivePerspTrack", "arch.DerivePerspTrack.moon", "Derive Perspective Track",
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Typesetting"),
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Makes a power-pin track from the lines’ perspective, for Perspective-Motion."),
         "Create a power-pin track file from the outer perspective quads of a set of lines.", "arch1t3cht", "MIT", "",
         kArchProject, kArchFeed},
        {"arch.FocusLines", "arch.FocusLines.moon", "Focus Lines", QT_TRANSLATE_NOOP("HikariSub.Automation", "Typesetting"),
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Draws moving focus lines, frame by frame."), "Draws moving focus lines.",
         "arch1t3cht", "MIT", "", kArchProject, kArchFeed},
        {"arch.SplitSections", "arch.SplitSections.moon", "Split Tag Sections",
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Typesetting"),
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Splits a line at its tags, one line per section."),
         "Split subtitle lines at tags, creating a separate event for each section", "arch1t3cht", "MIT", "",
         kArchProject, kArchFeed},
        {"arch.RWTools", "arch.RWTools.lua", "Rewriting Tools", QT_TRANSLATE_NOOP("HikariSub.Automation", "Editing"),
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Keeps earlier rewrites of a line and switches between them."),
         "Shortcuts for managing multiple rewrites of a line in one .ass event line.", "arch1t3cht", "MIT", "",
         kArchProject, kArchFeed},
        {"arch.GitSigns", "arch.GitSigns.moon", "Git Signs", QT_TRANSLATE_NOOP("HikariSub.Automation", "QC"),
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Marks the lines changed since a git commit."),
         "Displays git diffs in Aegisub", "arch1t3cht", "MIT", QT_TRANSLATE_NOOP("HikariSub.Automation", "Needs Git"),
         kArchProject, kArchFeed},
        {"arch.FixKFXBoilerplate", "arch.FixKFXBoilerplate.moon", "Fix KFX Boilerplate",
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Karaoke"),
         QT_TRANSLATE_NOOP("HikariSub.Automation", "Fixes old KFX templates so Aegisub 3.5 runs them too."),
         "Fix certain errors in boilerplate lines in old KFX templates on new Aegisub versions", "arch1t3cht", "MIT", "",
         kArchProject, kArchFeed},
    };
    return scripts;
}

const std::vector<std::string> &recommendedScriptGroups()
{
    static const std::vector<std::string> groups{QT_TRANSLATE_NOOP("HikariSub.Automation", "Typesetting"),
                                                 QT_TRANSLATE_NOOP("HikariSub.Automation", "Editing"),
                                                 QT_TRANSLATE_NOOP("HikariSub.Automation", "QC"),
                                                 QT_TRANSLATE_NOOP("HikariSub.Automation", "Karaoke")};
    return groups;
}

QStringList dependencyControlScripts(const QString &configFile)
{
    QFile file(configFile);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("macros")).toObject().keys();
}

ScriptPackages::ScriptPackages(AutomationShell &shell, std::vector<RecommendedScript> scripts, QObject *parent)
    : QObject(parent), m_shell(shell), m_scripts(std::move(scripts))
{
    connect(&shell.manager(), &backends::AutomationManager::changed, this, &ScriptPackages::changed);
    connect(&shell, &AutomationShell::progressChanged, this, [this] {
        if (!m_current && !m_queue.empty() && !m_shell.busy())
            QTimer::singleShot(0, this, &ScriptPackages::startNext);
    });
    refresh();
}

const RecommendedScript *ScriptPackages::find(const QString &nameSpace) const
{
    const auto it = std::ranges::find_if(m_scripts, [&](const RecommendedScript &s) {
        return QString::fromStdString(s.nameSpace) == nameSpace;
    });
    return it == m_scripts.end() ? nullptr : &*it;
}

bool ScriptPackages::queued(const QString &nameSpace) const
{
    return std::ranges::any_of(m_queue, [&](const Job &j) { return j.nameSpace == nameSpace; });
}

QString ScriptPackages::loadedPath(const RecommendedScript &script) const
{
    for (const auto &s : m_shell.scripts())
        if (isFileOf(script, QString::fromStdString(s.path)))
            return QString::fromStdString(s.path);
    return {};
}

QString ScriptPackages::state(const RecommendedScript &script) const
{
    const QString nameSpace = QString::fromStdString(script.nameSpace);
    if (m_current && m_current->nameSpace == nameSpace)
        return m_current->install ? QStringLiteral("installing") : QStringLiteral("uninstalling");
    if (queued(nameSpace))
        return QStringLiteral("queued");
    if (m_records.contains(nameSpace))
        return QStringLiteral("installed");
    if (std::ranges::any_of(m_files, [&](const QString &file) { return isFileOf(script, file); }))
        return QStringLiteral("manual");
    return m_failures.contains(nameSpace) ? QStringLiteral("failed") : QStringLiteral("install");
}

QVariantList ScriptPackages::entries() const
{
    QVariantList out;
    for (const auto &script : m_scripts) {
        const QString nameSpace = QString::fromStdString(script.nameSpace);
        const QString path = loadedPath(script);
        QString version;
        for (const auto &s : m_shell.scripts())
            if (QString::fromStdString(s.path) == path)
                version = QString::fromStdString(s.info.version);
        QString credit = QCoreApplication::translate("HikariSub.Automation", "%1 · %2")
                             .arg(QString::fromStdString(script.author), QString::fromStdString(script.licence));
        if (!script.needs.empty())
            credit = QCoreApplication::translate("HikariSub.Automation", "%1 · %2").arg(credit, translated(script.needs));
        const auto failure = m_failures.find(nameSpace);
        out.push_back(QVariantMap{
            {QStringLiteral("nameSpace"), nameSpace},
            {QStringLiteral("name"), QString::fromStdString(script.name)},
            {QStringLiteral("group"), translated(script.group)},
            {QStringLiteral("groupKey"), QString::fromStdString(script.group)},
            {QStringLiteral("line"), translated(script.line)},
            {QStringLiteral("credit"), credit},
            {QStringLiteral("about"), QString::fromStdString(script.about)},
            {QStringLiteral("project"), QString::fromStdString(script.project)},
            {QStringLiteral("path"), path},
            {QStringLiteral("version"), version},
            {QStringLiteral("state"), state(script)},
            {QStringLiteral("error"), failure == m_failures.end() ? QString() : failure->second.reason},
            {QStringLiteral("runId"), failure == m_failures.end() ? 0 : failure->second.runId},
        });
    }
    return out;
}

void ScriptPackages::refresh()
{
    m_records = dependencyControlScripts(QDir(m_shell.userAutomationRoot())
                                             .filePath(QStringLiteral("config/l0.DependencyControl.json")));
    m_files = m_shell.autoloadScripts();
    emit changed();
}

void ScriptPackages::install(const QString &nameSpace)
{
    const auto *script = find(nameSpace);
    if (!script || (m_current && m_current->nameSpace == nameSpace) || queued(nameSpace))
        return;
    const QString was = state(*script);
    if (was == QLatin1String("installed") || was == QLatin1String("manual"))
        return;
    m_failures.erase(nameSpace);
    m_queue.push_back({nameSpace, true});
    emit changed();
    startNext();
}

void ScriptPackages::uninstall(const QString &nameSpace)
{
    const auto *script = find(nameSpace);
    if (!script || state(*script) != QLatin1String("installed"))
        return;
    m_failures.erase(nameSpace);
    m_queue.push_back({nameSpace, false});
    emit changed();
    startNext();
}

bool ScriptPackages::openProject(const QString &nameSpace)
{
    const auto *script = find(nameSpace);
    return script && backends::desktop::openUrl(QUrl(QString::fromStdString(script->project)));
}

QString ScriptPackages::writeTask(const Job &job, const RecommendedScript &script) const
{
    const QString name = QString::fromStdString(script.name);
    const QString title = job.install ? QCoreApplication::translate("HikariSub.Automation", "Install %1").arg(name)
                                      : QCoreApplication::translate("HikariSub.Automation", "Uninstall %1").arg(name);
    QString body = QStringLiteral("script_name = %1\nlocal DepCtrl = require(\"l0.DependencyControl\")\n"
                                  "aegisub.register_macro(script_name, \"\", function()\n")
                       .arg(luaString(title));
    if (job.install)
        body += QStringLiteral(
                    "    local record = {namespace = %1, name = %2, feed = %3, virtual = true}\n"
                    "    local task, status, detail = DepCtrl.updater:addTask(record, nil, nil, nil, nil,\n"
                    "        DepCtrl.UpdateTask.UpdateReason.UserRequested)\n"
                    "    if task then\n"
                    "        status, detail = task:run()\n"
                    "        DepCtrl.updater:releaseLock()\n"
                    "    else\n"
                    "        aegisub.log(\"%s\\n\", DepCtrl.UpdateTask.getUpdaterErrorMsg(status, record.name,\n"
                    "            DepCtrl.Domain.ScriptType.Automation, true, detail))\n"
                    "    end\n"
                    "    if type(status) ~= \"number\" or status < 0 then\n"
                    "        error(\"depctrl \" .. tostring(status), 0)\n"
                    "    end\n")
                    .arg(luaString(job.nameSpace), luaString(name), luaString(QString::fromStdString(script.feed)));
    else
        body += QStringLiteral("    local config = DepCtrl.config:getSectionHandler()\n"
                               "    local record = config.c.macros and config.c.macros[%1]\n"
                               "    if not record then\n"
                               "        error(\"depctrl unmanaged\", 0)\n"
                               "    end\n"
                               "    local done, details = DepCtrl(record):uninstall()\n"
                               "    if done == nil then\n"
                               "        aegisub.log(\"%s\\n\", tostring(details))\n"
                               "        error(\"depctrl unmanaged\", 0)\n"
                               "    elseif not done then\n"
                               "        error(\"depctrl locked\", 0)\n"
                               "    end\n")
                    .arg(luaString(job.nameSpace));
    body += QStringLiteral("end)\n");
    const QString dir = QDir(m_shell.userAutomationRoot()).filePath(QStringLiteral("temp"));
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(QStringLiteral("hikari-%1-%2.lua")
                                      .arg(job.install ? QStringLiteral("install") : QStringLiteral("uninstall"),
                                           job.nameSpace)));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(body.toUtf8()) < 0)
        return {};
    return file.fileName();
}

void ScriptPackages::startNext()
{
    if (m_current || m_queue.empty() || m_shell.busy())
        return;
    const Job job = m_queue.front();
    const auto *script = find(job.nameSpace);
    const QString path = script ? writeTask(job, *script) : QString();
    if (path.isEmpty()) {
        m_queue.pop_front();
        m_failures[job.nameSpace] = {QCoreApplication::translate("HikariSub.Automation", "The user folder can’t be written."), 0};
        emit changed();
        QTimer::singleShot(0, this, &ScriptPackages::startNext);
        return;
    }
    const QString name = QString::fromStdString(script->name);
    m_current = job;
    const bool started = m_shell.runTask(
        job.install ? QCoreApplication::translate("HikariSub.Automation", "Install %1").arg(name)
                    : QCoreApplication::translate("HikariSub.Automation", "Uninstall %1").arg(name),
        path, [this, job, path](backends::LuaScriptHost::RunOutcome outcome, const QString &message) {
            QFile::remove(path);
            return finished(job, outcome, message);
        });
    if (!started) {
        m_current.reset();
        QFile::remove(path);
        return;
    }
    m_queue.pop_front();
    emit changed();
}

QString ScriptPackages::finished(Job job, backends::LuaScriptHost::RunOutcome outcome, const QString &message)
{
    using Outcome = backends::LuaScriptHost::RunOutcome;
    m_current.reset();
    QTimer::singleShot(0, this, &ScriptPackages::startNext);
    const auto *script = find(job.nameSpace);
    const QString name = script ? QString::fromStdString(script->name) : job.nameSpace;
    const int runId = m_shell.runId();
    if (outcome == Outcome::Cancelled) {
        refresh();
        return job.install ? QCoreApplication::translate("HikariSub.Automation", "The install was cancelled.")
                           : QCoreApplication::translate("HikariSub.Automation", "The uninstall was cancelled.");
    }
    if (outcome == Outcome::Ok && script) {
        refresh();
        if (job.install) {
            for (const QString &path : m_shell.autoloadScripts()) {
                if (!isFileOf(*script, path))
                    continue;
                m_shell.setAutoloadEnabled(path, true);
                if (!m_shell.manager().host(path.toStdString()))
                    m_shell.load(path.toStdString());
            }
        } else {
            for (const auto &s : m_shell.scripts())
                if (isFileOf(*script, QString::fromStdString(s.path)) && !QFile::exists(QString::fromStdString(s.path)))
                    m_shell.unload(s.path);
        }
        refresh();
        if (!job.install)
            return QCoreApplication::translate("HikariSub.Automation", "%1 is uninstalled.").arg(name);
        emit installed(job.nameSpace, name);
        return QCoreApplication::translate("HikariSub.Automation", "%1 is installed.").arg(name);
    }
    static const QRegularExpression status(QStringLiteral("depctrl (-?\\d+|unmanaged|locked)"));
    const auto match = status.match(message);
    QString reason;
    if (!match.hasMatch())
        reason = message.trimmed().isEmpty()
                     ? QCoreApplication::translate("HikariSub.Automation", "The run ended before it finished.")
                     : message.trimmed();
    else if (match.captured(1) == QLatin1String("unmanaged"))
        reason = QCoreApplication::translate("HikariSub.Automation", "DependencyControl has no record of it.");
    else if (match.captured(1) == QLatin1String("locked"))
        reason = QCoreApplication::translate("HikariSub.Automation",
                                             "Some of its files couldn’t be deleted. Close what uses them and try again.");
    else
        reason = installReason(match.captured(1).toInt());
    m_failures[job.nameSpace] = {reason, runId};
    refresh();
    return reason;
}

}
