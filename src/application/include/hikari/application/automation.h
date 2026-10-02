#pragma once

// Plain data crossing the automation boundary (N8; docs/qt/automation.md).
// A script dialog is decoded from Lua by the helper (the compatibility
// adapter, which applies the legacy coercions) into this description; the
// application renders it with fixed QML controls and answers with typed
// values. No script-generated QML and no Lua values cross the boundary.

#include <cfloat>
#include <climits>
#include <string>
#include <variant>
#include <vector>

namespace hikari::application {

struct ScriptMacro {
    std::string name;
    std::string description;
    bool hasValidate = false;
    bool hasIsActive = false;
};

struct ScriptInfo {
    std::string name; // script_name, or the file name when the script sets none
    std::string description;
    std::string author;
    std::string version;
    std::vector<ScriptMacro> macros;
};

// One control. `kind` is the lowercased Lua class: label, edit, textbox,
// intedit, floatedit, dropdown, checkbox, color, coloralpha or alpha.
struct DialogControl {
    std::string kind;
    std::string name;
    std::string hint;
    int x = 0, y = 0, width = 1, height = 1;
    std::string label; // label and checkbox
    std::string text;  // edit, textbox, alpha, dropdown and colour values
    int intValue = 0, intMin = INT_MIN, intMax = INT_MAX;                // intedit
    double number = 0, numberMin = -DBL_MAX, numberMax = DBL_MAX, step = 0; // floatedit (step unused)
    bool checked = false;            // checkbox
    std::vector<std::string> items;  // dropdown
};

struct DialogRequest {
    std::vector<DialogControl> controls;
    // Button labels in order. Empty: the default pair, OK then Cancel.
    std::vector<std::string> buttons;
};

enum class DialogValueType { None, Text, Integer, Number, Boolean };

// What a control returns: nothing for a label, otherwise its typed value.
inline DialogValueType dialogValueType(const std::string &kind)
{
    if (kind == "label")
        return DialogValueType::None;
    if (kind == "intedit")
        return DialogValueType::Integer;
    if (kind == "floatedit")
        return DialogValueType::Number;
    if (kind == "checkbox")
        return DialogValueType::Boolean;
    return DialogValueType::Text;
}

using DialogValue = std::variant<std::monostate, std::string, int, double, bool>;

// Controls are read back whichever way the dialog ends (as in legacy), so a
// closed dialog still returns every value.
struct DialogResult {
    int pressed = -1;                // button index; -1 when the dialog was closed
    std::vector<DialogValue> values; // one per control, in request order
};

// The result of a dialog closed without any edit: every control's initial value.
inline DialogResult initialDialogResult(const DialogRequest &request)
{
    DialogResult result;
    for (const DialogControl &c : request.controls) {
        switch (dialogValueType(c.kind)) {
        case DialogValueType::None: result.values.emplace_back(); break;
        case DialogValueType::Text: result.values.emplace_back(c.text); break;
        case DialogValueType::Integer: result.values.emplace_back(c.intValue); break;
        case DialogValueType::Number: result.values.emplace_back(c.number); break;
        case DialogValueType::Boolean: result.values.emplace_back(c.checked); break;
        }
    }
    return result;
}

} // namespace hikari::application
