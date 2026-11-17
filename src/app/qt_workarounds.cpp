#include "hikari/app/qt_workarounds.h"

#include <QByteArray>
#include <QLibraryInfo>
#include <QVersionNumber>

#include <initializer_list>
#include <limits>

namespace hikari::app {

void avoidQtRendererCrashes()
{
    if (QLibraryInfo::version() != QVersionNumber(6, 12, 0))
        return;
    for (const char *threshold : {"QSG_RENDERER_BATCH_NODE_THRESHOLD", "QSG_RENDERER_BATCH_VERTEX_THRESHOLD"})
        if (!qEnvironmentVariableIsSet(threshold))
            qputenv(threshold, QByteArray::number(std::numeric_limits<int>::max()));
}

}
