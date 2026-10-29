#pragma once

// Session files (last-session.json, .hksession):
//
//   {
//     "version": 1,
//     "program": "HikariSub 0.1.0",
//     "closed": true,              <- last-session.json only
//     "tabs": [
//       { "subtitles": "<path>", "video": "<path>", "position": <ms>,
//         "audio": "<path>", "keyframes": "<path>",
//         "activeLine": <row>, "scroll": <row>,
//         "layout": { <the tab's arrangement> } },
//       { "active": true, ... }    <- the active Document tab
//     ]
//   }
//
// "audio" and "keyframes" are left out when empty, "audio" also when it is the video, and
// "layout" when the tab has none. A file not starting with "{" is read as a legacy .kls.

#include "hikari/application/owned_file.h"
#include "hikari/application/session_file.h"

#include <QByteArray>

#include <optional>
#include <string_view>

namespace hikari::app {

// `closed` is written only for last-session.json.
QByteArray writeSessionJson(const application::Session &session, std::optional<bool> closed = std::nullopt);

// JSON or a legacy .kls; nullopt for empty bytes, or with `failure` set for a broken file.
std::optional<application::Session> readSession(std::string_view bytes, application::ParseFailure *failure = nullptr);

}
