#include "hikari/application/translation_mode.h"

#include "hikari/application/grid_translation.h"

#include <vector>

namespace hikari::application {

namespace {

using u8 = std::u8string;
using u8v = std::u8string_view;
constexpr auto npos = u8v::npos;

// wxString::Mid(first, count): empty past the end, clamped to the rest.
u8 mid(u8v s, std::size_t first, std::size_t count = npos)
{
    if (first > s.size())
        return {};
    return u8(s.substr(first, count));
}

// wxString::SubString(from, to) is Mid(from, to - from + 1) in size_t: with
// `to` before `from` - 1 the count wraps and the rest of the string is taken.
u8 subString(u8v s, std::size_t from, std::size_t to)
{
    const std::size_t count = to + 1 >= from ? to + 1 - from : npos;
    return mid(s, from, count);
}

// wxString::Replace("}{", "") with replaceAll: the occurrences found left to
// right in the original string are removed.
u8 removeJoinedBlocks(u8v s)
{
    u8 out;
    std::size_t at = 0;
    for (std::size_t pos; (pos = s.find(u8"}{", at)) != npos; at = pos + 2)
        out += s.substr(at, pos - at);
    out += s.substr(at);
    return out;
}

std::size_t utf16Length(u8v s)
{
    std::size_t n = 0;
    for (const char8_t c : s) {
        if ((c & 0xC0) != 0x80)
            ++n;
        if ((c & 0xF8) == 0xF0)
            ++n; // a supplementary character is a surrogate pair
    }
    return n;
}

bool translationModeOn(const core::Document &document)
{
    return document.scriptInfo(u8"TLMode") == std::optional<u8>(u8"Yes");
}

} // namespace

std::expected<void, CommandRefusal> turnOnTranslationModeStep(EditSession &session)
{
    const auto &document = session.document();
    const auto format = document.format();
    if (translationModeOn(document) || (format != core::SubtitleFormat::Ass && format != core::SubtitleFormat::PlainText))
        return std::unexpected(CommandRefusal::Invalid);
    return session.run(Command{"Turning on translator mode", session.revision(), {},
                               [](core::Document &d) { return turnOnTranslationMode(d); }});
}

std::expected<void, CommandRefusal> toggleUnconfirmed(EditSession &session)
{
    if (!translationModeOn(session.document()))
        return std::unexpected(CommandRefusal::Invalid);
    // `line->ChangeState(4)` on the editor's Line, then on each selected one.
    std::set<core::LineId> lines = session.selection().selected;
    if (const auto active = session.selection().active)
        lines.insert(*active);
    if (lines.empty())
        return std::unexpected(CommandRefusal::Invalid);
    // E5-unconfirmed-own-step: typed text on one of these Lines is its own
    // step first, then the flip is "Mark unconfirmed" on top of it (legacy's
    // flip joined that text's step). Committed here, before the revision is
    // taken, so the flip is not refused as stale.
    if (const auto draft = session.draftLine(); draft && lines.contains(*draft)) {
        if (session.draftProblem() && session.invalidCommitPolicy() == InvalidCommitPolicy::Block)
            return std::unexpected(CommandRefusal::InvalidDraft);
        if (!session.commitDraft() && session.draftLine())
            return std::unexpected(CommandRefusal::InvalidDraft);
    }
    return session.run(Command{"Mark unconfirmed", session.revision(), lines, [&](core::Document &d) {
                                   for (const auto id : lines)
                                       if (!d.editLine(id, [](core::LineRecord &l) { l.unconfirmed = !l.unconfirmed; }))
                                           return false;
                                   return true;
                               }});
}

std::optional<MovedTags> moveTagsFromOriginal(std::u8string_view source)
{
    const u8 text = removeJoinedBlocks(source);
    std::size_t getr = text.find(u8'}');
    if (getr == npos)
        return std::nullopt;
    std::size_t brackets = text.find(u8'{');
    u8 rest = text.size() > getr + 1 ? mid(text, getr + 1) : u8();
    MovedTags out;
    std::size_t pos = 0;
    if (text.starts_with(u8'{')) {
        out.translation = text.substr(0, getr + 1);
        pos = out.translation.size();
    } else if (brackets != npos && brackets > 0) {
        out.original = text.substr(0, brackets);
        out.translation = subString(text, brackets, getr);
    } else {
        out.original = text.substr(0, getr + 1);
    }
    for (;;) {
        brackets = rest.find(u8'{');
        getr = rest.find(u8'}');
        if (brackets == npos || getr == npos) {
            out.original += rest;
            break;
        }
        out.original += rest.substr(0, brackets);
        out.translation += subString(rest, brackets, getr);
        if (rest.size() > getr + 1)
            rest = mid(rest, getr + 1);
        else
            break;
    }
    out.caret = utf16Length(u8v(out.translation).substr(0, pos));
    return out;
}

void OriginalColumns::see(const core::Document &document)
{
    m_seen = true;
    m_tlMode = document.scriptInfo(u8"TLMode");
}

bool OriginalColumns::observe(const core::Document &document, bool showOriginalSetting)
{
    const auto tlMode = document.scriptInfo(u8"TLMode");
    const bool showtl = document.scriptInfo(u8"TLMode Showtl") == std::optional<u8>(u8"Yes");
    const bool on = tlMode == std::optional<u8>(u8"Yes");
    if (!m_seen)
        m_shown = on && (showtl || showOriginalSetting); // Clearing, then LoadSubtitles
    else if (tlMode != m_tlMode)
        m_shown = showtl || (on && showOriginalSetting); // DoUndo
    see(document);
    return m_shown;
}

void OriginalColumns::turnedOn(const core::Document &after, bool showOriginalSetting)
{
    if (showOriginalSetting)
        m_shown = true;
    see(after);
}

void OriginalColumns::turnedOff(const core::Document &after)
{
    m_shown = false;
    see(after);
}

void OriginalColumns::pasted(const core::Document &after)
{
    m_shown = true;
    see(after);
}

} // namespace hikari::application
