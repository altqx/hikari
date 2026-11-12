#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <functional>
#include <string>
#include <vector>

namespace hikari::app {

// A built-in skill, from HikariSub's own copy of src/ai-sidecar/skills.
struct BuiltinSkill {
    std::string name;
    std::string description;
    std::size_t files = 0;
};

std::vector<BuiltinSkill> builtinSkills();

// Settings › AI › Skills (docs/ai-skills.md): the skills a window's threads get, and the folders they come from.
class SkillsSettings final : public QObject {
    Q_OBJECT
public:
    SkillsSettings(QString userFolder, std::function<QString(int window)> projectRoot, QObject *parent = nullptr);

    // Groups "project" (in a Project window), "mine" and "builtin", in that order: {group, folder, rows}. Each row is
    // {name, origin, description, files, found, problem, hidden}; `problem` says why it isn't offered, `hidden` what
    // hides it, and both are empty for an offered skill.
    Q_INVOKABLE QVariantList groups(int window) const;
    Q_INVOKABLE bool openFolder() const;
    Q_INVOKABLE bool openProjectFolder(int window) const;
    Q_INVOKABLE void openGuide() const;

    static QUrl guideUrl() { return QUrl(QStringLiteral("https://hikarisub.org/en/docs/ai/skills")); }

private:
    QString userSkills() const;
    QString projectSkills(int window) const;

    QString m_userFolder;
    std::function<QString(int)> m_projectRoot;
};

}
