#pragma once

#include "hikari/application/update_check.h"

#include <QString>

#include <optional>

namespace hikari::app {

struct PackageInfo {
    application::PackageKind kind = application::PackageKind::Portable;
    QString appImage; // with AppImage: the image file ($APPIMAGE)
    // with Arch: aur-channel.txt beside the executable, when it names one
    std::optional<application::AurChannel> aurChannel;
};

// From package-kind.txt beside the executable (none: portable). An AppImage counts only while
// appImageEnv ($APPIMAGE) names an image file; an extracted AppDir is portable.
PackageInfo detectPackageKind(const QString &exeDir, const QString &appImageEnv);
// Test builds (HIKARI_UPDATE_TEST_FEED) take HIKARISUB_PACKAGE_KIND and _AUR_CHANNEL first.
PackageInfo currentPackageKind();

bool onWindows();
// The marker's token as --version prints it (portable-linux for a portable Linux copy).
QString packageKindToken(application::PackageKind kind);
// The kind as About names it: "AppImage", "AUR", "deb", "rpm", "installer"
// or "portable".
QString packageKindLabel(application::PackageKind kind);

}
