#include "hikari/app/skills_settings.h"

#include "hikari/api/folder_skills.h"
#include "hikari/backends/desktop_handoff.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace hikari::app {

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

std::filesystem::path pathOf(const QString &folder)
{
    return std::filesystem::path(folder.toStdU16String());
}

QString problemText(const api::SkillReport &report)
{
    switch (report.check.problem) {
    case api::SkillProblem::None: return {};
    case api::SkillProblem::NotASkillName:
        return QCoreApplication::translate("HikariSub.Skills", "The folder’s name isn’t a skill name: use a–z, 0–9 and single hyphens.");
    case api::SkillProblem::NoSkillFile: return QCoreApplication::translate("HikariSub.Skills", "The folder has no SKILL.md.");
    case api::SkillProblem::NotText: return QCoreApplication::translate("HikariSub.Skills", "SKILL.md isn’t UTF-8 text, or is over 256 KiB.");
    case api::SkillProblem::NoFrontmatter: return QCoreApplication::translate("HikariSub.Skills", "SKILL.md doesn’t start with frontmatter (---).");
    case api::SkillProblem::NameMismatch:
        return QCoreApplication::translate("HikariSub.Skills", "SKILL.md says name: %1, but the folder is %2.").arg(qs(report.check.namedAs), qs(report.folder));
    case api::SkillProblem::NoDescription: return QCoreApplication::translate("HikariSub.Skills", "SKILL.md has no description.");
    }
    return {};
}

QString shown(QString folder)
{
#ifndef Q_OS_WIN
    const QString home = QDir::homePath();
    if (folder.startsWith(home + u'/'))
        folder.replace(0, home.size(), QStringLiteral("~"));
#endif
    return QDir::toNativeSeparators(folder);
}

QVariantMap row(const QString &name, const QString &origin, const QString &description, std::size_t files,
                std::size_t found, const QString &problem, const QString &hidden)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("origin"), origin},
            {QStringLiteral("description"), description},
            {QStringLiteral("files"), qulonglong(files)},
            {QStringLiteral("found"), qulonglong(found)},
            {QStringLiteral("problem"), problem},
            {QStringLiteral("hidden"), hidden}};
}

bool openMade(const QString &folder)
{
    return !folder.isEmpty() && QDir().mkpath(folder) && backends::desktop::openUrl(QUrl::fromLocalFile(folder));
}

}

std::vector<BuiltinSkill> builtinSkills()
{
    std::vector<BuiltinSkill> out;
    const QDir root(QStringLiteral(":/hikari/ai-skills"));
    for (const QString &folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile skill(root.filePath(folder + QStringLiteral("/SKILL.md")));
        if (!skill.open(QIODevice::ReadOnly))
            continue;
        const QByteArray text = skill.readAll();
        const api::SkillCheck check = api::checkSkill(folder.toStdString(), std::string_view(text.data(), text.size()));
        if (check.problem != api::SkillProblem::None)
            continue;
        std::size_t files = 0;
        for (QDirIterator it(root.filePath(folder), QDir::Files, QDirIterator::Subdirectories); it.hasNext(); it.next())
            ++files;
        out.push_back({folder.toStdString(), check.description, files});
    }
    return out;
}

SkillsSettings::SkillsSettings(QString userFolder, std::function<QString(int window)> projectRoot, QObject *parent)
    : QObject(parent), m_userFolder(std::move(userFolder)), m_projectRoot(std::move(projectRoot))
{
}

QString SkillsSettings::userSkills() const
{
    return m_userFolder.isEmpty() ? QString() : m_userFolder + QStringLiteral("/ai/skills");
}

QString SkillsSettings::projectSkills(int window) const
{
    const QString root = m_projectRoot ? m_projectRoot(window) : QString();
    return root.isEmpty() ? QString() : root + QStringLiteral("/.hikari/skills");
}

QVariantList SkillsSettings::groups(int window) const
{
    const QString root = m_projectRoot ? m_projectRoot(window) : QString();
    const std::vector<api::SkillReport> reports = api::SkillTiers(pathOf(userSkills())).report(pathOf(root));
    QSet<QString> project, mine;
    for (const api::SkillReport &r : reports)
        if (r.check.problem == api::SkillProblem::None)
            (r.origin == api::SkillOrigin::Project ? project : mine).insert(qs(r.folder));
    const QString hiddenByProject = QCoreApplication::translate("HikariSub.Skills", "Hidden by this project’s.");
    QVariantList projectRows, mineRows, builtinRows;
    for (const api::SkillReport &r : reports) {
        const QString name = qs(r.folder);
        const bool ours = r.origin == api::SkillOrigin::Project;
        const bool offered = r.check.problem == api::SkillProblem::None;
        (ours ? projectRows : mineRows)
            .push_back(row(name, ours ? QStringLiteral("project") : QStringLiteral("mine"), qs(r.check.description),
                           r.files, r.found, problemText(r),
                           !ours && offered && project.contains(name) ? hiddenByProject : QString()));
    }
    for (const BuiltinSkill &skill : builtinSkills()) {
        const QString name = qs(skill.name);
        const QString hidden = project.contains(name) ? hiddenByProject
                               : mine.contains(name)  ? QCoreApplication::translate("HikariSub.Skills", "Replaced by yours.")
                                                      : QString();
        builtinRows.push_back(row(name, QStringLiteral("builtin"), qs(skill.description), skill.files, skill.files, {}, hidden));
    }
    const auto byName = [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString() < b.toMap().value(QStringLiteral("name")).toString();
    };
    std::stable_sort(builtinRows.begin(), builtinRows.end(), byName);
    QVariantList out;
    const auto group = [&](const QString &id, const QString &folder, const QVariantList &rows) {
        out.push_back(QVariantMap{{QStringLiteral("group"), id}, {QStringLiteral("folder"), folder}, {QStringLiteral("rows"), rows}});
    };
    if (!root.isEmpty())
        group(QStringLiteral("project"), QDir::toNativeSeparators(QFileInfo(root).fileName() + QStringLiteral("/.hikari/skills")),
              projectRows);
    group(QStringLiteral("mine"), shown(userSkills()), mineRows);
    group(QStringLiteral("builtin"), {}, builtinRows);
    return out;
}

bool SkillsSettings::openFolder() const
{
    return openMade(userSkills());
}

bool SkillsSettings::openProjectFolder(int window) const
{
    return openMade(projectSkills(window));
}

void SkillsSettings::openGuide() const
{
    backends::desktop::openUrl(guideUrl());
}

}
