#pragma once

// The plugin helper protocol over the helper framing. One helper process per running plugin.
//
// Requests start with an i32 Command:
//   Load: a LoadRequest. Runs the main file once. Terminal Ok carries an i32 count and the
//         command ids that have handlers; Failed carries the error text. A helper loads once.
//   Run:  str command id, as the manifest declares it. Terminal Ok, or Failed with the error text.
// Events carry str name and str JSON data. Each handler for the name runs in turn; Terminal Ok,
// or Failed with the first error text.
// Service payloads start with an i32 Service. HikariSub answers Ok unless the request has ended:
//   Call: str operation, str JSON params. Ok: str JSON, {"result": …} or {"error": {code, message, data}}.
//   Notify: i32 Level, u8 toast, str text.
//   SetSetting: str id, str JSON value. Ok: str refusal, empty when the value is kept.
//   Subscribe: str event name. Ok: str refusal, empty when subscribed.
//   Lock: takes the app-wide edit lock, waiting behind other plugins. Ok: str refusal, empty when held.
//   Unlock: releases it. Ok, empty.
//   Ui: str operation, str JSON params, for the plugin's panels and dialogs. Ok: the Call envelope.

#include "hikari/backends/helper_protocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hikari::backends::plugin {

inline constexpr std::uint32_t kProtocolVersion = 3;
inline constexpr char kHelperName[] = "hikari-plugin-helper";

enum class Command : std::int32_t { Load = 1, Run = 2 };
enum class Service : std::int32_t { Call = 1, Notify = 2, SetSetting = 3, Subscribe = 4, Lock = 5, Unlock = 6, Ui = 7 };
enum class Level : std::int32_t { Info = 0, Warning = 1, Error = 2 };
enum class Tier : std::int32_t { Restricted = 0, Unrestricted = 1 };

struct LoadRequest {
    std::string root; // the installed package folder
    std::string main; // the main file's package path
    std::string id;
    std::string version;
    Tier tier = Tier::Restricted;
    std::int64_t memoryLimit = 0;
    std::string locale;   // the UI language, as "pt_BR"
    std::string settings; // a JSON object: every declared setting's current value
    std::vector<std::string> commands; // the declared command ids
};

std::vector<std::byte> encodeLoad(const LoadRequest &load);
// After the leading Command.
std::optional<LoadRequest> decodeLoad(helper::Reader &in);

std::vector<std::byte> encodeStrings(const std::vector<std::string> &items);
std::optional<std::vector<std::string>> decodeStrings(const std::vector<std::byte> &payload);

}
