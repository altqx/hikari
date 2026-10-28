#pragma once

// AD2 (#311): what script dialogs keep between runs (docs/automation.md).
// Repeat last run sends a dialog's recorded answer again, without showing
// it, when the script shows the same dialog at the same point of the run
// (AD2-repeat-last-run). Last used, presets, Reset and "Fill in my last
// answers" fill a dialog by control name (AD2-dialog-memory,
// AD2-fill-last-answers). Only the values shown before a button is pressed
// change: what a script gets back keeps Aegisub's shape.

#include "hikari/application/automation.h"

#include <map>
#include <optional>
#include <string>

namespace hikari::application {

// A dialog's values by control name; labels and unnamed controls have none.
using NamedDialogValues = std::map<std::string, DialogValue>;

// The same dialog for Repeat: the same controls in order (class, name,
// place, span, dropdown items, label and hint text) and the same buttons
// (labels and ids). The values the script sends may differ; what the dialog
// says may not, so a prompt naming something else (a trust question for
// another feed, say) is shown again rather than answered as before.
bool sameDialog(const DialogRequest &a, const DialogRequest &b);

// A dialog with nothing to fill in, labels and buttons only: a question (a
// trust prompt, a confirmation). Repeat asks it again rather than answering
// it as before, since a button there is a decision, not typing to save.
bool isQuestion(const DialogRequest &request);

// Whether `pressed` answers the dialog as cancelled: no button (the window
// closed, Esc), the default pair's Cancel, or a button whose id is cancel.
bool cancelAnswer(const DialogRequest &request, int pressed);

// `value` as a value of `control`'s type: an integer from a whole number,
// a number from an integer, nothing for another type or a label. Not
// clamped.
std::optional<DialogValue> dialogValueOf(const DialogControl &control, const DialogValue &value);

// `value` as `control` takes it: of its type, a number clamped to its
// range, a dropdown value one of its items; nothing otherwise.
std::optional<DialogValue> acceptedDialogValue(const DialogControl &control, const DialogValue &value);

// The recorded answer to the same dialog, sent again: its button, and its
// values as the controls take them now (a value they no longer take: the
// script's).
DialogResult replayedAnswer(const DialogRequest &request, const DialogResult &recorded);

NamedDialogValues namedDialogValues(const DialogRequest &request, const DialogResult &result);

// `base` (one value per control of `request`) with the controls `values`
// names set to their value where the control takes it.
DialogResult filledByName(const DialogRequest &request, DialogResult base, const NamedDialogValues &values);

// A dialog's last answer: what the script sent, what was answered and the
// button.
struct LastDialogAnswer {
    NamedDialogValues sent;
    NamedDialogValues answered;
    int pressed = -1;
    bool operator==(const LastDialogAnswer &) const = default;
};

LastDialogAnswer lastDialogAnswer(const DialogRequest &request, const DialogResult &answer);

// The safe-prefill rule (AD2-fill-last-answers): the script's values, with
// a control's last answer where the script sent the value it sent last
// time. A value the script works out itself wins.
DialogResult safePrefill(const DialogRequest &request, const LastDialogAnswer &last);

} // namespace hikari::application
