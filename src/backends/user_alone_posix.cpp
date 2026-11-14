#include "hikari/backends/user_alone.h"

#include <QFile>
#include <QFileInfo>

namespace hikari::backends {

namespace {

constexpr QFileDevice::Permissions kGroupAndOther = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                                   QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                                   QFileDevice::WriteOther | QFileDevice::ExeOther;

}

bool isForUserAlone(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && !info.isSymLink() && (info.permissions() & kGroupAndOther) == QFileDevice::Permissions();
}

bool keepForUserAlone(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || info.isSymLink())
        return false;
    QFileDevice::Permissions owner = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (info.isDir())
        owner |= QFileDevice::ExeOwner;
    return QFile::setPermissions(path, owner);
}

}
