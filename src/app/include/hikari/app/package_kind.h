#pragma once

// How this copy was installed (ADR 0019): package-kind.txt beside the
// executable names the installer (NSIS, AppImage, deb, rpm, the AUR); no
// marker is a portable copy. An AppImage counts only while $APPIMAGE names
// the image it runs from; otherwise it is an extracted AppDir, portable.

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

// The kind of the copy in `exeDir`, with `appImageEnv` as $APPIMAGE.
PackageInfo detectPackageKind(const QString &exeDir, const QString &appImageEnv);
// This process's kind. Test builds (HIKARI_UPDATE_TEST_FEED) take
// HIKARISUB_PACKAGE_KIND (and HIKARISUB_AUR_CHANNEL) first, so the AUR
// notice and the other flows can be tried without installing a package.
PackageInfo currentPackageKind();

bool onWindows();
// The marker's token (`portable-linux` on Linux without one), as
// `--version` prints it.
QString packageKindToken(application::PackageKind kind);
// The kind as About names it: "AppImage", "AUR", "deb", "rpm", "installer"
// or "portable".
QString packageKindLabel(application::PackageKind kind);

} // namespace hikari::app
