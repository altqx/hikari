// E6: legacy TextEditorTagList.cpp and DialogueTextEditor.cpp's tag list at
// 20d647c4 (see tag_list.h).

#include "hikari/core/tag_list.h"

#include <algorithm>
#include <array>

namespace hikari::core::taglist {

namespace {

// PopupTagList::InitList (TextEditorTagList.cpp:340-416).
constexpr std::array<Entry, kEntryCount> kEntries{{
    {u"1a", TypeUsedInVisual, false},
    {u"2a", TypeUsedInVisual, false},
    {u"3a", TypeUsedInVisual, false},
    {u"4a", TypeUsedInVisual, false},
    {u"1c", TypeUsedInVisual, false},
    {u"2c", TypeUsedInVisual, false},
    {u"3c", TypeUsedInVisual, false},
    {u"4c", TypeUsedInVisual, false},
    {u"1img", TypeVsfilterMod, true},
    {u"2img", TypeVsfilterMod, true},
    {u"3img", TypeVsfilterMod, true},
    {u"4img", TypeVsfilterMod, true},
    {u"1va", TypeVsfilterMod, true},
    {u"2va", TypeVsfilterMod, true},
    {u"3va", TypeVsfilterMod, true},
    {u"4va", TypeVsfilterMod, true},
    {u"1vc", TypeVsfilterMod, true},
    {u"2vc", TypeVsfilterMod, true},
    {u"3vc", TypeVsfilterMod, true},
    {u"4vc", TypeVsfilterMod, true},
    {u"a", TypeUsedInVisual, false},
    {u"alpha", TypeNormal, false},
    {u"an", TypeUsedInVisual, false},
    {u"b", TypeUsedInVisual, false},
    {u"be", TypeNormal, false},
    {u"blur", TypeNormal, false},
    {u"bord", TypeNormal, false},
    {u"clip", TypeUsedInVisual, true},
    {u"distort", TypeVsfilterMod, true},
    {u"fad", TypeNormal, true},
    {u"fade", TypeNormal, true},
    {u"fax", TypeNormal, false},
    {u"fay", TypeNormal, false},
    {u"fe", TypeNormal, false},
    {u"fn", TypeUsedInVisual, false},
    {u"frs", TypeVsfilterMod, false},
    {u"frx", TypeUsedInVisual, false},
    {u"fry", TypeUsedInVisual, false},
    {u"frz", TypeUsedInVisual, false},
    {u"fs", TypeUsedInVisual, false},
    {u"fsc", TypeVsfilterMod, false},
    {u"fscx", TypeUsedInVisual, false},
    {u"fscy", TypeUsedInVisual, false},
    {u"fsp", TypeNormal, false},
    {u"fsvp", TypeVsfilterMod, false},
    {u"i", TypeUsedInVisual, false},
    {u"iclip", TypeUsedInVisual, true},
    {u"jitter", TypeVsfilterMod, true},
    {u"k", TypeUsedInVisual, false},
    {u"K", TypeUsedInVisual, false},
    {u"ko", TypeUsedInVisual, false},
    {u"kt", TypeUsedInVisual, false},
    {u"move", TypeUsedInVisual, true},
    {u"mover", TypeVsfilterMod, true},
    {u"moves3", TypeVsfilterMod, true},
    {u"moves4", TypeVsfilterMod, true},
    {u"movevc", TypeVsfilterMod, true},
    {u"org", TypeUsedInVisual, true},
    {u"p", TypeUsedInVisual, false},
    {u"pbo", TypeNormal, false},
    {u"pos", TypeUsedInVisual, true},
    {u"q", TypeNormal, false},
    {u"r", TypeNormal, false},
    {u"rnd", TypeVsfilterMod, false},
    {u"rnds", TypeVsfilterMod, false},
    {u"rndx", TypeVsfilterMod, false},
    {u"rndy", TypeVsfilterMod, false},
    {u"rndz", TypeVsfilterMod, false},
    {u"s", TypeUsedInVisual, false},
    {u"shad", TypeNormal, false},
    {u"t", TypeNormal, true},
    {u"u", TypeNormal, false},
    {u"xbord", TypeNormal, false},
    {u"ybord", TypeNormal, false},
    {u"xshad", TypeNormal, false},
    {u"yshad", TypeNormal, false},
    {u"z", TypeVsfilterMod, false},
}};

} // namespace

std::span<const Entry> entries()
{
    return kEntries;
}

bool shownByOptions(const Entry &entry, int options)
{
    // TagListItem::ShowItem(int) (TextEditorTagList.h:38-43).
    return entry.type == TypeNormal || ((options & TypeUsedInVisual) && (entry.type & TypeUsedInVisual)) ||
           ((options & TypeVsfilterMod) && (entry.type & TypeVsfilterMod));
}

std::u16string insertedTag(const Entry &entry)
{
    // TagListItem::GetTag (TextEditorTagList.h:53-59).
    std::u16string tag(entry.tag);
    if (entry.needBrackets)
        tag += u"()";
    return tag;
}

TagList::TagList(int options) : m_options(options), m_shown(kEntryCount)
{
    // The constructor reads TEXT_EDITOR_TAG_LIST_OPTIONS and InitList shows
    // each item by it (TextEditorTagList.cpp:246-252).
    for (std::size_t i = 0; i < kEntryCount; ++i)
        m_shown[i] = shownByOptions(kEntries[i], options);
}

std::size_t TagList::count() const
{
    return static_cast<std::size_t>(std::count(m_shown.begin(), m_shown.end(), true));
}

std::vector<std::size_t> TagList::shownEntries() const
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < kEntryCount; ++i)
        if (m_shown[i])
            out.push_back(i);
    return out;
}

std::optional<std::size_t> TagList::entryAt(int row) const
{
    // FindItemById (TextEditorTagList.cpp:295-316): the row-th shown item.
    if (row < 0)
        return std::nullopt;
    int seen = 0;
    for (std::size_t i = 0; i < kEntryCount; ++i)
        if (m_shown[i]) {
            if (seen == row)
                return i;
            ++seen;
        }
    return std::nullopt;
}

void TagList::appendToKeyword(char16_t ch)
{
    // AppendToKeyword (TextEditorTagList.cpp:440-445).
    m_keyword += ch;
    filterByKeyword(m_keyword, false);
}

void TagList::filterByKeyword(std::u16string_view keyword, bool setKeyword)
{
    // FilterListViaKeyword (TextEditorTagList.cpp:269-283); ShowItem(keyword)
    // only hides (TextEditorTagList.h:44-47), case-sensitively (StartsWith).
    if (setKeyword)
        m_keyword = keyword;
    for (std::size_t i = 0; i < kEntryCount; ++i)
        if (m_shown[i])
            m_shown[i] = kEntries[i].tag.starts_with(keyword);
    if (m_lastItems > count())
        popup(0);
    else if (m_popup)
        setSelection(count() ? 0 : -1);
}

void TagList::filterByOptions(int options)
{
    // FilterListViaOptions (TextEditorTagList.cpp:261-267).
    m_options = options;
    for (std::size_t i = 0; i < kEntryCount; ++i)
        m_shown[i] = shownByOptions(kEntries[i], options);
    popup(0);
}

void TagList::popup(int selected)
{
    // PopupTagList::Popup (TextEditorTagList.cpp:419-438): the old popup
    // goes; a new one only when its height (a row per shown entry, ten at
    // most, plus 2) exceeds 5, that is with an entry shown.
    ++m_popupCalls;
    m_popup = count() > 0;
    if (m_popup) {
        // A new PopupWindow: sel 0, scrollPositionV 0, blockMouseEvent.
        m_selection = 0;
        m_scroll = 0;
        m_blockMouse = true;
        setSelection(selected); // PopupWindow::Popup
    }
    m_lastItems = count();
}

void TagList::setSelection(int row)
{
    // PopupWindow::SetSelection (TextEditorTagList.cpp:229-244).
    if (!m_popup)
        return;
    const int shown = static_cast<int>(count());
    if (row < 0)
        row = shown - 1;
    else if (row >= shown)
        row = 0;
    m_selection = row;
    if (m_selection < m_scroll && m_selection != -1)
        m_scroll = m_selection;
    else if (m_selection >= m_scroll + kMaxVisible && m_selection - kMaxVisible + 1 >= 0)
        m_scroll = m_selection - kMaxVisible + 1;
    paintClamp();
}

void TagList::paintClamp()
{
    // OnPaint (TextEditorTagList.cpp:160-166), run by every Refresh.
    const int shown = static_cast<int>(count());
    if (m_scroll >= shown - kMaxVisible)
        m_scroll = shown - kMaxVisible;
    if (m_scroll < 0)
        m_scroll = 0;
}

void TagList::scrollBy(int rows)
{
    // OnMouseEvent's wheel (TextEditorTagList.cpp:109-116), then OnPaint.
    if (!m_popup)
        return;
    m_scroll += rows;
    const int shown = static_cast<int>(count());
    if (m_scroll < 0)
        m_scroll = 0;
    else if (m_scroll > shown - kMaxVisible)
        m_scroll = shown - kMaxVisible;
    paintClamp();
}

bool TagList::wheel(int rows)
{
    if (!m_popup)
        return false;
    if (m_blockMouse) {
        m_blockMouse = false;
        return false;
    }
    scrollBy(rows);
    return true;
}

bool TagList::pointerAt(int row)
{
    // OnMouseEvent (TextEditorTagList.cpp:98-129).
    if (!m_popup)
        return false;
    if (m_blockMouse) {
        m_blockMouse = false;
        return false;
    }
    const int element = row + m_scroll;
    if (row < 0 || element >= static_cast<int>(count()) || element < 0)
        return false;
    if (element != m_selection) {
        if (element >= m_scroll + kMaxVisible || element < m_scroll)
            return false;
        m_selection = element;
    }
    return true;
}

bool Completion::typed(std::u16string_view text, std::size_t caret, char16_t key, int options)
{
    // TextEditor::OnCharPress (DialogueTextEditor.cpp:426-492), after the
    // character went in and the caret moved past it.
    const auto at = [&](std::ptrdiff_t i) { return text[static_cast<std::size_t>(i)]; };
    const auto c = static_cast<std::ptrdiff_t>(std::min(caret, text.size()));
    const auto length = static_cast<std::ptrdiff_t>(text.size());
    if (!m_list) {
        if (!(key == u'\\' || (c - 2 >= 0 && at(c - 2) == u'\\') ||
              (c < length && (at(c) == u'\\' || at(c) == u'}'))))
            return false;
        // Back from the typed character to the block's "{": a "}" first
        // means the caret is outside a block; the characters after the last
        // backslash are the tag typed so far.
        std::u16string partTag;
        bool hasPartTag = false;
        for (std::ptrdiff_t i = c - 1; i >= 0; --i) {
            const char16_t ch = at(i);
            if (ch == u'}')
                break;
            if (ch == u'{') {
                m_list.emplace(options);
                if (key != u'\\') {
                    if (partTag.empty())
                        m_list->appendToKeyword(key);
                    else
                        m_list->filterByKeyword(partTag);
                }
                m_list->popup(0);
                return true;
            }
            if (!hasPartTag) {
                if (ch == u'\\') {
                    hasPartTag = true;
                    continue;
                }
                partTag.insert(partTag.begin(), ch);
            }
        }
        return false;
    }
    m_list->appendToKeyword(key);
    if (!m_list->count())
        m_list.reset();
    return true;
}

bool Completion::move(int delta)
{
    // OnAccelerator's ID_DOWN / ID_UP (DialogueTextEditor.cpp:722-750):
    // the key moves the selection, or does nothing without a popup.
    if (!m_list)
        return false;
    if (m_list->popupShown())
        m_list->setSelection(m_list->selection() + delta);
    return true;
}

std::optional<Put> Completion::put(std::u16string_view text, std::size_t caret)
{
    // TextEditor::PutTag (DialogueTextEditor.cpp:2829-2852).
    if (!m_list)
        return std::nullopt;
    const auto entry = m_list->entryAt(m_list->selection());
    if (!entry)
        return std::nullopt;
    caret = std::min(caret, text.size());
    for (std::size_t i = caret; i-- > 0;) {
        if (text[i] != u'\\')
            continue;
        const std::u16string tag = insertedTag(kEntries[*entry]);
        std::size_t position = i + 1 + tag.size();
        if (tag.ends_with(u')'))
            --position; // inside the brackets
        Put out{std::u16string(text), position};
        out.text.replace(i + 1, caret - (i + 1), tag);
        m_list.reset();
        return out;
    }
    return std::nullopt;
}

} // namespace hikari::core::taglist
