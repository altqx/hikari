#pragma once

// T6: the all-tags tool's tag definitions (legacy AllTagsSetting,
// LoadSettings, GetNames, SaveSettings and the AllTagsEdition dialog,
// HikariSub/VisualAllTagsEdition.h and .cpp at 20d647c4; docs/qt/
// visual-tools.md, "The Position shifter and the all-tags tool"). The
// definitions live in Config/AllTagsSettings.txt beside the settings: a
// "HYDRA2.0" line, then one "Tag: <name>, <tag>, <min>, <max>, <value>,
// <step>, <decimal places>, <placing>, <change option>[, <more values>]"
// line each; without the file legacy's 22 defaults are used.

#include <array>
#include <optional>
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

// LoadSettings' defaults (VisualAllTagsEdition.cpp:467-489), as the text it
// parses, and the 22 definitions it gives.
std::u16string_view defaultAllTagsText();
std::vector<AllTagsSetting> defaultAllTags();

// LoadSettings (VisualAllTagsEdition.cpp:457-547) on the file's text (empty:
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
// SaveSettings (VisualAllTagsEdition.cpp:557-575): "HYDRA2.0\n" before the
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

// The "Tag editing" dialog (AllTagsEdition, VisualAllTagsEdition.cpp:
// 155-455). It edits a copy of the definitions; Apply keeps an edit in the
// copy, OK keeps it and gives the copy back to be saved, Cancel drops it.
// The fields are what the dialog shows; `current` is legacy's currentTag,
// what they were last set from or saved to.
class AllTagsEdition {
public:
    // A legacy message box: its text and title. Error boxes have OK only.
    struct Message {
        std::u16string text;
        std::u16string title;
    };

    // curTag out of range starts on the first definition.
    AllTagsEdition(std::vector<AllTagsSetting> tags, int curTag);

    const std::vector<AllTagsSetting> &tags() const { return m_tags; }
    // The list as legacy's HikariChoice holds it: names are added and
    // removed with the definitions, and a save puts a rename there
    // (T6-dialog-list-stale: legacy never refreshed it).
    const std::vector<std::u16string> &list() const { return m_list; }
    int selection() const { return m_selection; }
    const AllTagsSetting &current() const { return m_current; }

    // The fields.
    std::u16string newTagName;
    std::u16string name, tag;
    NumberField minValue{0, -10000.0, 10000.0, false};
    NumberField maxValue{0, -10000.0, 10000.0, false};
    NumberField step{0, -10000.0, 10000.0, false};
    int placing = 0; // the "mode" choice: kPlacings
    NumberField digitsAfterDot{0, 0, 6, true};
    std::array<NumberField, 4> values{NumberField{0, -10000.0, 10000.0, false},
                                      NumberField{0, -10000.0, 10000.0, false},
                                      NumberField{0, -10000.0, 10000.0, false},
                                      NumberField{0, -10000.0, 10000.0, false}};
    int additionalValues = 0; // the count choice: values - 1
    int changeOption = 0;     // the insert mode choice (AllTagsPasteMode)
    // values[i] (i 1-3) take input only while additionalValues + 1 > i.
    bool valueEnabled(int i) const { return i == 0 || additionalValues + 1 > i; }

    // OnAddTag: a new definition named `newTagName`, selected. Refused with a
    // message when the name is empty or the list holds it (ignoring case).
    std::optional<Message> addTag();
    // OnRemoveTag: the selected definition goes; refused for the last one.
    std::optional<Message> removeTag();
    // CheckModified: a field differs from `current`.
    bool modified() const;
    // OnListChanged's question when modified(): "Save changes to tag
    // \"%s\"?" (legacy names the tag, not its name).
    Message saveChangesQuestion() const;
    // OnListChanged after its question: the list's `index` (out of range:
    // the first) becomes the selection and the fields show it (SetTag).
    void select(int index);
    // Save (VisualAllTagsEdition.cpp:422-455) for Apply and OK: the fields
    // become `current` and, unless a check refuses them, the selected
    // definition. An empty name becomes the tag.
    std::optional<Message> save();
    // OnResetDefault after its question and the file's removal: the
    // definitions are the defaults, the list keeps the selected name's
    // place if it is there (else the first, HikariChoice::PutArray) and the
    // fields show it.
    void restoreDefaults();
    static Message restoreQuestion();

    // The dialog's choices (VisualAllTagsEdition.cpp:204-221).
    static const std::array<std::u16string_view, 3> &placings();
    static const std::array<std::u16string_view, 4> &valueCounts();

private:
    void updateTag();    // UpdateTag
    void showCurrent();  // SetTagFromSettings
    void setTag(int num); // SetTag
    std::vector<AllTagsSetting> m_tags;
    std::vector<std::u16string> m_list;
    AllTagsSetting m_current;
    int m_selection = 0;
};

// The toolbar's "Tag change options" (AllTagsItem::ShowContols,
// VideoToolbar.cpp:756-768): their names and its help text.
const std::array<std::u16string_view, 8> &allTagsChangeOptions();
std::u16string_view allTagsChangeOptionsHelp();

} // namespace hikari::application::visual
