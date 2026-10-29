#pragma once

// T5: the drawing tool's shape presets (legacy ShapesSetting, LoadSettings,
// SaveSettings and the ShapesEdition dialog, HikariSub/VisualDrawingShapes.h
// and .cpp at 20d647c4; docs/visual-tools.md, "Drawing and shape
// presets"). The presets live in shapes.toml in the user folder (CF2,
// ui/list_files.h); legacy's Config/ShapesSettings.txt, one "Shape: <name>;
// <shape>; <mode>; <scaling mode>" line each, is converted into it once.
// Without the file legacy's five defaults are used.
//
// A name another preset has asks to replace it (accepted on #55, PU5's
// message box on #307); legacy kept both.

#include "hikari/application/edit_history.h"

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

// PU5 (#307): the "Edit shapes" window's model (legacy ShapesEdition,
// "Vector shape editing", VisualDrawingShapes.cpp:43-288, redesigned). Every
// change is kept as it is made and is a step of the window's history: the
// caller hands the presets back to the drawing tool and writes the file
// after each one. A value the tool cannot use is refused and the preset
// keeps its last good one. The questions and their answers are the
// window's: a name another preset has is refused here and replaceNamed()
// replaces that preset; restoreDefaults() after its question.
class ShapesEdition {
public:
    // The window's steps (it names them for Undo and Redo).
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
    // Why a name is refused.
    enum class NameCheck { Ok, Empty, Taken, Separator };

    // `selection` is a preset's index; out of range the first (the toolbar
    // gives its list's selection less "Choose", T5-editor-opens-next).
    ShapesEdition(std::vector<ShapePreset> presets, int selection);

    const std::vector<ShapePreset> &presets() const { return m_history.items(); }
    int selection() const { return m_history.selection(); }
    // The selected preset (an empty one when there are none).
    const ShapePreset &current() const;
    // Another preset chosen: no step.
    void select(int index);
    static constexpr int kNameMaxLength = 20; // shapeName->SetMaxLength(20): typing only

    // A name for the selected preset: empty, holding ";" or a line break
    // (the file's separators), or another preset's (ignoring case: `taken`
    // gets its name) is refused; else it is the preset's name.
    NameCheck setName(std::u16string_view name, std::u16string *taken = nullptr);
    // The drawing: line breaks become spaces; empty (or only blanks) or
    // holding ";" is refused.
    bool setShape(std::u16string_view shape);
    // "Scale from the cursor" (ShapePreset::Mode).
    void setMode(int mode);
    // A field left: its next change is a step of its own.
    void endTyping() { m_history.endMerge(); }

    // New shape: the last drawing in `lineText` (the active Line's), else a
    // 100 x 100 square, named "New shape" ("New shape 2", ...), selected.
    void addShape(std::u16string_view lineText);
    // Duplicate: the selected preset as "<name> copy" ("<name> copy 2", the
    // name cut to fit 20 characters), after it, selected.
    void duplicate();
    // Delete: the selected preset goes; never the last one (false).
    bool remove();
    bool canRemove() const { return presets().size() > 1; }
    // Take from the active line: the last drawing in `lineText` replaces
    // the selected preset's (ParseTags "p": the text after a \p other than
    // \p0). False when the Line has none.
    bool takeFromLine(std::u16string_view lineText);
    // The answer Replace to a taken name: the preset that has it goes and
    // the selected one takes `name`, one step.
    void replaceNamed(std::u16string_view name);
    // Restore default shapes, after its question: the presets are
    // `defaults`, the selected name's place kept when it is there (else the
    // first, HikariChoice::PutArray), one step.
    void restoreDefaults(std::vector<ShapePreset> defaults);
    // Undo all changes: the presets the window opened with, one step.
    bool undoAll() { return m_history.undoAll(UndoAll); }

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    int undoStep() const { return m_history.undoStep(); }
    int redoStep() const { return m_history.redoStep(); }
    bool undo() { return m_history.undo(); }
    bool redo() { return m_history.redo(); }
    // What the last change, Undo, Redo or Undo all renamed (Name, Replace):
    // the toolbar's choice follows it.
    const Renames &renamed() const { return m_history.renamed(); }
    std::uint64_t revision() const { return m_history.revision(); }

private:
    void edit(ShapePreset preset, Step step, std::string mergeKey = {}, Renames renamed = {});
    std::u16string freeName(std::u16string base, bool copy) const;
    EditHistory<ShapePreset> m_history;
};

// wxString::IsSameAs(other, false) / wxArrayString::Index(s, false): equal
// ignoring case.
bool sameNameIgnoringCase(std::u16string_view a, std::u16string_view b);

} // namespace hikari::application::visual
