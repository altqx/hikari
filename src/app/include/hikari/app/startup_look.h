#pragma once

#include <QByteArray>
#include <QString>

class QObject;

namespace hikari::ui::theme {
struct Roles;
}

namespace hikari::app {

// launcher::parseLook reads this back.
QByteArray startupLook(const ui::theme::Roles &roles);
// On Windows, writes the current theme's look to `file` now and after every theme change while `context` lives.
void keepStartupLook(QObject *context, const QString &file);

}
