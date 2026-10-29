#pragma once

#include "hikari/application/edit_history.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

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

struct AllTagsSetting {
    std::u16string name;
    std::u16string tag; // without the backslash
    float rangeMin = 0.f;
    float rangeMax = 0.f;
    float step = 0.f;
    std::array<float, 4> values{0.f, 0.f, 0.f, 0.f};
    // Where Insert puts the tag: 0 at the cursor, 1 at the text's start, or 2.
    unsigned char mode = 0;
    unsigned char digitsAfterDot = 0;
    unsigned char numOfValues = 1;
    int tagMode = 0; // an AllTagsPasteMode

    // Name and tag `name`, step 1, range 0-100.
    static AllTagsSetting named(std::u16string name);
    bool operator==(const AllTagsSetting &) const = default;
};

std::u16string_view defaultAllTagsText();
std::vector<AllTagsSetting> defaultAllTags();

// `text` has its UTF-8 BOM dropped. A text not starting "HYDRA2.0" gives the
// defaults, and `writeDefaults` says they replace a non-empty text. Lines
// missing a field or holding an unreadable number are skipped.
std::vector<AllTagsSetting> parseAllTags(std::u16string_view text, bool *writeDefaults = nullptr);
// Numbers as "%f", as HikariSub legacy writes them; empty for no definitions.
std::u16string writeAllTags(const std::vector<AllTagsSetting> &tags);
std::vector<std::u16string> allTagsNames(const std::vector<AllTagsSetting> &tags);

// The value read back is clamped to the range, falling back to the last
// valid text typed.
class NumberField {
public:
    NumberField(double value, double rangeFrom, double rangeTo, bool intOnly);
    const std::u16string &text() const { return m_text; }
    void setText(std::u16string text);
    double getDouble() const;
    int getInt() const { return static_cast<int>(getDouble()); }
    void setDouble(double value);
    void setInt(int value);
    bool intOnly() const { return m_intOnly; }
    double rangeFrom() const { return m_from; }
    double rangeTo() const { return m_to; }

private:
    double m_from, m_to;
    bool m_intOnly;
    std::u16string m_text, m_oldval;
    mutable double m_value = 0;
};
// "%f" without trailing zeros (and point).
std::u16string numCtrlText(double value);

// Per definition, by name: 0 common defaults, 1 other defaults, 2 the user's own.
std::vector<int> allTagsSections(const std::vector<AllTagsSetting> &tags);
// One label per value the definition takes.
std::vector<std::u16string> allTagsValueNames(const AllTagsSetting &tag);
// The tag at its starting values; colours as ASS "&HBBGGRR&" from R G B.
std::u16string allTagsSetText(const AllTagsSetting &tag);

// Every change is a history step; a refused value keeps the last good one.
// Names compare ignoring case.
class AllTagsEdition {
public:
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
        TakenLastUsable // taken by the last definition with a tag while this one has none
    };
    enum class Number { Min, Max, Step, Digits, Value0, Value1, Value2, Value3 };
    static constexpr double kNumberLimit = 10000.0;
    static constexpr int kMaxDigits = 6;

    // An out-of-range `selection` selects the first.
    AllTagsEdition(std::vector<AllTagsSetting> tags, int selection);

    const std::vector<AllTagsSetting> &tags() const { return m_history.items(); }
    // Definitions with a tag.
    std::vector<AllTagsSetting> usable() const;
    int selection() const { return m_history.selection(); }
    // An empty definition when there are none.
    const AllTagsSetting &current() const;
    // Not a history step.
    void select(int index);

    Check setName(std::u16string_view name, std::u16string *taken = nullptr);
    // Drops surrounding blanks and leading backslashes.
    Check setTag(std::u16string_view tag);
    // A comma reads as the decimal point. A refused range sets `limit` to the
    // other end, or to half the range.
    Check setNumber(Number field, std::u16string_view text, float *limit = nullptr);
    // 1 to 4; values past it are kept.
    void setCount(int count);
    // An AllTagsPasteMode.
    void setChangeOption(int option);
    // 0 to 2 (AllTagsSetting::mode).
    void setPlacing(int placing);
    // The next change starts a new history step.
    void endTyping() { m_history.endMerge(); }

    // Appends and selects "New tag" (numbered when taken) with no tag yet.
    void addTag();
    // Inserts "<name> copy" after the selected one and selects it.
    void duplicate();
    // False for the last definition, or the last one with a tag.
    bool remove();
    bool canRemove() const;
    // Drops the definition named `name` and renames the selected one to it,
    // one step; refused when that drops the last definition with a tag.
    void replaceNamed(std::u16string_view name);
    // Keeps the selected name selected when the defaults have it, one step.
    void restoreDefaults();
    // Back to the definitions it opened with, one step.
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
    bool replacesLastUsable(std::size_t other) const;
    void edit(AllTagsSetting tag, Step step, std::string mergeKey = {}, Renames renamed = {});
    std::u16string freeName(std::u16string base, bool copy) const;
    EditHistory<AllTagsSetting> m_history;
};

const std::array<std::u16string_view, 8> &allTagsChangeOptions();
std::u16string_view allTagsChangeOptionsHelp();

} // namespace hikari::application::visual
