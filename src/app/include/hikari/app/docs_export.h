#pragma once

#include <QString>

namespace hikari::app {

// DX1 (#367): `HikariSub --export-docs <dir>`, the reference the site's
// hotkeys and settings pages are made from (altqx/hikari-site), written as
// four JSON files into `dir` (made if missing):
// - commands.json: every command the Hotkeys page lists (not the retired
//   ones), by id: its symbol, English name, window, default keys per window
//   and its places in the menu bar and the Grid's menu;
// - settings.json: the Settings window's pages and every control bound to a
//   setting, in page order: its page, section, label, hint, control, type,
//   the value a new profile has and its range or entries; a setting only
//   one platform's window shows has `platform` ("windows"), so the export
//   of any platform lists every setting;
// - guide-links.json: User guide's pages (guide_links.h);
// - meta.json: the version, the commit the build was configured at and the
//   schema (kDocsSchema).
// The program composes as it does at a start, over a new profile in a
// temporary folder and in English, makes the main window and the Settings
// window without showing them, and reads them. Two exports of one build give
// the same bytes. Needs a QGuiApplication (run() starts one on the offscreen
// platform). False, with `error` set, when the window cannot be made or a
// file cannot be written.
inline constexpr int kDocsSchema = 1;
bool exportDocs(const QString &dir, QString *error = nullptr);

} // namespace hikari::app
