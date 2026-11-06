#pragma once

#include "hikari/backends/lua_script_host.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hikari::app {

class AutomationShell;

// One entry of the Automation panel's Recommended list. `group`, `line` and `needs` are source strings
// in the HikariSub.Automation context.
struct RecommendedScript {
    std::string nameSpace;
    // Its file name, in the third-party corpus and in an autoload folder.
    std::string fileName;
    std::string name;
    std::string group;
    std::string line;
    // The feed's own description, in English.
    std::string about;
    std::string author;
    std::string licence;
    std::string needs;
    std::string project;
    std::string feed;
};

// In the order the panel shows them: by group, then by name.
const std::vector<RecommendedScript> &recommendedScripts();
const std::vector<std::string> &recommendedScriptGroups();

// The namespaces that DependencyControl's config file records as installed automation scripts.
QStringList dependencyControlScripts(const QString &configFile);

// Installs and uninstalls the Recommended scripts through the bundled DependencyControl, one at a time,
// each as an Automation run.
class ScriptPackages : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
public:
    ScriptPackages(AutomationShell &shell, std::vector<RecommendedScript> scripts, QObject *parent = nullptr);

    // Each entry: nameSpace, name, group, line, credit, about, version, project, path, runId, error, and state,
    // one of "install", "queued", "installing", "failed", "installed", "manual" (installed, not by
    // DependencyControl) and "uninstalling".
    QVariantList entries() const;
    // Reads DependencyControl's config and the autoload folders again.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void install(const QString &nameSpace);
    Q_INVOKABLE void uninstall(const QString &nameSpace);
    Q_INVOKABLE bool openProject(const QString &nameSpace);

signals:
    void changed();
    void installed(const QString &nameSpace, const QString &name);

private:
    struct Job {
        QString nameSpace;
        bool install = true;
    };
    struct Failure {
        QString reason;
        int runId = 0;
    };
    const RecommendedScript *find(const QString &nameSpace) const;
    QString state(const RecommendedScript &script) const;
    QString loadedPath(const RecommendedScript &script) const;
    bool queued(const QString &nameSpace) const;
    void startNext();
    QString finished(Job job, backends::LuaScriptHost::RunOutcome outcome, const QString &message);
    QString writeTask(const Job &job, const RecommendedScript &script) const;

    AutomationShell &m_shell;
    std::vector<RecommendedScript> m_scripts;
    std::deque<Job> m_queue;
    std::optional<Job> m_current;
    std::map<QString, Failure> m_failures;
    QStringList m_records;
    QStringList m_files;
};

}
