#include "hikari/application/lyrics_insert.h"

#include <vector>

namespace hikari::application {

std::expected<void, CommandRefusal> addLyricsLines(EditSession &session, const core::Document &lyrics,
                                                   std::u8string_view style)
{
    if (session.document().format() != core::SubtitleFormat::Ass || lyrics.lines().empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<core::LineRecord> added;
    for (const core::LineRecord *line : lyrics.lines()) {
        core::LineRecord copy;
        copy.start = line->start;
        copy.end = line->end;
        copy.style = std::u8string(style);
        copy.actor = line->actor;
        copy.text = line->text;
        added.push_back(std::move(copy));
    }
    std::vector<core::LineId> ids;
    const auto ran = session.run(Command{"Import lyrics", session.revision(), {}, [&](core::Document &d) {
                                             ids.clear();
                                             for (const auto &line : added) {
                                                 const auto id = d.appendLine(line);
                                                 if (!id)
                                                     return false;
                                                 ids.push_back(*id);
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    Selection selection;
    selection.active = selection.anchor = ids.front();
    selection.selected = {ids.begin(), ids.end()};
    session.setSelection(std::move(selection));
    return {};
}

}
