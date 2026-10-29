#include "next_file_folder.h"

#include <QDir>
#include <QFile>

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace hikari::tests {

QStringList enumerateFolder(const QString &folder)
{
    QStringList out;
#ifdef _WIN32
    const std::wstring spec = QDir::toNativeSeparators(folder + QStringLiteral("/*")).toStdWString();
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileW(spec.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return out;
    do {
        const QString name = QString::fromWCharArray(data.cFileName);
        if (name != QLatin1String(".") && name != QLatin1String(".."))
            out << name;
    } while (FindNextFileW(find, &data));
    FindClose(find);
#else
    if (DIR *d = opendir(QFile::encodeName(folder).constData())) {
        while (const dirent *e = readdir(d)) {
            const QString name = QFile::decodeName(e->d_name);
            if (name != QLatin1String(".") && name != QLatin1String(".."))
                out << name;
        }
        closedir(d);
    }
#endif
    return out;
}

bool setHiddenAttribute(const QString &path)
{
#ifdef _WIN32
    return SetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN) != 0;
#else
    Q_UNUSED(path);
    return true;
#endif
}

bool setSystemAttribute(const QString &path)
{
#ifdef _WIN32
    return SetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str(), FILE_ATTRIBUTE_SYSTEM) != 0;
#else
    Q_UNUSED(path);
    return true;
#endif
}

QString folderFileSystem(const QString &folder)
{
#ifdef _WIN32
    wchar_t root[MAX_PATH] = {};
    wchar_t name[MAX_PATH] = {};
    if (GetVolumePathNameW(QDir::toNativeSeparators(folder).toStdWString().c_str(), root, MAX_PATH)
        && GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, name, MAX_PATH))
        return QString::fromWCharArray(name);
#else
    Q_UNUSED(folder);
#endif
    return {};
}

}
