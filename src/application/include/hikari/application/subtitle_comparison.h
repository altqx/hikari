#pragma once

// Each Line of the first Document pairs with the next Line of the second that
// passes every chosen criterion.

#include "hikari/application/write_coordinator.h"
#include "hikari/core/document.h"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Bits of compareBy.
namespace compare_by {
inline constexpr int Times = 1, Styles = 2, ChosenStyles = 4, Visible = 8, Selections = 16;
}

// One character as HikariSub legacy counted it: a UTF-16 unit on Windows (16-bit
// wchar_t), a code point elsewhere; they differ only outside the BMP.
enum class TextUnits { Utf16, CodePoints };
#ifdef _WIN32
inline constexpr TextUnits kLegacyTextUnits = TextUnits::Utf16;
#else
inline constexpr TextUnits kLegacyTextUnits = TextUnits::CodePoints;
#endif

// `marks`: empty when the Line was not compared or its text is equal; otherwise a
// leading 1, then inclusive [start, end] pairs of the differing characters, in
// the comparison's TextUnits.
struct LineComparison {
    std::optional<std::size_t> matchedRow; // the other Document's row
    bool differences = true;               // false only for a matched Line with equal text
    std::vector<int> marks;

    bool mismatch() const { return !marks.empty(); }
    bool match() const { return !differences; }
    bool operator==(const LineComparison &) const = default;
};

struct ComparedDocument {
    const core::Document *document = nullptr;
    std::set<core::LineId> selected;
    // Script Info "TLMode: Yes"
    bool translationMode = false;
};

struct ComparisonResult {
    std::vector<LineComparison> first, second; // by Document row
};

// With `chosenStyles` not empty, both Lines must have the same style and it must
// be one of them (the ChosenStyles bit is not read).
ComparisonResult compareSubtitles(const ComparedDocument &first, const ComparedDocument &second, int compareBy,
                                  const std::vector<std::u8string> &chosenStyles,
                                  TextUnits units = kLegacyTextUnits);

// Equal texts clear `differences`; otherwise both get the leading 1 and the
// ranges outside a longest common subsequence, matched from the ends.
void compareTexts(LineComparison &first, LineComparison &second, std::u16string_view a, std::u16string_view b);
void compareTexts(LineComparison &first, LineComparison &second, std::u32string_view a, std::u32string_view b);

// One [start, end] range of `marks` as UTF-16 offsets into the shown text;
// nullopt when the run is empty (start past the text).
struct MarkedRun {
    std::size_t start = 0, length = 0;
    bool operator==(const MarkedRun &) const = default;
};
std::optional<MarkedRun> markedRun(std::u16string_view shown, int start, int end, TextUnits units = kLegacyTextUnits);

// The translation in translation mode when there is one, else the text.
std::u16string comparedText(const core::LineRecord &line, bool translationMode);

// The first Document's Style names, in its order, that the second also has.
std::vector<std::u8string> commonStyles(const core::Document &first, const core::Document &second);

// Application state; never changes a Document.
class SubtitleComparison {
public:
    // `common`: the Styles the two share. Each one in `optionStyles` is shown
    // checked and added to chosenStyles unless there; the list is never cleared.
    struct MenuStyle {
        std::u8string name;
        bool checked = false;
    };
    std::vector<MenuStyle> openMenu(const std::vector<std::u8string> &common,
                                    const std::vector<std::u8string> &optionStyles);
    bool chosenStylesShown() const { return !m_chosenStyles.empty(); }
    const std::vector<std::u8string> &chosenStyles() const { return m_chosenStyles; }

    // Returns the new comparison type; the new chosen styles are chosenStyles().
    int toggleStyle(int compareBy, const std::u8string &name, bool checked);
    static int toggleBit(int compareBy, int bit) { return compareBy ^ bit; }

    // Only the compared pair has tables.
    void compare(DocumentId first, DocumentId second, const ComparedDocument &a, const ComparedDocument &b,
                 int compareBy);
    // Runs after an edit, Undo or a filter on a grid that has a table.
    void recompare(const ComparedDocument &a, const ComparedDocument &b, int compareBy);
    // Every table goes.
    void remove();
    // The Document's tab got other subtitles: its table goes, and first() or
    // second() names the replacement.
    void replaced(DocumentId old, DocumentId replacement);
    // A Document that is gone: its table goes with it.
    void forget(DocumentId id);

    bool active() const { return m_active; }
    std::optional<DocumentId> first() const { return m_first; }
    std::optional<DocumentId> second() const { return m_second; }
    std::vector<DocumentId> tabled() const;
    // A grid's Comparison table, or nullptr when it has none.
    const std::vector<LineComparison> *table(DocumentId id) const;

private:
    std::vector<std::u8string> m_chosenStyles;
    std::map<std::uint64_t, std::vector<LineComparison>> m_tables;
    std::optional<DocumentId> m_first, m_second;
    bool m_active = false;
};

} // namespace hikari::application
