#pragma once

// Legacy ASS tag commands of the Line editor (V2-E-grammar, E28-grammar):
// TagFindReplace::FindTag and PutTagInText, as EditBox's Bold, Italic,
// Underline and Strikeout commands use them. The search is positional and
// keeps the legacy quirks (index arithmetic on signed/unsigned values, the
// "\t(...)" handling, "\r" stopping at the block before the caret). Text and
// positions are UTF-16 code units, as on Windows, where wxString is UTF-16.

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

} // namespace hikari::core::legacy
