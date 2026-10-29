#pragma once

#include "hikari/application/automation.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/resample.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

inline constexpr std::string_view kScriptResolutionStep = "Setting the script resolution";

// atoi of each key's last entry, 0 when missing.
Resolution writtenScriptResolution(const core::Document &document);
Resolution writtenScriptResolution(const std::vector<MacroInfoLine> &info);

// Either value below 1.
bool lacksScriptResolution(Resolution written);

// 1280x720 when both are missing, a 16:9 partner when one is; writes nothing.
Resolution legacyScriptResolution(Resolution written);

// The video's size for both keys, even when one is written; else legacyScriptResolution.
Resolution suggestedScriptResolution(Resolution written, std::optional<Resolution> video);

// The key's last entry takes the value, else an entry is appended.
void setStagedScriptInfo(std::vector<MacroInfoLine> &info, std::string_view key, std::string value);
void setStagedScriptResolution(std::vector<MacroInfoLine> &info, Resolution resolution);

// One undo step. Refused for a non-ASS Document or a resolution below 1x1;
// no step when both keys already hold it.
std::expected<void, CommandRefusal> setScriptResolution(EditSession &session, Resolution resolution);

}
