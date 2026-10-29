#pragma once

// The Lua helper protocol over the helper framing. One helper process per loaded script; its Lua
// state persists between requests.
//
// Requests start with an i32 Command:
//   Load: str script path, str shared include directory, i32 trace level (aegisub.debug.out
//         levels above it are dropped). Terminal Ok carries the ScriptInfo; Failed carries the
//         error text. A helper loads one script once.
//   Run:  i32 macro index, then a MacroSnapshot. Progress events and Service calls while it runs;
//         Terminal Ok carries the MacroResult, Failed the error text, or Cancelled.
//   RunValidated: as Run, but the macro's validation function (when it has one) runs first on
//         the same subtitles object. When it returns false or raises an error the macro does not
//         run: Terminal Ok carries a MacroResult with valid false (and the error text, if any).
//   Validate: as RunValidated, but the macro never runs: Terminal Ok carries a MacroResult with
//         no lists, valid true or false (and the error text). What validation staged is dropped.
// The ScriptInfo lists the files the script's top level loaded as Lua code (require and include).
// Progress payloads start with an i32 ProgressEvent: Log (i32 level, -1 when aegisub.debug.out
// was given none; str), Set (f64 percent), Task (str), Title (str).
// Service payloads start with an i32 Service; Dialog carries a DialogRequest and is answered with
// a DialogResult; Host carries a HostServiceRequest (without the identity, which the application
// side fills in) and is answered with a HostServiceReply.

#include "hikari/application/automation.h"
#include "hikari/backends/helper_protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hikari::backends::lua {

inline constexpr std::uint32_t kProtocolVersion = 8;
inline constexpr char kHelperName[] = "hikari-lua-helper";

enum class Command : std::int32_t { Load = 1, Run = 2, RunValidated = 3, Validate = 4 };
// Started: the macro itself begins, after its validation function.
enum class ProgressEvent : std::int32_t { Log = 1, Set = 2, Task = 3, Title = 4, Started = 5 };
enum class Service : std::int32_t { Dialog = 1, Host = 2 };

std::vector<std::byte> encodeInfo(const application::ScriptInfo &info);
std::optional<application::ScriptInfo> decodeInfo(const std::vector<std::byte> &payload);

// Without the leading Service field.
std::vector<std::byte> encodeDialogRequest(const application::DialogRequest &request);
std::optional<application::DialogRequest> decodeDialogRequest(const std::vector<std::byte> &payload);

std::vector<std::byte> encodeSnapshot(const application::MacroSnapshot &snapshot);
std::optional<application::MacroSnapshot> decodeSnapshot(helper::Reader &in, std::size_t payloadSize);
std::vector<std::byte> encodeMacroResult(const application::MacroResult &result);
std::optional<application::MacroResult> decodeMacroResult(const std::vector<std::byte> &payload);

// Without the leading Service field.
std::vector<std::byte> encodeHostRequest(const application::HostServiceRequest &request);
std::optional<application::HostServiceRequest> decodeHostRequest(const std::vector<std::byte> &payload);
std::vector<std::byte> encodeHostReply(const application::HostServiceReply &reply);
std::optional<application::HostServiceReply> decodeHostReply(const std::vector<std::byte> &payload);

std::vector<std::byte> encodeDialogResult(const application::DialogResult &result);
// Rejects a result that does not fit its request: a wrong value count or
// type, or a button index out of range.
std::optional<application::DialogResult> decodeDialogResult(const std::vector<std::byte> &payload,
                                                            const application::DialogRequest &request);

} // namespace hikari::backends::lua
