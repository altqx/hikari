#pragma once

// The Lua helper protocol (N8) over the H1 framing. One helper process per
// loaded script; its Lua state persists between requests.
//
// Requests start with an i32 Command:
//   Load: str script path, str shared include directory, i32 trace level
//         (aegisub.debug.out levels above it are dropped). Terminal Ok
//         carries the ScriptInfo; Failed carries the error text. A helper
//         loads one script once; reloading is a new helper (top-level code
//         runs again, visibly).
//   Run:  i32 macro index. Progress events and Service calls while it runs;
//         Terminal Ok, Failed (error text), or Cancelled (aegisub.cancel() or
//         a host Cancel).
// Progress payloads start with an i32 ProgressEvent: Log (str), Set (f64
// percent), Task (str), Title (str).
// Service payloads start with an i32 Service; Dialog carries a DialogRequest
// and is answered with a DialogResult.

#include "hikari/application/automation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hikari::backends::lua {

inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr char kHelperName[] = "hikari-lua-helper";

enum class Command : std::int32_t { Load = 1, Run = 2 };
enum class ProgressEvent : std::int32_t { Log = 1, Set = 2, Task = 3, Title = 4 };
enum class Service : std::int32_t { Dialog = 1 };

std::vector<std::byte> encodeInfo(const application::ScriptInfo &info);
std::optional<application::ScriptInfo> decodeInfo(const std::vector<std::byte> &payload);

// Without the leading Service field.
std::vector<std::byte> encodeDialogRequest(const application::DialogRequest &request);
std::optional<application::DialogRequest> decodeDialogRequest(const std::vector<std::byte> &payload);

std::vector<std::byte> encodeDialogResult(const application::DialogResult &result);
// Rejects a result that does not fit its request: a wrong value count or
// type, or a button index out of range.
std::optional<application::DialogResult> decodeDialogResult(const std::vector<std::byte> &payload,
                                                            const application::DialogRequest &request);

} // namespace hikari::backends::lua
