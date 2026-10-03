#pragma once

// Legacy ASS tag commands of the Line editor (V2-E-grammar, E28-grammar):
// TagFindReplace::FindTag and PutTagInText, as EditBox's Bold, Italic,
// Underline and Strikeout commands use them. The search is positional and
// keeps the legacy quirks (index arithmetic on signed/unsigned values, the
// "\t(...)" handling, "\r" stopping at the block before the caret). Text and
// positions are UTF-16 code units, as on Windows, where wxString is UTF-16.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::core::legacy {

// FindBrackets: the override block around position `from`, or -1s.
std::pair<long, long> findBrackets(std::u16string_view text, long from);

struct EditorText {
    std::u16string text;
    long selectionStart = 0;
    long selectionEnd = 0;
};

class TagEditor {
public:
    explicit TagEditor(EditorText state) : m_state(std::move(state)) {}

    // mode 0: the selection; 1: position 0; 3: keep the previous from/to.
    bool findTag(std::u16string_view pattern, int mode, bool toEndOfSelection);
    void putTagInText(std::u16string_view tag, std::u16string_view resetTag, bool restoreSelection = false);

    const std::u16string &finding() const { return m_finding; }
    const EditorText &state() const { return m_state; }

private:
    EditorText m_state;
    long m_from = 0, m_to = 0;
    std::u16string m_lastPattern;
    std::pair<long, long> m_lastSelection{0, 0};
    // FindData
    std::u16string m_finding;
    long m_posX = 0, m_posY = 0;
    long m_cursor = 0;
    bool m_inBracket = false;
    bool m_hasSelection = false;
};

// EditBox::OnBoldClick and its Italic/Underline/Strikeout siblings for ASS:
// the Style's value (or the value of the tag in effect at the caret) is
// switched, and with a selection the previous value is restored after it.
// `tag` is 'b', 'i', 'u' or 's'.
EditorText toggleTag(EditorText state, char16_t tag, bool styleValue);

// The same commands for the other formats (EditBox::PutinNonass): SRT gets
// <b>/<i>/<u>/<s>, MicroDVD gets {Y:b}/{Y:i} after the last "|" before the
// caret; everything else (MicroDVD underline/strikeout, MPL2, TMPlayer) is
// left unchanged, as in the legacy editor. `srt` selects the SRT form.
EditorText toggleNonAssTag(EditorText state, char16_t tag, bool srt);

// A custom tag button (EditBox::OnButtonTag, EDITBOX_TAG_BUTTON1-20).
// Types 0 ("Tag inserted in place of cursor") and 1 ("Insert Tag at text
// beginning") put the override tag in with the legacy reset: the value in
// effect (found in the text), else the Style's (`styleValue` for the tag
// name, legacy TagValueFromStyle), else "0"; \r resets to itself.
EditorText applyTagButton(EditorText state, std::u16string_view tag, bool atTextStart,
                          const std::function<std::optional<std::u16string>(std::u16string_view)> &styleValue);
// Type 2 ("Plain text"): the text replaces the selection; a caret inside an
// override block moves past it first (legacy index arithmetic kept).
EditorText insertTagButtonText(EditorText state, std::u16string_view text);
// The same insertion into another selected Line's text at `from` (several
// Lines selected): the position is clamped to the text.
std::u16string insertTagButtonTextAt(std::u16string text, long from, std::u16string_view insert);

} // namespace hikari::core::legacy
