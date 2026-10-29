#pragma once

// Y4-playres-ask (user decision 2026-10-13; docs/compatibility-decisions.md):
// legacy SubsGrid::GetASSRes (SubsGridBase.cpp:1653-1679 at 20d647c4) wrote a
// missing PlayResX/PlayResY into the Document whenever anything read the
// resolution, so a macro reading it (subtitles.script_resolution, karaskel's
// collect_head) saw 1280x720 or a 16:9 partner written silently. Here nothing
// writes on read: when a macro reads the resolution of a Document that lacks
// it, the user is asked to set it (prefilled from the open video, else
// legacy's values), or it is set from the video without asking when the user
// chose so; from the video, both keys take the video's size, even when the
// file has one of them. Without anyone to ask the macro gets legacy's values
// and nothing is written.

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

// PlayResX and PlayResY as written (wxAtoi of the last entry of each, 0 when
// missing), from a Document or from a macro's staged Script Info.
Resolution writtenScriptResolution(const core::Document &document);
Resolution writtenScriptResolution(const std::vector<MacroInfoLine> &info);

// GetASSRes's test: either value is missing or below 1.
bool lacksScriptResolution(Resolution written);

// GetASSRes's values for written ones (1280x720 for both missing, a 16:9
// partner for one), without writing anything.
Resolution legacyScriptResolution(Resolution written);

// What the question offers first for written values: the open video's own
// size (both keys, even when the file has one), else legacy's values.
Resolution suggestedScriptResolution(Resolution written, std::optional<Resolution> video);

// Document::setScriptInfo on a macro's staged Script Info: the last entry of
// that key takes the value, else the entry is added at the end.
void setStagedScriptInfo(std::vector<MacroInfoLine> &info, std::string_view key, std::string value);
// PlayResX and PlayResY on staged Script Info, as setScriptResolution writes them.
void setStagedScriptResolution(std::vector<MacroInfoLine> &info, Resolution resolution);

// PlayResX and PlayResY become `resolution` as one "Setting the script
// resolution" step. Refused for a Document that is not ASS or a resolution
// below 1x1; nothing changes (and no step is added) when both already hold it.
std::expected<void, CommandRefusal> setScriptResolution(EditSession &session, Resolution resolution);

} // namespace hikari::application
