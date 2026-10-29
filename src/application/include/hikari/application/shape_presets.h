#pragma once

#include "hikari/application/edit_history.h"

#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

struct ShapePreset {
    // In BothScaleX the drag's rectangle keeps the shape's proportions (Shift
    // toggles it).
    enum Mode : int { SeparateScaleXY = 0, BothScaleX = 1, OnlyScaleX = 2 };
    // ChangeScale also writes \fscx (and \fscy unless OnlyScaleX).
    enum Scaling : int { ChangePoints = 0, ChangeScale = 1 };
    std::u16string name;
    std::u16string shape;
    int mode = 0;        // read values wrap at 256
    int scalingMode = 0; // read values wrap at 256
    bool operator==(const ShapePreset &) const = default;
};

std::u16string_view defaultShapePresetsText();
std::vector<ShapePreset> defaultShapePresets();

// Lines starting "Shape: " are split at ";" (runs count as one) into the
// name, the shape and two base-10 modes; a line missing a field or with an
// unreadable mode is skipped.
std::vector<ShapePreset> parseShapePresets(std::u16string_view text);
// One "Shape: <name>; <shape>; <mode>; <scaling>" line per preset.
std::u16string writeShapePresets(const std::vector<ShapePreset> &presets);

// "Choose" (free drawing), the presets' names, then "Edit".
std::vector<std::u16string> shapeListChoices(const std::vector<ShapePreset> &presets);

// Every change is a history step; a refused value keeps the last good one.
// Names compare ignoring case.
class ShapesEdition {
public:
    enum Step : int {
        Rename,
        EditDrawing,
        ChangeScaling,
        NewShape,
        DuplicateShape,
        DeleteShape,
        TakeFromLine,
        ReplaceShape,
        RestoreDefaults,
        UndoAll
    };
    enum class NameCheck { Ok, Empty, Taken, Separator };

    // An out-of-range `selection` selects the first.
    ShapesEdition(std::vector<ShapePreset> presets, int selection);

    const std::vector<ShapePreset> &presets() const { return m_history.items(); }
    int selection() const { return m_history.selection(); }
    // An empty preset when there are none.
    const ShapePreset &current() const;
    // Not a history step.
    void select(int index);
    static constexpr int kNameMaxLength = 20; // limits typing only

    // Refuses an empty name, ";" or a line break, or another preset's name
    // (ignoring case; `taken` gets it).
    NameCheck setName(std::u16string_view name, std::u16string *taken = nullptr);
    // Line breaks become spaces; blank or holding ";" is refused.
    bool setShape(std::u16string_view shape);
    // A ShapePreset::Mode.
    void setMode(int mode);
    // The next change starts a new history step.
    void endTyping() { m_history.endMerge(); }

    // Appends and selects "New shape" (numbered when taken) with the last
    // drawing in `lineText`, else a 100 x 100 square.
    void addShape(std::u16string_view lineText);
    // Inserts "<name> copy" (cut to fit 20 characters) after the selected one
    // and selects it.
    void duplicate();
    // False for the last preset.
    bool remove();
    bool canRemove() const { return presets().size() > 1; }
    // The last drawing in `lineText` (after a \p other than \p0) replaces the
    // selected preset's; false when there is none.
    bool takeFromLine(std::u16string_view lineText);
    // Drops the preset named `name` and renames the selected one to it, one step.
    void replaceNamed(std::u16string_view name);
    // Keeps the selected name selected when `defaults` have it, one step.
    void restoreDefaults(std::vector<ShapePreset> defaults);
    // Back to the presets it opened with, one step.
    bool undoAll() { return m_history.undoAll(UndoAll); }

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    int undoStep() const { return m_history.undoStep(); }
    int redoStep() const { return m_history.redoStep(); }
    bool undo() { return m_history.undo(); }
    bool redo() { return m_history.redo(); }
    // Names changed by the last change, Undo, Redo or Undo all.
    const Renames &renamed() const { return m_history.renamed(); }
    std::uint64_t revision() const { return m_history.revision(); }

private:
    void edit(ShapePreset preset, Step step, std::string mergeKey = {}, Renames renamed = {});
    std::u16string freeName(std::u16string base, bool copy) const;
    EditHistory<ShapePreset> m_history;
};

bool sameNameIgnoringCase(std::u16string_view a, std::u16string_view b);

} // namespace hikari::application::visual
