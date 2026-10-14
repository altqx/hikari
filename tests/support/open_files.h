#pragma once

// Which files a test's process and its helper processes still hold open.
// Linux reads /proc/<pid>/fd of the process and of its descendants (the
// media and Lua helpers); Windows opens the file with no sharing at all,
// which any open handle refuses. Elsewhere nothing can be told.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <algorithm>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#endif

namespace openfiles {

// The files under `folder` held open, each as "<path> (process <pid>)"
// (Linux; empty elsewhere).
inline QStringList under(const QString &folder)
{
    QStringList out;
#ifdef Q_OS_LINUX
    const QString prefix = QFileInfo(folder).canonicalFilePath() + QLatin1Char('/');
    QStringList pids{QString::number(QCoreApplication::applicationPid())};
    for (qsizetype i = 0; i < pids.size(); ++i) {
        const QString proc = QStringLiteral("/proc/") + pids[i];
        for (const QString &task : QDir(proc + QStringLiteral("/task")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile children(proc + QStringLiteral("/task/") + task + QStringLiteral("/children"));
            if (children.open(QIODevice::ReadOnly))
                for (const QByteArray &pid : children.readAll().simplified().split(' '))
                    if (!pid.isEmpty())
                        pids << QString::fromLatin1(pid);
        }
        // the links of open files are not System entries: AllEntries too
        const auto fds = QDir(proc + QStringLiteral("/fd"))
                             .entryInfoList(QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &fd : fds) {
            const QString target = fd.symLinkTarget();
            if (target.startsWith(prefix))
                out << QStringLiteral("%1 (process %2)").arg(target, pids[i]);
        }
    }
#else
    Q_UNUSED(folder);
#endif
    return out;
}

// Whether no process of the test holds `file` open (true where that cannot
// be told).
inline bool nothingHolds(const QString &file)
{
#ifdef _WIN32
    int fd = -1;
    if (_wsopen_s(&fd, reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(file).utf16()),
                  _O_RDONLY | _O_BINARY, _SH_DENYRW, 0) != 0)
        return false;
    _close(fd);
    return true;
#else
    const QString held = QFileInfo(file).canonicalFilePath() + QStringLiteral(" (");
    const QStringList open = under(QFileInfo(file).absolutePath());
    return std::none_of(open.begin(), open.end(), [&](const QString &o) { return o.startsWith(held); });
#endif
}

} // namespace openfiles
