#include "hikari/app/automation_reset.h"

#include "hikari/application/update_check.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace hikari::app {

namespace {

const QString kRecord = QStringLiteral(".automation-reset");
const QString kSeedRecord = QStringLiteral(".bundled-files");

constexpr NativeScript kNative[] = {
    {"arch.Line2Fbf", "GRID_SPLIT_BY_FRAME"},
    {"arch.PerspectiveMotion", "GLOBAL_PERSPECTIVE_TRACKING"},
    {"Flux.DialogSwapper", "GLOBAL_SWAP_DIALOGUE_ALTERNATIVES"},
    {"l0.CascadeTransforms", "GLOBAL_INTERPOLATE_LINES"},
    {"lyger.FbfTransform", "GLOBAL_INTERPOLATE_LINES"},
    {"phos.AutoFade", "GLOBAL_FIND_FADE"},
    {"phos.RemoveTags", "GLOBAL_REMOVE_TAGS"},
    {"zah.aegi-color-track", "GLOBAL_TRACK_COLOUR"},
};

std::filesystem::path pathOf(const QString &path)
{
    return std::filesystem::path(QDir::toNativeSeparators(path).toStdWString());
}

QString rename(const QString &from, const QString &to)
{
    std::error_code error;
    std::filesystem::rename(pathOf(from), pathOf(to), error);
    return error ? QString::fromStdString(error.message()) : QString();
}

QStringList entriesOf(const QString &folder)
{
    QStringList names = QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    names.sort();
    return names;
}

QByteArray sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result().toHex() : QByteArray();
}

QHash<QString, QByteArray> seededVersions(const QString &root)
{
    QHash<QString, QByteArray> out;
    QFile in(root + QLatin1Char('/') + kSeedRecord);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    while (!in.atEnd()) {
        const QByteArray line = in.readLine().trimmed();
        const qsizetype space = line.indexOf(' ');
        if (space > 0)
            out.insert(QString::fromUtf8(line.mid(space + 1)), line.left(space));
    }
    return out;
}

QStringList onlySeededFiles(const QString &root)
{
    const QHash<QString, QByteArray> seeded = seededVersions(root);
    const QDir base(root);
    QStringList files;
    QDirIterator it(root + QStringLiteral("/automation"), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo entry = it.nextFileInfo();
        if (entry.isDir() && !entry.isSymLink())
            continue;
        const QString relative = base.relativeFilePath(entry.filePath());
        if (entry.isSymLink() || !seeded.contains(relative) || sha256Of(entry.filePath()) != seeded.value(relative))
            return {};
        files << relative;
    }
    return files;
}

void removeEmptyFolders(const QString &folder)
{
    const QDir dir(folder);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks))
        removeEmptyFolders(dir.filePath(name));
    QDir().rmdir(folder);
}

bool record(const QString &root)
{
    QSaveFile out(root + QLatin1Char('/') + kRecord);
    return out.open(QIODevice::WriteOnly | QIODevice::Text) && out.write(kAutomationResetVersion) >= 0 &&
           out.write("\n") >= 0 && out.commit();
}

QString freeBackupName(const QString &root, const QDate &today)
{
    const QString base = root + QStringLiteral("/automation-backup-") + today.toString(QStringLiteral("yyyy-MM-dd"));
    QString name = base;
    for (int n = 2; QFileInfo::exists(name) || QFileInfo(name).isSymLink(); ++n)
        name = base + QStringLiteral("-%1").arg(n);
    return name;
}

// <root>/config is where scripts and DependencyControl keep their records (`?user/config`).
QString moveConfigInto(const QString &root, const QString &backup, QStringList &moved)
{
    const QString config = root + QStringLiteral("/config");
    if (!QFileInfo(config).isDir() || entriesOf(config).isEmpty())
        return {};
    if (QString error = rename(config, backup + QStringLiteral("/config")); !error.isEmpty())
        return error;
    moved << QStringLiteral("config");
    return {};
}

}

bool automationResetDue(bool enabled, const QString &version)
{
    const auto running = application::parseSemVer(version.toStdString());
    const auto gate = application::parseSemVer(kAutomationResetVersion);
    return enabled && running && gate && application::compareSemVer(*running, *gate) >= 0;
}

AutomationReset resetAutomationFolder(const QString &root, const QDate &today)
{
    AutomationReset report;
    if (QFileInfo::exists(root + QLatin1Char('/') + kRecord))
        return report;
    const QString automation = root + QStringLiteral("/automation");
    const QStringList top = entriesOf(automation);
    if (top.isEmpty()) {
        record(root);
        return report;
    }
    if (const QStringList seeded = onlySeededFiles(root); !seeded.isEmpty()) {
        for (const QString &file : seeded)
            QFile::remove(root + QLatin1Char('/') + file);
        removeEmptyFolders(automation);
        QDir().mkpath(automation);
        record(root);
        report.outcome = AutomationReset::Outcome::Cleared;
        return report;
    }
    report.backup = freeBackupName(root, today);
    const auto fail = [&](const QString &reason) {
        report.outcome = AutomationReset::Outcome::Failed;
        report.reason = reason;
        return report;
    };
    if (std::error_code error; !std::filesystem::create_directory(pathOf(report.backup), error))
        return fail(QString::fromStdString(error ? error.message() : std::make_error_code(std::errc::file_exists).message()));
    if (QString error = rename(automation, report.backup + QStringLiteral("/automation")); !error.isEmpty()) {
        QDir().rmdir(report.backup);
        return fail(error);
    }
    for (const QString &name : top)
        report.moved << QStringLiteral("automation/") + name;
    if (QString error = moveConfigInto(root, report.backup, report.moved); !error.isEmpty()) {
        const QString undone = rename(report.backup + QStringLiteral("/automation"), automation);
        if (undone.isEmpty())
            QDir().rmdir(report.backup);
        report.moved.clear();
        return fail(error);
    }
    QDir().mkpath(automation);
    record(root);
    report.outcome = AutomationReset::Outcome::Moved;
    return report;
}

std::span<const NativeScript> nativeScripts()
{
    return kNative;
}

ReinstallPlan reinstallPlan(const QString &configFile, const QStringList &bundled)
{
    ReinstallPlan plan;
    QFile file(configFile);
    if (!file.open(QIODevice::ReadOnly))
        return plan;
    const QJsonObject macros = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("macros")).toObject();
    for (auto it = macros.begin(); it != macros.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QString feed = entry.value(QStringLiteral("feed")).toString();
        if (entry.value(QStringLiteral("recordType")).toString() != QLatin1String("managed") || feed.isEmpty() ||
            bundled.contains(it.key()))
            continue;
        const auto native = std::ranges::find_if(kNative, [&](const NativeScript &n) { return it.key() == QLatin1String(n.nameSpace); });
        if (native != std::end(kNative)) {
            plan.builtIn.push_back(*native);
            continue;
        }
        const QString name = entry.value(QStringLiteral("name")).toString();
        plan.queued.push_back({it.key(), name.isEmpty() ? it.key() : name, feed});
    }
    return plan;
}

}
