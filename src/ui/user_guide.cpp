#include "user_guide.h"

#include "hikari/application/guide_links.h"

namespace hikari::ui {

bool UserGuide::hasPage(const QString &kind, const QString &id) const
{
    using application::GuideSurface;
    if (kind != QLatin1String("panel") && kind != QLatin1String("window"))
        return false;
    return !application::guidePage(kind == QLatin1String("panel") ? GuideSurface::Panel : GuideSurface::Window,
                                   id.toStdString())
                .empty();
}

}
