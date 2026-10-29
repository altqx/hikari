#pragma once

// CF3 (#490): sessions as JSON, last-session.json and the .hksession files of
// Save session:
//
//   {
//     "version": 1,
//     "program": "HikariSub 0.1.0",
//     "closed": true,              <- last-session.json only: the program closed
//     "tabs": [
//       { "subtitles": "<path>", "video": "<path>", "position": <ms>,
//         "audio": "<path>", "keyframes": "<path>",
//         "activeLine": <row>, "scroll": <row>,
//         "layout": { <the tab's arrangement, WS1's envelope> } },
//       { "active": true, ... }    <- the active Document tab
//     ]
//   }
//
// The fields are the legacy file's (application/session_file.h): "audio"
// and "keyframes" are left out when empty, "audio" also when it is the
// video, and "layout" when the tab has none. A legacy .kls still opens: a
// file that does not start with "{" is read with the legacy grammar.

#include "hikari/application/owned_file.h"
#include "hikari/application/session_file.h"

#include <QByteArray>

#include <optional>
#include <string_view>

namespace hikari::app {

// The JSON file's bytes. `closed` is written only for last-session.json.
QByteArray writeSessionJson(const application::Session &session, std::optional<bool> closed = std::nullopt);

// A session file's bytes, JSON or a legacy .kls: nullopt for empty bytes
// (legacy ignored an empty file), or with `failure` set for a broken file
// (its line when the JSON is malformed).
std::optional<application::Session> readSession(std::string_view bytes, application::ParseFailure *failure = nullptr);

} // namespace hikari::app
