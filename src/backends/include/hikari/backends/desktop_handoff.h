#pragma once

// 38e: every hand-off from the application to another program on the
// desktop goes through here, so tests can stand in for the desktop:
//  - openUrl: a web page or a local file or folder, opened by the desktop's
//    handler (QDesktopServices::openUrl; Help > HikariSub website, Report an
//    issue, an update's release page, Font Collector's folder on Linux);
//  - selectInFolder: a file shown selected in its folder (the tab menu's and
//    the recent files' folder items, a downloaded update's Show in folder;
//    show_in_folder.h);
//  - startDetached: a program started without waiting (the Lua script
//    editor; Explorer for Font Collector's folder on Windows).
// The application's own relaunches and installer runs (update_installer.cpp,
// update_apply.cpp, and composition.cpp's start for the files a quit left)
// are not hand-offs and do not come here.
//
// Tests replace the routes (tests/support/desktop/desktop_guard.cpp), so no
// test reaches a real browser, file manager or editor. The application never
// replaces them.

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace hikari::backends::desktop {

// The desktop's handler opens `url`; false when it could not.
bool openUrl(const QUrl &url);

// Legacy SelectInFolder (backends::selectInFolder).
void selectInFolder(const QString &path);

// QProcess::startDetached(program, arguments); false when it did not start.
bool startDetached(const QString &program, const QStringList &arguments);

struct Routes {
    std::function<bool(const QUrl &url)> openUrl;
    std::function<void(const QString &path)> selectInFolder;
    std::function<bool(const QString &program, const QStringList &arguments)> startDetached;
};

// Tests: the routes from now on, a null one being the desktop's own; returns
// the routes it replaces.
Routes replaceRoutes(Routes routes);

} // namespace hikari::backends::desktop
