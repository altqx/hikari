#pragma once

// The Lua helper protocol over the helper framing. One helper process per loaded script; its Lua
// state persists between requests.
//
// Requests start with an i32 Command:
//   Load: str script path, str shared include directory, i32 trace level (aegisub.debug.out
//         levels above it are dropped), str compile cache directory (version 10; empty: none, so
//         MoonScript compiles every load). Terminal Ok carries the ScriptInfo; Failed carries the
//         error text. A helper loads one script once.
//   Run:  i32 macro index, then a MacroSnapshot. Progress events and Service calls while it runs;
//         Terminal Ok carries the MacroResult, Failed the error text, or Cancelled.
//   RunValidated: as Run, but the macro's validation function (when it has one) runs first on
//         the same subtitles object. When it returns false or raises an error the macro does not
//         run: Terminal Ok carries a MacroResult with valid false (and the error text, if any).
//   Validate: as RunValidated, but the macro never runs: Terminal Ok carries a MacroResult with
//         no lists, valid true or false (and the error text). What validation staged is dropped.
//   RunNodes (version 9): a Lua node's batch, a NodeCall. It needs no Load. Terminal Ok carries
//         NodeRows, Failed the error text. A version 8 helper is refused at the handshake, so it
//         never sees this command.
// The ScriptInfo lists the files the script's top level loaded as Lua code (require and include).
// Progress payloads start with an i32 ProgressEvent: Log (i32 level, -1 when aegisub.debug.out
// was given none; str), Set (f64 percent), Task (str), Title (str).
// Service payloads start with an i32 Service; Dialog carries a DialogRequest and is answered with
// a DialogResult; Host carries a HostServiceRequest (without the identity, which the application
// side fills in) and is answered with a HostServiceReply.

#include "hikari/application/automation.h"
#include "hikari/application/node_effects.h"
#include "hikari/backends/helper_protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hikari::backends::lua {

inline constexpr std::uint32_t kProtocolVersion = 11;
inline constexpr char kHelperName[] = "hikari-lua-helper";

enum class Command : std::int32_t { Load = 1, Run = 2, RunValidated = 3, Validate = 4, RunNodes = 5 };
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

using NodeRows = std::vector<std::vector<application::NodeValue>>;
// A value is written by its port's type: Number, Time and Alpha f64, Text and Tags str, Bool u8,
// Colour three i32, Point two f64. Other types have no Lua form and are refused.
std::vector<std::byte> encodeNodeCall(const application::LuaNodePort::Call &call);
// Without the leading Command field.
std::optional<application::LuaNodePort::Call> decodeNodeCall(helper::Reader &in, std::size_t payloadSize);
std::vector<std::byte> encodeNodeRows(const NodeRows &rows, const std::vector<application::PortSpec> &outputs);
std::optional<NodeRows> decodeNodeRows(const std::vector<std::byte> &payload,
                                       const std::vector<application::PortSpec> &outputs);
bool isLuaNodeType(application::PortType type);

}
