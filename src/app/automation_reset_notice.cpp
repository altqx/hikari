#include "hikari/app/automation_reset_notice.h"

#include "hikari/app/script_packages.h"
#include "hikari/application/hotkeys.h"
#include "hikari/backends/desktop_handoff.h"

#include "command_names.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

namespace hikari::app {

namespace {

QStringList namespacesIn(const QString &folder)
{
    QStringList out;
    for (const QFileInfo &file : QDir(folder).entryInfoList(QDir::Files))
        out << file.completeBaseName();
    return out;
}

}

AutomationResetNotice::AutomationResetNotice(AutomationReset reset, AutomationShell &shell,
                                             const QString &bundledAutoload, QObject *parent)
    : QObject(parent), m_reset(std::move(reset))
{
    if (m_reset.outcome != AutomationReset::Outcome::Moved)
        return;
    const ReinstallPlan plan =
        reinstallPlan(m_reset.backup + QStringLiteral("/config/l0.DependencyControl.json"), namespacesIn(bundledAutoload));
    for (const NativeScript &native : plan.builtIn) {
        const std::string name = application::hotkeyName(application::hotkeyIdOf(native.command));
        m_builtIn.push_back(QVariantMap{
            {QStringLiteral("nameSpace"), QString::fromLatin1(native.nameSpace)},
            {QStringLiteral("command"), ui::hotkeyCommandName(name)},
        });
    }
    std::vector<RecommendedScript> scripts;
    for (const BackupScript &script : plan.queued) {
        RecommendedScript entry;
        entry.nameSpace = script.nameSpace.toStdString();
        entry.name = script.name.toStdString();
        entry.feed = script.feed.toStdString();
        scripts.push_back(std::move(entry));
    }
    m_packages = std::make_unique<ScriptPackages>(shell, std::move(scripts));
    connect(m_packages.get(), &ScriptPackages::changed, this, &AutomationResetNotice::changed);
}

AutomationResetNotice::~AutomationResetNotice() = default;

bool AutomationResetNotice::due() const
{
    return m_reset.outcome == AutomationReset::Outcome::Moved || m_reset.outcome == AutomationReset::Outcome::Failed;
}

QString AutomationResetNotice::outcome() const
{
    switch (m_reset.outcome) {
    case AutomationReset::Outcome::Moved: return QStringLiteral("moved");
    case AutomationReset::Outcome::Failed: return QStringLiteral("failed");
    default: return {};
    }
}

QString AutomationResetNotice::backup() const
{
    return QDir::toNativeSeparators(m_reset.backup);
}

QVariantList AutomationResetNotice::scripts() const
{
    QVariantList out;
    if (!m_packages)
        return out;
    for (const QVariant &e : m_packages->entries()) {
        const QVariantMap entry = e.toMap();
        out.push_back(QVariantMap{
            {QStringLiteral("nameSpace"), entry.value(QStringLiteral("nameSpace"))},
            {QStringLiteral("name"), entry.value(QStringLiteral("name"))},
            {QStringLiteral("state"), entry.value(QStringLiteral("state"))},
            {QStringLiteral("error"), entry.value(QStringLiteral("error"))},
        });
    }
    return out;
}

int AutomationResetNotice::reinstallCount() const
{
    return m_packages && !m_reinstalling ? int(m_packages->entries().size()) : 0;
}

void AutomationResetNotice::reinstall()
{
    if (!m_packages || m_reinstalling)
        return;
    m_reinstalling = true;
    for (const QVariant &e : m_packages->entries())
        m_packages->install(e.toMap().value(QStringLiteral("nameSpace")).toString());
    emit changed();
}

void AutomationResetNotice::retry(const QString &nameSpace)
{
    if (m_packages && m_reinstalling)
        m_packages->install(nameSpace);
}

bool AutomationResetNotice::openBackup() const
{
    return !m_reset.backup.isEmpty() && QFileInfo(m_reset.backup).isDir() &&
           backends::desktop::openUrl(QUrl::fromLocalFile(m_reset.backup));
}

}
