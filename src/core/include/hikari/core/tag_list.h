#pragma once

// Text and positions are UTF-16 code units.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core::taglist {

enum Type : int { TypeNormal = 0, TypeUsedInVisual = 1, TypeVsfilterMod = 2 };
enum Option : int { ShowAllTags = 1, ShowVsfilterModTags = 2, ShowDescription = 4 };

inline constexpr int kMaxVisible = 10;

struct Entry {
    std::u16string_view tag;
    int type = TypeNormal;
    bool needBrackets = false; // insertedTag adds "()"
};

std::span<const Entry> entries();
inline constexpr std::size_t kEntryCount = 77;

// Type 0 always, otherwise when an option bit it has is on.
bool shownByOptions(const Entry &entry, int options);

// The popup exists only with a shown entry.
class TagList {
public:
    explicit TagList(int options);

    int options() const { return m_options; }
    bool showDescription() const { return (m_options & ShowDescription) != 0; }
    const std::u16string &keyword() const { return m_keyword; }
    bool entryShown(std::size_t entry) const { return m_shown[entry]; }
    std::size_t count() const;
    std::vector<std::size_t> shownEntries() const;
    // The entry at a shown row, or none.
    std::optional<std::size_t> entryAt(int row) const;

    bool popupShown() const { return m_popup; }
    int selection() const { return m_popup ? m_selection : -1; }
    int scrollPosition() const { return m_scroll; }
    // How many times the popup was made (its width depends on it).
    int popupCalls() const { return m_popupCalls; }

    // Narrows the shown entries.
    void appendToKeyword(char16_t ch);
    // Hides shown entries not starting with `keyword` (never shows one
    // again); with fewer entries the popup is made again with the first row
    // selected.
    void filterByKeyword(std::u16string_view keyword, bool setKeyword = true);
    // The options alone decide again (the keyword's narrowing is dropped),
    // and the popup is made again.
    void filterByOptions(int options);
    // Made again, with `selected` selected, when an entry is shown.
    void popup(int selected);
    // Wraps around and keeps the row in view.
    void setSelection(int row);
    // Three rows per wheel notch.
    void scrollBy(int rows);
    // Ignored as the first mouse event after the popup is made, as every event is.
    bool wheel(int rows);
    // `row` counted from the popup's top: the selection follows it inside the
    // shown rows. The first mouse event after the popup is made is ignored.
    // Returns whether the event acts: a click then chooses (left) or opens
    // the menu (right).
    bool pointerAt(int row);

private:
    void paintClamp();
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

// The editor's text after choosing an entry.
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

    // After `key` was inserted: `caret` is the position after it. True when
    // the list opened or changed.
    bool typed(std::u16string_view text, std::size_t caret, char16_t key, int options);
    // The selection moves and wraps. False when the list is closed (the key
    // is the field's).
    bool move(int delta);
    // The selected entry replaces the text from the last backslash before the
    // caret to the caret, and the list closes. Nothing without a selected
    // entry or a backslash (the list stays open).
    std::optional<Put> put(std::u16string_view text, std::size_t caret);
    void close() { m_list.reset(); }

private:
    std::optional<TagList> m_list;
};

// With "()" for tags taking arguments.
std::u16string insertedTag(const Entry &entry);

}
