#include "hikari/app/startup_look.h"
#include "theme.h"

#include <QFile>
#include <QSaveFile>

namespace hikari::app {

QByteArray startupLook(const ui::theme::Roles &roles)
{
    QByteArray out;
    for (const QColor &colour : {roles.background, roles.text, roles.muted, roles.accent})
        out += colour.name(QColor::HexRgb).toLatin1() + ' ';
    out.back() = '\n';
    return out;
}

void keepStartupLook([[maybe_unused]] QObject *context, [[maybe_unused]] const QString &file)
{
#ifdef _WIN32
    const auto write = [file] {
        const QByteArray look = startupLook(ui::theme::current().roles);
        QFile existing(file);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == look)
            return;
        QSaveFile out(file);
        if (out.open(QIODevice::WriteOnly) && out.write(look) == look.size())
            out.commit();
    };
    write();
    ui::theme::onChanged(context, write);
#endif
}

}
