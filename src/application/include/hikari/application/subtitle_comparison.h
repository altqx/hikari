#pragma once

// R1: subtitle comparison (legacy SubsGrid::SubsComparison, CompareTexts and
// RemoveComparison in SubsGridBase.cpp, the Notebook tab menu in
// Notebook.cpp, at 20d647c4). Two Documents are walked in order: each Line
// of the first is paired with the next Line of the second that passes every
// chosen criterion, and the paired texts are compared character by character.
//
// The rewrite compares the editing target (legacy CG1, the active tab) with
// the protected reference (CG2, the tab the menu was opened on). What the
// Grid shows of a result is in its rows: a matched Line whose text is equal
// (GRID_COMPARISON_BACKGROUND_MATCH), a matched Line whose text differs
// (GRID_COMPARISON_BACKGROUND_NOT_MATCH, the differing characters outlined in
// GRID_COMPARISON_OUTLINE), and an unmatched Line (its usual colour).

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

// SUBS_COMPARISON_TYPE bits (legacy Notebook.h:140-144 COMPARE_BY_*).
namespace compare_by {
inline constexpr int Times = 1, Styles = 2, ChosenStyles = 4, Visible = 8, Selections = 16;
}

// One Line's result (legacy compareData, SubsGrid.h:67-78). `marks` is the
// legacy lineCompare array: empty when the Line was not compared or its text
// is equal; otherwise a leading 1, then inclusive [start, end] pairs of the
// differing characters in UTF-16 code units (wxString on Windows).
struct LineComparison {
    std::optional<std::size_t> matchedRow; // secondComparedLine: the other Document's row
    bool differences = true;               // false only for a matched Line with equal text
    std::vector<int> marks;

    // SubsGridWindow.cpp:419-420: the colours the Grid paints.
    bool mismatch() const { return !marks.empty(); }
    bool match() const { return !differences; }
    bool operator==(const LineComparison &) const = default;
};

// What SubsComparison reads of one side.
struct ComparedDocument {
    const core::Document *document = nullptr;
    std::set<core::LineId> selected; // file->IsSelected
    // The grid's hasTLMode: Script Info "TLMode: Yes" (SubsGridBase.cpp:995-996).
    bool translationMode = false;
};

struct ComparisonResult {
    std::vector<LineComparison> first, second; // by Document row
};

// SubsGrid::SubsComparison (SubsGridBase.cpp:1737-1793). `chosenStyles` is
// SubsGrid::compareStyles: when it is not empty, both Lines must have the
// same style and it must be one of them (the ChosenStyles bit is not read).
ComparisonResult compareSubtitles(const ComparedDocument &first, const ComparedDocument &second, int compareBy,
                                  const std::vector<std::u8string> &chosenStyles);

// SubsGrid::CompareTexts (SubsGridBase.cpp:1796-1884): equal texts clear
// `differences`; otherwise both get the leading 1 and the ranges outside a
// longest common subsequence, matched from the ends of the texts.
void compareTexts(LineComparison &first, LineComparison &second, std::u16string_view a, std::u16string_view b);

// The text a Line is compared by (SubsGridBase.cpp:1780-1781): the
// translation in translation mode when there is one, else the text.
std::u16string comparedText(const core::LineRecord &line, bool translationMode);

// SubsGrid::GetCommonStyles (SubsGridBase.cpp:1713-1724): the names of the
// first Document's Styles, in its order, that the second also has.
std::vector<std::u8string> commonStyles(const core::Document &first, const core::Document &second);

// Legacy's comparison state: the statics SubsGrid::CG1, CG2, hasCompare and
// compareStyles, and each grid's Comparison table. It is application state;
// it never changes a Document.
class SubtitleComparison {
public:
    // The tab menu's "Subtitle comparison" (Notebook::ContextMenu,
    // Notebook.cpp:915-939), opened on a tab other than the active one with
    // `common` the Styles the two share: each one in SUBS_COMPARISON_STYLES is
    // shown checked and added to compareStyles again (legacy never clears that
    // list, so a style stays chosen however often it was added).
    struct MenuStyle {
        std::u8string name;
        bool checked = false;
    };
    std::vector<MenuStyle> openMenu(const std::vector<std::u8string> &common,
                                    const std::vector<std::u8string> &optionStyles);
    // "Compare by selected styles" is shown checked (Notebook.cpp:936).
    bool chosenStylesShown() const { return !m_chosenStyles.empty(); }
    const std::vector<std::u8string> &chosenStyles() const { return m_chosenStyles; }

    // A style item toggled to `checked` (the ID_CHECK_EVENT handler, case
    // 4448, Notebook.cpp:97-123). Returns the new SUBS_COMPARISON_TYPE; the
    // new SUBS_COMPARISON_STYLES is chosenStyles().
    int toggleStyle(int compareBy, const std::u8string &name, bool checked);
    // MENU_COMPARE + 1..4 (Notebook.cpp:85-96): the bit flips.
    static int toggleBit(int compareBy, int bit) { return compareBy ^ bit; }

    // MENU_COMPARE (Notebook.cpp:947-951): CG1 and CG2 are set and compared.
    // The previous pair's tables stay (legacy does not remove them first).
    void compare(DocumentId first, DocumentId second, const ComparedDocument &a, const ComparedDocument &b,
                 int compareBy);
    // SubsComparison again for CG1 and CG2, as an edit, Undo or a filter on a
    // grid that has a table runs it (SetModified, DoUndo, RefreshSubsOnVideo).
    void recompare(const ComparedDocument &a, const ComparedDocument &b, int compareBy);
    // RemoveComparison (SubsGridBase.cpp:1886-1903): CG1's and CG2's tables go.
    void remove();
    // SubsGrid::Clearing (SubsGridBase.cpp:120): a Document's table goes when
    // its tab is cleared (other subtitles loaded into it); CG1/CG2 now name
    // the replacement, as legacy kept the grid.
    void replaced(DocumentId old, DocumentId replacement);
    // A Document that is gone: its table goes with it.
    void forget(DocumentId id);

    bool active() const { return m_active; }               // hasCompare
    std::optional<DocumentId> first() const { return m_first; }   // CG1
    std::optional<DocumentId> second() const { return m_second; } // CG2
    // The Documents that have a table.
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
