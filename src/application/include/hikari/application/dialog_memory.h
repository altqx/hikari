#pragma once

#include "hikari/application/automation.h"

#include <map>
#include <optional>
#include <string>

namespace hikari::application {

// A dialog's values by control name; labels and unnamed controls have none.
using NamedDialogValues = std::map<std::string, DialogValue>;

// Same controls in order (class, name, place, span, dropdown items, label,
// hint) and same buttons (labels, ids); the values may differ.
bool sameDialog(const DialogRequest &a, const DialogRequest &b);

// Labels and buttons only.
bool isQuestion(const DialogRequest &request);

// No button (closed, Esc), the default pair's Cancel, or a button with id cancel.
bool cancelAnswer(const DialogRequest &request, int pressed);

// An integer from a whole number, a number from an integer; nothing for
// another type or a label. Not clamped.
std::optional<DialogValue> dialogValueOf(const DialogControl &control, const DialogValue &value);

// Of the control's type, a number clamped to its range, a dropdown value one
// of its items; nothing otherwise.
std::optional<DialogValue> acceptedDialogValue(const DialogControl &control, const DialogValue &value);

// A recorded value the controls no longer take stays the script's.
DialogResult replayedAnswer(const DialogRequest &request, const DialogResult &recorded);

NamedDialogValues namedDialogValues(const DialogRequest &request, const DialogResult &result);

// Sets the controls `values` names, where the control takes the value.
DialogResult filledByName(const DialogRequest &request, DialogResult base, const NamedDialogValues &values);

struct LastDialogAnswer {
    NamedDialogValues sent;
    NamedDialogValues answered;
    int pressed = -1;
    bool operator==(const LastDialogAnswer &) const = default;
};

LastDialogAnswer lastDialogAnswer(const DialogRequest &request, const DialogResult &answer);

// A control gets its last answer only where the script sent the same value
// as last time.
DialogResult safePrefill(const DialogRequest &request, const LastDialogAnswer &last);

}
