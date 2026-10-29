#pragma once

// T6: the all-tags tool's tag definitions (legacy AllTagsSetting,
// LoadSettings, GetNames, SaveSettings and the AllTagsEdition dialog,
// HikariSub/VisualAllTagsEdition.h and .cpp at 20d647c4; docs/
// visual-tools.md, "The Position shifter and the all-tags tool"). The
// definitions live in tags.toml in the user folder (CF2, ui/list_files.h);
// legacy's Config/AllTagsSettings.txt, a "HYDRA2.0" line, then one "Tag:
// <name>, <tag>, <min>, <max>, <value>, <step>, <decimal places>, <placing>,
// <change option>[, <more values>]" line each, is converted into it once.
// Without the file legacy's 22 defaults are used.

#include "hikari/application/edit_history.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

// VisualAllTagsEdition.h:29-45. The tag's kind (AllTags::CheckTag) and the
// toolbar's "Tag change options".
enum AllTagsKind : int { IsHexAlpha = 1, IsHexColour = 2, IsVector = 4, IsTAnimation = 8 };
enum AllTagsPasteMode : int {
    PasteAdd = 0,
    PasteInsert,
    PasteMultiply,
    PasteMultiplyPlus,
    PasteGradientTextIncrease,
    PasteGradientTextDecrease,
    PasteGradientLineIncrease,
    PasteGradientLineDecrease
};

// Legacy AllTagsSetting (VisualAllTagsEdition.h:47-80): floats and unsigned
// chars as legacy keeps them.
struct AllTagsSetting {
    std::u16string name;
    std::u16string tag; // without the backslash
    float rangeMin = 0.f;
    float rangeMax = 0.f;
    float step = 0.f;
    std::array<float, 4> values{0.f, 0.f, 0.f, 0.f};
    // Where Insert puts the tag (FindTag's mode): 0 at the cursor, 1 at the
    // text's start, 2 (legacy's third choice).
    unsigned char mode = 0;
    unsigned char digitsAfterDot = 0;
    unsigned char numOfValues = 1;
    int tagMode = 0; // the change option the toolbar takes with the tag (AllTagsPasteMode)

    // AllTagsSetting(name) (VisualAllTagsEdition.h:64-69): a new tag named
    // and written `name`, step 1, range 0-100.
    static AllTagsSetting named(std::u16string name);
    bool operator==(const AllTagsSetting &) const = default;
};

// LoadSettings' defaults (VisualAllTagsEdition.cpp:338-360), as the text it
// parses, and the 22 definitions it gives.
std::u16string_view defaultAllTagsText();
std::vector<AllTagsSetting> defaultAllTags();

// LoadSettings (VisualAllTagsEdition.cpp:328-417) on the file's text (empty:
// no file or an empty one; a UTF-8 BOM already dropped). A text that does
// not start "HYDRA2.0" (an older version's, or none) gives the defaults, and
// `writeDefaults` says the defaults replace a text that was there. The lines
// after the first are split at "\n" (empty ones skipped); a line starting
// "Tag: " is split at "," (empty fields skipped, wxTOKEN_STRTOK) into the
// name (as written), the tag (blanks before it dropped), the minimum,
// maximum, value and step (blanks before them dropped, then the whole rest
// a C-locale number), the decimal places, placing and change option (wxAtoi)
// and up to three more values (each that reads one; the count follows the
// last that read). A line missing a field or a number that does not read is
// skipped.
std::vector<AllTagsSetting> parseAllTags(std::u16string_view text, bool *writeDefaults = nullptr);
// SaveSettings (VisualAllTagsEdition.cpp:428-446): "HYDRA2.0\n" before the
// first, then "Tag: " and the fields joined by ", ", the numbers as
// wxString's << writes a float ("%f"), the more values when there are any.
// The file gets a UTF-8 BOM first; no definitions write nothing.
std::u16string writeAllTags(const std::vector<AllTagsSetting> &tags);
// GetNames.
std::vector<std::u16string> allTagsNames(const std::vector<AllTagsSetting> &tags);

// Legacy NumCtrl for a double (NumCtrl.cpp): the text shown and the value
// read back clamped to its range, falling back to the last valid text typed.
class NumberField {
public:
    NumberField(double value, double rangeFrom, double rangeTo, bool intOnly);
    // What the field shows.
    const std::u16string &text() const { return m_text; }
    // The user typed: NumCtrl::OnNumWrite keeps a valid text in range as
    // the one to fall back to.
    void setText(std::u16string text);
    double getDouble() const; // NumCtrl::GetDouble
    int getInt() const { return static_cast<int>(getDouble()); }
    void setDouble(double value); // NumCtrl::SetDouble (getdouble's text)
    void setInt(int value);       // NumCtrl::SetInt
    bool intOnly() const { return m_intOnly; }
    double rangeFrom() const { return m_from; }
    double rangeTo() const { return m_to; }

private:
    double m_from, m_to;
    bool m_intOnly;
    std::u16string m_text, m_oldval;
    mutable double m_value = 0; // NumCtrl's value member
};
// NumCtrl.cpp's getdouble: "%f" without trailing zeros (and point).
std::u16string numCtrlText(double value);

// HY1's tag list sections (#271): legacy's everyday definitions (0,
// Common), the rest of its defaults (1, More), the user's own (2), by
// definition and by its name.
std::vector<int> allTagsSections(const std::vector<AllTagsSetting> &tags);
// The names the Hydra strip gives a definition's values (HY1): one value is
// "Value"; several are R G B for a colour, X Y for \pos, In Out for \fad,
// Start End for \t, else 1 to 4. As many as the definition takes.
std::vector<std::u16string> allTagsValueNames(const AllTagsSetting &tag);
// What the all-tags tool's Set writes for the definition at its starting
// values where the Line has no such tag (AllTagsTool::getTagValue without a
// value: \<tag> and the value as its decimal places give it; the alphas
// "&H..&", the colours "&HBBGGRR&" from R G B, several values in brackets).
std::u16string allTagsSetText(const AllTagsSetting &tag);

// PU5 (#307): the "Edit tags" window's model (legacy AllTagsEdition, "Tag
// editing", VisualAllTagsEdition.cpp:155-455, redesigned). Every change is
// kept as it is made and is a step of the window's history: the caller
// hands the definitions back to the tool and writes the file after each
// one (usable(): a new definition reaches them once it has its tag). A
// value the tool cannot use is refused with its reason and the definition
// keeps its last good one: legacy's Save checks (an empty tag, the maximum
// not above the minimum, the step not above zero or above half the range),
// a number that does not read or lies outside -10000 to 10000 (NumCtrl's
// range; decimal places 0 to 6, whole), and a name that is empty, holds
// the file's separator "," or another definition's (ignoring case:
// replaceNamed() replaces that one, the window asking first).
class AllTagsEdition {
public:
    // The window's steps (it names them for Undo and Redo).
    enum Step : int {
        Rename,
        EditTag,
        EditRange,
        EditStep,
        EditDecimals,
        EditCount,
        EditValue,
        ChangeOption,
        ChangePlacing,
        NewTag,
        DuplicateTag,
        DeleteTag,
        ReplaceTag,
        RestoreDefaults,
        UndoAll
    };
    enum class Check {
        Ok,
        Empty,
        Taken,
        Separator,
        NotANumber,
        OutOfRange,
        MaxNotAboveMin,
        MinNotBelowMax,
        StepNotPositive,
        StepTooLarge,
        TakenLastUsable // taken by the last definition with a tag, this one without its tag yet
    };
    // The number fields: the range, the step, the decimal places and the
    // starting values.
    enum class Number { Min, Max, Step, Digits, Value0, Value1, Value2, Value3 };
    static constexpr double kNumberLimit = 10000.0; // NumCtrl's -10000 to 10000
    static constexpr int kMaxDigits = 6;

    // `selection` is a definition's index; out of range the first.
    AllTagsEdition(std::vector<AllTagsSetting> tags, int selection);

    const std::vector<AllTagsSetting> &tags() const { return m_history.items(); }
    // The definitions the tool and the file take: those with a tag.
    std::vector<AllTagsSetting> usable() const;
    int selection() const { return m_history.selection(); }
    // The selected definition (an empty one when there are none).
    const AllTagsSetting &current() const;
    // Another definition chosen: no step.
    void select(int index);

    Check setName(std::u16string_view name, std::u16string *taken = nullptr);
    // The tag, the blanks around it and its leading backslashes dropped.
    Check setTag(std::u16string_view tag);
    // A number field's text (a comma reads as the decimal point). A range
    // refused sets `limit` to the value its reason names: the other end, or
    // half the range as typed.
    Check setNumber(Number field, std::u16string_view text, float *limit = nullptr);
    // How many values the tag takes, 1 to 4; the values past it are kept.
    void setCount(int count);
    // The change option the tag brings to the strip (AllTagsPasteMode).
    void setChangeOption(int option);
    // Where Set puts the tag (AllTagsSetting::mode, 0 to 2).
    void setPlacing(int placing);
    // A field left: its next change is a step of its own.
    void endTyping() { m_history.endMerge(); }

    // New tag: "New tag" ("New tag 2", ...), no tag yet, range 0 to 100,
    // step 1 (AllTagsSetting::named's), after the last, selected.
    void addTag();
    // Duplicate: the selected definition as "<name> copy" ("<name> copy 2"),
    // after it, selected.
    void duplicate();
    // Delete: the selected definition goes; never the last one, nor the
    // last one with a tag while new ones wait for theirs (false): the tool
    // and the file always have one.
    bool remove();
    bool canRemove() const;
    // The answer Replace to a taken name: the definition that has it goes
    // and the selected one takes `name`, one step. Never the last one with
    // a tag for one without its tag yet (setName's TakenLastUsable).
    void replaceNamed(std::u16string_view name);
    // Restore default tags, after its question: legacy's 22, the selected
    // name's place kept when it is there (else the first), one step.
    void restoreDefaults();
    // Undo all changes: the definitions the window opened with, one step.
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
    bool replacesLastUsable(std::size_t other) const;
    void edit(AllTagsSetting tag, Step step, std::string mergeKey = {}, Renames renamed = {});
    std::u16string freeName(std::u16string base, bool copy) const;
    EditHistory<AllTagsSetting> m_history;
};

// The toolbar's "Tag change options" (AllTagsItem::ShowContols,
// VideoToolbar.cpp:756-768): their names and its help text.
const std::array<std::u16string_view, 8> &allTagsChangeOptions();
std::u16string_view allTagsChangeOptionsHelp();

} // namespace hikari::application::visual
