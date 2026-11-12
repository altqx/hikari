#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <chrono>

namespace hikari::testing {

/// Removes the default-named QTemporaryDirs (`<tempPath>/<applicationName>-XXXXXX`) an earlier run of this test left
/// when it was killed or crashed. Folders changed within `age` may belong to a run still going, so they stay.
inline int removeStaleTemporaryDirs(std::chrono::seconds age = std::chrono::hours(1))
{
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-age.count());
    const QString pattern = QCoreApplication::applicationName() + QStringLiteral("-??????");
    int removed = 0;
    for (const QFileInfo &info : QDir(QDir::tempPath()).entryInfoList({pattern}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
        if (!info.isSymLink() && info.lastModified().toUTC() < cutoff && QDir(info.filePath()).removeRecursively())
            ++removed;
    return removed;
}

}
