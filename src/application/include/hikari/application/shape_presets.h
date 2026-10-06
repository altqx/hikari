#pragma once

// T5: the drawing tool's shape presets (legacy ShapesSetting, LoadSettings,
// SaveSettings and the ShapesEdition dialog, HikariSub/VisualDrawingShapes.h
// and .cpp at 20d647c4; docs/qt/visual-tools.md, "Drawing and shape
// presets"). The presets live in Config/ShapesSettings.txt beside the
// settings, one "Shape: <name>; <shape>; <mode>; <scaling mode>" line each;
// without the file legacy's five defaults are used.
//
// Saving a preset under a name another preset has asks to replace it or to
// rename (accepted on #55, surface-decision-routing.md); legacy kept both.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

// Legacy ShapesSetting (VisualDrawingShapes.h:36-64).
struct ShapePreset {
    // "Scaling relative to cursor": the drag's rectangle keeps the shape's
    // proportions in BothScaleX (Shift toggles it).
    enum Mode : int { SeparateScaleXY = 0, BothScaleX = 1, OnlyScaleX = 2 };
    // "Scaling mode": ChangeScale also writes \fscx (and \fscy unless
    // OnlyScaleX). The dialog's choice for it is disabled (legacy).
    enum Scaling : int { ChangePoints = 0, ChangeScale = 1 };
    std::u16string name;
    std::u16string shape;
    int mode = 0;        // unsigned char in legacy: read values wrap at 256
    int scalingMode = 0; // ditto
    bool operator==(const ShapePreset &) const = default;
};

// LoadSettings' defaults (VisualDrawingShapes.cpp:299-303), as the text it
// parses: rectangle, circle and three rounded squares.
std::u16string_view defaultShapePresetsText();
std::vector<ShapePreset> defaultShapePresets();

// LoadSettings' reader (VisualDrawingShapes.cpp:305-330): lines split at
// "\n" (empty ones skipped); a line starting "Shape: " is split at ";" (runs
// of ";" count as one, as wxTOKEN_STRTOK does) into the name (as written),
// the shape (blanks before it dropped) and the two modes (blanks before
// them dropped, then the whole rest a base-10 integer, wxString::ToLong).
// A line without all four, or with a mode that does not read, is skipped.
std::vector<ShapePreset> parseShapePresets(std::u16string_view text);
// SaveSettings (VisualDrawingShapes.cpp:341-353): "Shape: <name>; <shape>;
// <mode>; <scaling>\n" a preset. The file gets a UTF-8 BOM first.
std::u16string writeShapePresets(const std::vector<ShapePreset> &presets);

// The list legacy's VectorItem shows for the drawing (VideoToolbar.cpp:
// 506-510): "Choose" (no shape: free drawing), the presets' names, "Edit"
// (opens the editor).
std::vector<std::u16string> shapeListChoices(const std::vector<ShapePreset> &presets);

// The ShapesEdition dialog's state and actions (VisualDrawingShapes.cpp:
// 43-288), "Vector shape editing". It edits a copy of the presets; Apply
// keeps an edit in the copy, OK keeps it and gives the copy back to be
// saved, Cancel drops the copy. The fields (name, shape, the two modes) are
// what the dialog shows; `current` is legacy's currentShape, what they were
// last set from or saved to.
class ShapesEdition {
public:
    // A legacy message box: its text and title. Error boxes have OK only.
    struct Message {
        std::u16string text;
        std::u16string title;
    };

    // curShape is a preset's index; out of range the first (the toolbar
    // gives its list's selection less "Choose", T5-editor-opens-next).
    ShapesEdition(std::vector<ShapePreset> presets, int curShape);

    const std::vector<ShapePreset> &presets() const { return m_presets; }
    // The dialog's list of presets as legacy's HikariChoice holds it: names
    // are added and removed with the presets, and a save puts a rename
    // there (T5-dialog-list-stale: legacy never refreshed it).
    const std::vector<std::u16string> &list() const { return m_list; }
    int selection() const { return m_selection; }
    const ShapePreset &current() const { return m_current; }

    // The fields.
    std::u16string name, shape;
    int mode = 0, scalingMode = 0;
    static constexpr int kNameMaxLength = 20; // shapeName->SetMaxLength(20): typing only

    // OnAddShape: a new empty preset named `newName`, selected. Refused with
    // a message when the name is empty or the list holds it (ignoring case).
    std::optional<Message> addShape(std::u16string_view newName);
    // OnRemoveShape: the selected preset goes; refused for the last one.
    std::optional<Message> removeShape();
    // CheckModified: a field differs from `current`.
    bool modified() const;
    // OnListChanged's question when modified(): "Save changes to shape
    // \"%s\"?" with the preset's name (T5-save-question-text: legacy's
    // showed the shape's text).
    Message saveChangesQuestion() const;
    // SetShape: the list's `index` (out of range: the first) becomes the
    // selection and the fields show it.
    void select(int index);
    // OnGetShapeFromLine on the active Line's text: every drawing in it
    // (ParseTags "p": the text after a \p other than \p0) goes into the
    // shape field; when the current preset already has a shape each one
    // becomes a new preset "Untitled" (only the name and shape fields
    // change). False when the Line has no drawing.
    bool getShapeFromLine(std::u16string_view lineText);

    // Save (VisualDrawingShapes.cpp:267-288), for Apply and OK: the fields
    // become the selected preset. An empty shape is refused; an empty name
    // becomes "Untitled". When another preset has the name (ignoring case)
    // nothing is saved and `clash` names it: replaceClash() replaces it,
    // or the user renames and saves again.
    struct SaveResult {
        std::optional<Message> error;
        std::optional<std::u16string> clash;
        bool saved() const { return !error && !clash; }
    };
    SaveResult save();
    // The answer Replace: the other preset with the clashing name goes and
    // the held edit is saved over the selection (the answer Rename keeps
    // nothing; the fields stay as typed). Returns the selection's new index; `pending` (a
    // list index the caller still wants to move to, or -1) is given back
    // adjusted for the removal (-1 if it was the removed preset).
    int replaceClash(int *pending = nullptr);

    // OnResetDefault after its question ("Are you sure you want to reset to
    // default?") and the file's removal: the presets are `defaults`, the
    // list keeps the selected name's place if it is there (else the first,
    // HikariChoice::PutArray) and the fields show it.
    void restoreDefaults(std::vector<ShapePreset> defaults);
    static Message restoreQuestion();

private:
    void showCurrent(); // SetShapeFromSettings
    std::vector<ShapePreset> m_presets;
    std::vector<std::u16string> m_list;
    ShapePreset m_current;
    int m_selection = 0;
    std::optional<ShapePreset> m_pending; // the edit a name clash holds back
};

// wxString::IsSameAs(other, false) / wxArrayString::Index(s, false): equal
// ignoring case.
bool sameNameIgnoringCase(std::u16string_view a, std::u16string_view b);

} // namespace hikari::application::visual
