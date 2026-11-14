#pragma once

#include <QList>

class QObject;
class QWindow;

namespace hikari::ui {

/// Every object a window owns or shows: a side bar's panel is shown here while its closed dock owns it.
QList<QObject *> shownObjects(QWindow *window);

}
