#include "hikari/app/package_kind.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

namespace hikari::app {

using application::PackageKind;

namespace {

PackageInfo fromMarker(const QByteArray &marker, const QString &appImageEnv)
{
    PackageInfo info;
    const auto kind = application::parsePackageKind(std::string_view(marker.constData(), std::size_t(marker.size())));
    if (!kind)
        return info;
    if (*kind == PackageKind::AppImage) {
        // An extracted AppDir has the marker but no image to replace.
        const QFileInfo image(appImageEnv);
        if (appImageEnv.isEmpty() || !image.isFile())
            return info;
        info.appImage = image.absoluteFilePath();
    }
    info.kind = *kind;
    return info;
}

std::optional<application::AurChannel> channelOf(const QByteArray &content)
{
    return application::parseAurChannel(std::string_view(content.constData(), std::size_t(content.size())));
}

}

bool onWindows()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

PackageInfo detectPackageKind(const QString &exeDir, const QString &appImageEnv)
{
    QFile marker(exeDir + QStringLiteral("/package-kind.txt"));
    if (!marker.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = marker.read(64);
    PackageInfo info = fromMarker(bytes, appImageEnv);
    if (info.kind == PackageKind::Arch) {
        QFile channel(exeDir + QStringLiteral("/aur-channel.txt"));
        if (channel.open(QIODevice::ReadOnly))
            info.aurChannel = channelOf(channel.read(64));
    }
    return info;
}

PackageInfo currentPackageKind()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
#ifdef HIKARI_UPDATE_TEST_FEED
    if (const QByteArray forced = qgetenv("HIKARISUB_PACKAGE_KIND"); !forced.isEmpty()) {
        PackageInfo info = fromMarker(forced, appImage);
        if (info.kind == PackageKind::Arch)
            info.aurChannel = channelOf(qgetenv("HIKARISUB_AUR_CHANNEL"));
        return info;
    }
#endif
    return detectPackageKind(QCoreApplication::applicationDirPath(), appImage);
}

QString packageKindToken(PackageKind kind)
{
    const std::string_view token = application::packageKindToken(kind, onWindows());
    return QString::fromLatin1(token.data(), qsizetype(token.size()));
}

QString packageKindLabel(PackageKind kind)
{
    switch (kind) {
    case PackageKind::AppImage:
        return QStringLiteral("AppImage");
    case PackageKind::Arch:
        return QStringLiteral("AUR");
    case PackageKind::Deb:
        return QStringLiteral("deb");
    case PackageKind::Rpm:
        return QStringLiteral("rpm");
    case PackageKind::NsisUser:
    case PackageKind::NsisMachine:
        return QCoreApplication::translate("HikariSub.MainWindow", "installer");
    case PackageKind::Portable:
        break;
    }
    return QCoreApplication::translate("HikariSub.MainWindow", "portable");
}

}
