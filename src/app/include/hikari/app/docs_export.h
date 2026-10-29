#pragma once

#include <QString>

namespace hikari::app {

// Writes commands.json, settings.json, guide-links.json and meta.json into `dir`, made if
// missing. Needs a QGuiApplication. False, with `error` set, when a window or file fails.
inline constexpr int kDocsSchema = 1;
bool exportDocs(const QString &dir, QString *error = nullptr);

}
