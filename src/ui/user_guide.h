#pragma once

// DX1 (#367): User guide (GLOBAL_HELP) for QML: whether a surface has a page
// of the guide (application/guide_links.h) and the request to open one. The
// shell takes the request to the application (Main.qml:
// Application::openUserGuide, the desktop hand-off), so a DG1 window's "?"
// button and F1 open the page the same way.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class UserGuide : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // `kind` "panel" (a panel id) or "window" (a DG1 window's guideId).
    Q_INVOKABLE bool hasPage(const QString &kind, const QString &id) const;
    Q_INVOKABLE void open(const QString &kind, const QString &id) { emit openRequested(kind, id); }

signals:
    void openRequested(const QString &kind, const QString &id);
};

} // namespace hikari::ui
