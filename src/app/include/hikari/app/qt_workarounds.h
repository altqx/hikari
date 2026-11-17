#pragma once

namespace hikari::app {

// Call before the first Qt Quick window renders. On Qt 6.12.0 a Text whose node becomes a batch
// root (table borders, code backgrounds) crashes the renderer once it updates: QTBUG-151670.
void avoidQtRendererCrashes();

}
