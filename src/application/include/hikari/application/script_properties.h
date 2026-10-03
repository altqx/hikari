#pragma once

// Y3: the ASS Script properties (legacy GLOBAL_OPEN_ASS_PROPERTIES:
// HikariSubFrame::OnAssProps and the ScriptInfo dialog at 20d647c4).

#include "hikari/application/edit_session.h"

#include <expected>
#include <string>
#include <vector>

namespace hikari::application {

struct ScriptProperties {
    std::u8string title, originalScript, originalTranslation, originalEditing, originalTiming, updatedBy;
    int playResX = 0, playResY = 0;     // as the dialog shows them (missing values filled, legacy GetASSRes)
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

// The fields the user changed (legacy IsModified on each text control).
struct ScriptPropertiesEdits {
    bool title = false, originalScript = false, originalTranslation = false, originalEditing = false,
         originalTiming = false, updatedBy = false, playResX = false, playResY = false, layoutResX = false,
         layoutResY = false;
};

// OK in the dialog ("Changing the subtitle header", one step):
// - missing PlayRes values are written as legacy GetASSRes fills them;
// - changed text fields are written; an empty Title becomes "HikariSub Ass
//   File" even when unchanged;
// - changed resolutions are written, a missing subtitle one completed 16:9
//   (a missing layout width legacy "completes" from itself, so it stays 0);
// - with linked resolutions and a LayoutResX present (legacy checks LayoutResX
//   twice) the layout follows the subtitle resolution;
// - the matrix, wrap style, collisions and border scaling are written when
//   they differ from the stored text, so an absent Collisions or
//   ScaledBorderAndShadow is added as "Normal"/"yes".
// Nothing changed adds no step.
std::expected<void, CommandRefusal> applyScriptProperties(EditSession &session, const ScriptProperties &edited,
                                                          const ScriptPropertiesEdits &edits, bool linkResolutions);

} // namespace hikari::application
