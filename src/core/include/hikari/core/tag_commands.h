#pragma once

// Text and positions are UTF-16 code units.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hikari::core::legacy {

// The override block around position `from`, or -1s.
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
    // Without focus the caret goes to the tag's position instead of after it
    // (inside a block only).
    void setFocus(bool focus) { m_focus = focus; }

    const std::u16string &finding() const { return m_finding; }
    const EditorText &state() const { return m_state; }
    std::pair<long, long> position() const { return {m_posX, m_posY}; }
    void setPosition(long x, long y)
    {
        m_posX = x;
        m_posY = y;
    }
    bool inBracket() const { return m_inBracket; }

private:
    EditorText m_state;
    long m_from = 0, m_to = 0;
    std::u16string m_lastPattern;
    std::pair<long, long> m_lastSelection{0, 0};
    std::u16string m_finding;
    long m_posX = 0, m_posY = 0;
    long m_cursor = 0;
    bool m_inBracket = false;
    bool m_hasSelection = false;
    bool m_focus = true;
};

// The tag replaces the pattern's match found from position 0, or goes into
// the first block; with no block it is prepended in a new one.
std::u16string putTagInLine(std::u16string text, std::u16string_view pattern, std::u16string_view tag);

// SRT wraps the selection (<tag> ... </tag>), MicroDVD puts {tag} after the
// last "|" before the caret; TMPlayer and MPL2 are unchanged.
enum class NonAssFormat { Srt, MicroDvd, Mpl2, TmPlayer };
// MUST KILL: `text` is the regex finding an existing tag and `tag` what is written
EditorText putInNonAss(EditorText state, NonAssFormat format, std::u16string_view text, std::u16string_view tag);
// Several Lines: each Line's text gets <text>/{text} in front, with matches
// of the `tag` regex removed first when the text starts with the bracket.
// TMPlayer is unchanged.
std::u16string putInNonAssLine(std::u16string line, NonAssFormat format, std::u16string_view text,
                               std::u16string_view tag);

// The Style's value (or the tag's in effect at the caret) is switched; with a
// selection the previous value is restored after it. `tag` is 'b', 'i', 'u'
// or 's'.
EditorText toggleTag(EditorText state, char16_t tag, bool styleValue);

// SRT gets <b>/<i>/<u>/<s>, MicroDVD {Y:b}/{Y:i} after the last "|" before
// the caret; everything else is left unchanged. `srt` selects the SRT form.
EditorText toggleNonAssTag(EditorText state, char16_t tag, bool srt);

// The override tag goes in with a reset to the value in effect, else the
// Style's (`styleValue`), else "0"; \r resets to itself.
EditorText applyTagButton(EditorText state, std::u16string_view tag, bool atTextStart,
                          const std::function<std::optional<std::u16string>(std::u16string_view)> &styleValue);
// The text replaces the selection; a caret inside an override block moves
// past it first.
EditorText insertTagButtonText(EditorText state, std::u16string_view text);
// `from` is clamped to the text.
std::u16string insertTagButtonTextAt(std::u16string text, long from, std::u16string_view insert);

}
