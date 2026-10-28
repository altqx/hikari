#pragma once

// E6: the Line editor's tag list (legacy TextEditorTagList.h/.cpp and the
// tag-list parts of DialogueTextEditor.cpp at 20d647c4): the 77 completion
// entries, their filtering by TEXT_EDITOR_TAG_LIST_OPTIONS and by the typed
// keyword, the popup's selection and scroll position, and the editor's
// rules for opening, narrowing, choosing and closing it. Text and positions
// are UTF-16 code units, as legacy's wxString on Windows. The descriptions
// are the UI's (translated there, by entry index).

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core::taglist {

// TextEditorTagList.h:24-33. An entry's type, and the option bits
// (TEXT_EDITOR_TAG_LIST_OPTIONS): bit 1 "Show all tags" (the tags of the
// visual tools' type), bit 2 "Show VSFiltermod tags", bit 4 "Show description".
enum Type : int { TypeNormal = 0, TypeUsedInVisual = 1, TypeVsfilterMod = 2 };
enum Option : int { ShowAllTags = 1, ShowVsfilterModTags = 2, ShowDescription = 4 };

// The popup shows at most ten rows (TextEditorTagList.cpp:26).
inline constexpr int kMaxVisible = 10;

struct Entry {
    std::u16string_view tag;
    int type = TypeNormal;
    bool needBrackets = false; // GetTag adds "()"
};

// PopupTagList::InitList (TextEditorTagList.cpp:338-417), in its order.
std::span<const Entry> entries();
inline constexpr std::size_t kEntryCount = 77;

// TagListItem::ShowItem(int): an entry of type 0 always, otherwise when an
// option bit it has is on.
bool shownByOptions(const Entry &entry, int options);

// PopupTagList with its PopupWindow: which entries are shown, the keyword,
// whether the popup exists (legacy creates it only with a shown entry) and
// its selection and first row.
class TagList {
public:
    explicit TagList(int options); // InitList(option)

    int options() const { return m_options; }
    bool showDescription() const { return (m_options & ShowDescription) != 0; }
    const std::u16string &keyword() const { return m_keyword; }
    bool entryShown(std::size_t entry) const { return m_shown[entry]; }
    std::size_t count() const; // GetCount
    // The shown entries, in order (FindItemById over 0..count-1).
    std::vector<std::size_t> shownEntries() const;
    // The entry at a shown row (GetItem / FindItemById), or none.
    std::optional<std::size_t> entryAt(int row) const;

    bool popupShown() const { return m_popup; }
    int selection() const { return m_popup ? m_selection : -1; } // GetSelection
    int scrollPosition() const { return m_scroll; }
    // How many times Popup ran for this list (its width depends on it).
    int popupCalls() const { return m_popupCalls; }

    // AppendToKeyword: the keyword grows and narrows the shown entries.
    void appendToKeyword(char16_t ch);
    // FilterListViaKeyword: shown entries that do not start with `keyword`
    // are hidden (it never shows one again). With fewer entries than when
    // the popup was made it is made again with the first row selected.
    void filterByKeyword(std::u16string_view keyword, bool setKeyword = true);
    // FilterListViaOptions: the options alone decide again (the keyword's
    // narrowing is dropped), and the popup is made again.
    void filterByOptions(int options);
    // Popup: made again, with `selected` selected, when an entry is shown.
    void popup(int selected);
    // PopupTagList::SetSelection: wraps around and keeps the row in view.
    void setSelection(int row);
    // The scroll bar (OnScroll) or the mouse wheel's rows (three per notch).
    void scrollBy(int rows);
    // The mouse wheel (PopupWindow::OnMouseEvent): ignored as the first
    // mouse event after the popup is made, as every event is.
    bool wheel(int rows);
    // The row under the pointer (`row` counted from the popup's top): the
    // selection follows it inside the shown rows. The first mouse event
    // after the popup is made is ignored (blockMouseEvent). Returns whether
    // the event acts: a click then chooses (left) or opens the menu (right).
    bool pointerAt(int row);

private:
    void paintClamp(); // OnPaint's scroll clamp
    int m_options = 0;
    std::vector<bool> m_shown;
    std::u16string m_keyword;
    std::size_t m_lastItems = 0;
    bool m_popup = false;
    int m_selection = 0;
    int m_scroll = 0;
    bool m_blockMouse = true;
    int m_popupCalls = 0;
};

// The editor's text after choosing an entry (PutTag).
struct Put {
    std::u16string text;
    std::size_t caret = 0;
};

// TextEditor's tag list: opened by typing in an override block, narrowed by
// what is typed after it, chosen with Enter or a click, closed by other keys.
class Completion {
public:
    bool open() const { return m_list.has_value(); }
    const TagList *list() const { return m_list ? &*m_list : nullptr; }
    TagList *list() { return m_list ? &*m_list : nullptr; }

    // OnCharPress after the character `key` was inserted: `text` is the
    // field's text and `caret` the position after the character.
    // Returns true when the list opened or changed.
    bool typed(std::u16string_view text, std::size_t caret, char16_t key, int options);
    // Up / Down (ID_UP / ID_DOWN) while the list is open: the selection moves
    // and wraps. False when the list is closed (the key is the field's).
    bool move(int delta);
    // Enter (EDITBOX_COMMIT_GO_NEXT_LINE) or a click: the selected entry
    // replaces the text from the last backslash before the caret to the
    // caret, and the list closes. Nothing without a selected entry or a
    // backslash (the list stays open).
    std::optional<Put> put(std::u16string_view text, std::size_t caret);
    void close() { m_list.reset(); }

private:
    std::optional<TagList> m_list;
};

// GetTag: the tag, with "()" for those taking arguments.
std::u16string insertedTag(const Entry &entry);

} // namespace hikari::core::taglist
