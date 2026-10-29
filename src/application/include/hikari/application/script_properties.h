#pragma once

#include "hikari/application/edit_session.h"

#include <expected>
#include <string>
#include <vector>

namespace hikari::application {

struct ScriptProperties {
    std::u8string title, originalScript, originalTranslation, originalEditing, originalTiming, updatedBy;
    int playResX = 0, playResY = 0;     // missing values filled (1280x720, or 16:9 / 9:16)
    int layoutResX = 0, layoutResY = 0; // 0 when absent
    int matrix = 0;                     // index in matrixNames(); 0 (None) when absent or unknown
    int wrapStyle = 0;
    bool reverseCollisions = false;
    bool scaledBorderAndShadow = true;
};

// The dialog's YCbCr matrix choices: "None", "TV.601", "PC.601", ... "PC.240M".
const std::vector<std::u8string> &matrixNames();

// What the dialog opens with.
ScriptProperties scriptProperties(const core::Document &document);

struct ScriptPropertiesEdits {
    bool title = false, originalScript = false, originalTranslation = false, originalEditing = false,
         originalTiming = false, updatedBy = false, playResX = false, playResY = false, layoutResX = false,
         layoutResY = false;
};

// One undo step ("Changing the subtitle header"); none when nothing changed.
// Missing PlayRes values are filled; an empty Title becomes "HikariSub Ass File".
// A missing subtitle resolution is completed 16:9; a missing layout width stays 0.
// With linkResolutions and a LayoutResX present, the layout follows the subtitle
// resolution. An absent Collisions or ScaledBorderAndShadow is added as "Normal"/"yes".
std::expected<void, CommandRefusal> applyScriptProperties(EditSession &session, const ScriptProperties &edited,
                                                          const ScriptPropertiesEdits &edits, bool linkResolutions);

} // namespace hikari::application
