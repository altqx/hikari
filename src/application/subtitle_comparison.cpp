#include "hikari/application/subtitle_comparison.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace hikari::application {

namespace {

// SubsTime::operator!= compares mstime (SubsTime.cpp:209-212).
std::int64_t milliseconds(const core::TimeField &time)
{
    return time.value.microseconds() / 1000;
}

bool contains(const std::vector<std::u8string> &list, const std::u8string &name)
{
    return std::ranges::find(list, name) != list.end();
}

// Whether a UTF-16 unit at `i` begins a surrogate pair.
bool pairAt(std::u16string_view s, std::size_t i)
{
    return s[i] >= 0xD800 && s[i] < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000;
}

// The text as wxGTK's wxString holds it: a code point per element (a lone
// surrogate, which valid UTF-8 never gives, stays one element).
std::u32string codePoints(std::u16string_view s)
{
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (pairAt(s, i)) {
            out += static_cast<char32_t>(0x10000 + ((s[i] - 0xD800) << 10) + (s[i + 1] - 0xDC00));
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// SubsGrid::CompareTexts over the texts' wxString elements (`Char` is the
// platform's wchar_t width).
template <typename Char>
void compareElements(LineComparison &firstCompare, LineComparison &secondCompare, std::basic_string_view<Char> first,
                     std::basic_string_view<Char> second);

} // namespace

std::u16string comparedText(const core::LineRecord &line, bool translationMode)
{
    // (CCG1->hasTLMode && dial1->TextTl != emptyString) ? dial1->TextTl : dial1->Text
    return core::toUtf16(translationMode && !line.translation.empty() ? line.translation : line.text);
}

ComparisonResult compareSubtitles(const ComparedDocument &first, const ComparedDocument &second, int compareBy,
                                  const std::vector<std::u8string> &chosenStyles, TextUnits units)
{
    // SubsGridBase.cpp:1739-1744: the chosen styles are read from the list
    // itself, not from the ChosenStyles bit.
    const bool byVisible = (compareBy & compare_by::Visible) != 0;
    const bool byTimes = (compareBy & compare_by::Times) != 0;
    const bool byStyles = (compareBy & compare_by::Styles) != 0;
    const bool byChosenStyles = !chosenStyles.empty();
    const bool bySelections = (compareBy & compare_by::Selections) != 0;

    const auto lines1 = first.document->lines();
    const auto lines2 = second.document->lines();
    ComparisonResult result;
    result.first.resize(lines1.size());
    result.second.resize(lines2.size());

    // Hidden (NOT_VISIBLE, SubsDialogue.h:45) is the only invisible state;
    // a revealed block (VISIBLE_BLOCK) counts as visible.
    auto hidden = [](const core::LineRecord *line) { return line->visibility == core::LineVisibility::Hidden; };

    // SubsGridBase.cpp:1757-1790: each Line of the first is paired with the
    // first Line of the second, from just after the last pair, that passes
    // every criterion; a Line with no partner leaves the search where it was.
    std::size_t lastJ = 0;
    for (std::size_t i = 0; i < lines1.size(); ++i) {
        const core::LineRecord *dial1 = lines1[i];
        if (byVisible && hidden(dial1))
            continue;
        for (std::size_t j = lastJ; j < lines2.size(); ++j) {
            const core::LineRecord *dial2 = lines2[j];
            if (byVisible && hidden(dial2))
                continue;
            if (byTimes && (milliseconds(dial1->start) != milliseconds(dial2->start) ||
                            milliseconds(dial1->end) != milliseconds(dial2->end)))
                continue;
            if (byStyles && dial1->style != dial2->style)
                continue;
            if (byChosenStyles && (!contains(chosenStyles, dial1->style) || dial1->style != dial2->style))
                continue;
            if (bySelections && (!first.selected.contains(dial1->id) || !second.selected.contains(dial2->id)))
                continue;
            const std::u16string text1 = comparedText(*dial1, first.translationMode);
            const std::u16string text2 = comparedText(*dial2, second.translationMode);
            if (units == TextUnits::CodePoints)
                compareTexts(result.first[i], result.second[j], codePoints(text1), codePoints(text2));
            else
                compareTexts(result.first[i], result.second[j], text1, text2);
            result.first[i].matchedRow = j;
            result.second[j].matchedRow = i;
            lastJ = j + 1;
            break;
        }
    }
    return result;
}

void compareTexts(LineComparison &firstCompare, LineComparison &secondCompare, std::u16string_view first,
                  std::u16string_view second)
{
    compareElements(firstCompare, secondCompare, first, second);
}

void compareTexts(LineComparison &firstCompare, LineComparison &secondCompare, std::u32string_view first,
                  std::u32string_view second)
{
    compareElements(firstCompare, secondCompare, first, second);
}

std::optional<MarkedRun> markedRun(std::u16string_view shown, int start, int end, TextUnits units)
{
    if (start < 0 || end < start)
        return std::nullopt;
    if (units == TextUnits::Utf16) {
        const auto from = static_cast<std::size_t>(start);
        if (from >= shown.size())
            return std::nullopt;
        return MarkedRun{from, std::min(static_cast<std::size_t>(end) + 1, shown.size()) - from};
    }
    // Code point `start` and the one after `end`, as UTF-16 offsets.
    std::size_t from = shown.size(), to = shown.size();
    const long long after = static_cast<long long>(end) + 1;
    long long index = 0;
    for (std::size_t i = 0; i < shown.size(); ++i, ++index) {
        if (index == start)
            from = i;
        if (index == after) {
            to = i;
            break;
        }
        if (pairAt(shown, i))
            ++i;
    }
    if (from >= shown.size())
        return std::nullopt;
    return MarkedRun{from, to - from};
}

namespace {

template <typename Char>
void compareElements(LineComparison &firstCompare, LineComparison &secondCompare, std::basic_string_view<Char> first,
                     std::basic_string_view<Char> second)
{
    if (first == second) {
        firstCompare.differences = false;
        secondCompare.differences = false;
        return;
    }
    firstCompare.marks.push_back(1);
    secondCompare.marks.push_back(1);

    // SubsGridBase.cpp:1807-1826: a table too large to allocate leaves just
    // the leading 1 (legacy logs "text comparison is too large" or "memory
    // allocation failed").
    const std::size_t l1 = first.size(), l2 = second.size();
    if ((l1 + 1) > std::numeric_limits<std::size_t>::max() / (l2 + 1))
        return;
    const std::size_t w = l2 + 1;
    std::vector<std::size_t> dpt;
    try {
        dpt.assign((l1 + 1) * w, 0);
    } catch (const std::bad_alloc &) {
        return;
    } catch (const std::length_error &) {
        return;
    }

    // SubsGridBase.cpp:1828-1845: the LCS table over the reversed texts.
    for (std::size_t i1 = 1; i1 <= l1; ++i1)
        for (std::size_t i2 = 1; i2 <= l2; ++i2) {
            if (first[l1 - i1] == second[l2 - i2])
                dpt[w * i1 + i2] = dpt[w * (i1 - 1) + (i2 - 1)] + 1;
            else if (dpt[w * (i1 - 1) + i2] > dpt[w * i1 + (i2 - 1)])
                dpt[w * i1 + i2] = dpt[w * (i1 - 1) + i2];
            else
                dpt[w * i1 + i2] = dpt[w * i1 + (i2 - 1)];
        }

    // SubsGridBase.cpp:1847-1883: walking from the start of both texts, a run
    // of characters not in the subsequence becomes one [start, end] range.
    int sfirst = -1, ssecond = -1;
    std::size_t i1 = l1, i2 = l2;
    for (;;) {
        if (i1 > 0 && i2 > 0 && first[l1 - i1] == second[l2 - i2]) {
            if (sfirst >= 0) {
                firstCompare.marks.push_back(sfirst);
                firstCompare.marks.push_back(static_cast<int>(l1 - i1) - 1);
                sfirst = -1;
            }
            if (ssecond >= 0) {
                secondCompare.marks.push_back(ssecond);
                secondCompare.marks.push_back(static_cast<int>(l2 - i2) - 1);
                ssecond = -1;
            }
            --i1;
            --i2;
            continue;
        }
        if (i1 > 0 && (i2 == 0 || dpt[w * (i1 - 1) + i2] >= dpt[w * i1 + (i2 - 1)])) {
            if (sfirst == -1)
                sfirst = static_cast<int>(l1 - i1);
            --i1;
            continue;
        }
        if (i2 > 0 && (i1 == 0 || dpt[w * (i1 - 1) + i2] < dpt[w * i1 + (i2 - 1)])) {
            if (ssecond == -1)
                ssecond = static_cast<int>(l2 - i2);
            --i2;
            continue;
        }
        break;
    }
    if (sfirst >= 0) {
        firstCompare.marks.push_back(sfirst);
        firstCompare.marks.push_back(static_cast<int>(l1 - i1) - 1);
    }
    if (ssecond >= 0) {
        secondCompare.marks.push_back(ssecond);
        secondCompare.marks.push_back(static_cast<int>(l2 - i2) - 1);
    }
}

} // namespace

std::vector<std::u8string> commonStyles(const core::Document &first, const core::Document &second)
{
    std::vector<std::u8string> out;
    const auto styles2 = core::decodeStyles(second);
    for (const auto &style1 : core::decodeStyles(first))
        if (std::ranges::any_of(styles2, [&](const auto &style2) { return style2.name == style1.name; }))
            out.push_back(style1.name);
    return out;
}

std::vector<SubtitleComparison::MenuStyle> SubtitleComparison::openMenu(const std::vector<std::u8string> &common,
                                                                        const std::vector<std::u8string> &optionStyles)
{
    // Notebook.cpp:922-928.
    std::vector<MenuStyle> items;
    for (const auto &name : common) {
        const bool checked = contains(optionStyles, name);
        items.push_back({name, checked});
        if (checked)
            m_chosenStyles.push_back(name);
    }
    return items;
}

int SubtitleComparison::toggleStyle(int compareBy, const std::u8string &name, bool checked)
{
    // Notebook.cpp:97-123.
    if (m_chosenStyles.empty() && (compareBy & compare_by::ChosenStyles))
        compareBy ^= compare_by::ChosenStyles;
    // Only the first occurrence is removed (the loop breaks at it).
    const auto it = std::ranges::find(m_chosenStyles, name);
    if (it != m_chosenStyles.end()) {
        if (!checked)
            m_chosenStyles.erase(it);
    } else if (checked) {
        m_chosenStyles.push_back(name);
    }
    if ((!m_chosenStyles.empty() && !(compareBy & compare_by::ChosenStyles)) ||
        (m_chosenStyles.empty() && (compareBy & compare_by::ChosenStyles)))
        compareBy ^= compare_by::ChosenStyles;
    return compareBy;
}

void SubtitleComparison::compare(DocumentId first, DocumentId second, const ComparedDocument &a,
                                 const ComparedDocument &b, int compareBy)
{
    // Notebook.cpp:948-951: CG1, CG2, SubsComparison, hasCompare. Legacy
    // left the previous pair's tables on their grids, where no later
    // comparison updated them (R1-stale-table): only the new pair has one.
    m_tables.clear();
    m_first = first;
    m_second = second;
    recompare(a, b, compareBy);
    m_active = true;
}

void SubtitleComparison::recompare(const ComparedDocument &a, const ComparedDocument &b, int compareBy)
{
    if (!m_first || !m_second)
        return;
    auto result = compareSubtitles(a, b, compareBy, m_chosenStyles);
    // SubsGridBase.cpp:1750-1755: each table is cleared and sized anew.
    m_tables[m_first->value] = std::move(result.first);
    m_tables[m_second->value] = std::move(result.second);
}

void SubtitleComparison::remove()
{
    // SubsGridBase.cpp:1888-1902 removes CG1's and CG2's tables. Every table
    // goes (R1-stale-table): none outlives the comparison.
    m_tables.clear();
    m_first.reset();
    m_second.reset();
    m_active = false;
}

void SubtitleComparison::replaced(DocumentId old, DocumentId replacement)
{
    m_tables.erase(old.value);
    if (m_first == old)
        m_first = replacement;
    if (m_second == old)
        m_second = replacement;
}

void SubtitleComparison::forget(DocumentId id)
{
    m_tables.erase(id.value);
}

std::vector<DocumentId> SubtitleComparison::tabled() const
{
    std::vector<DocumentId> out;
    for (const auto &[id, rows] : m_tables)
        out.push_back(DocumentId{id});
    return out;
}

const std::vector<LineComparison> *SubtitleComparison::table(DocumentId id) const
{
    const auto it = m_tables.find(id.value);
    return it == m_tables.end() ? nullptr : &it->second;
}

} // namespace hikari::application
