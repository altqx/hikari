#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace hikari::backends::desktop {

bool openUrl(const QUrl &url);

void selectInFolder(const QString &path);

bool startDetached(const QString &program, const QStringList &arguments);

struct Routes {
    std::function<bool(const QUrl &url)> openUrl;
    std::function<void(const QString &path)> selectInFolder;
    std::function<bool(const QString &program, const QStringList &arguments)> startDetached;
};

// For tests. A null route falls back to the desktop; returns the routes replaced.
Routes replaceRoutes(Routes routes);

}
