#include "hikari/application/text_import.h"

#include "hikari/application/document_files.h"
#include "hikari/core/ass_load.h"

#include <cstring>
#include <string>
#include <vector>

namespace hikari::application {

namespace {

core::LineRecord record(const core::TextImportLine &line, std::u8string_view style)
{
    core::LineRecord out;
    out.comment = line.comment;
    out.style = std::u8string(style);
    out.actor = line.actor;
    out.text = line.text;
    return out;
}

}

core::LoadResult textImportFile(std::span<const core::TextImportLine> lines)
{
    std::string script(defaultScript());
    script.erase(script.rfind("Dialogue:"));
    std::vector<std::byte> bytes(script.size());
    std::memcpy(bytes.data(), script.data(), script.size());
    core::LoadResult load = core::loadAss(bytes);
    for (const core::TextImportLine &line : lines)
        (void)load.document.appendLine(record(line, u8"Default"));
    return load;
}

std::expected<void, CommandRefusal> addTextLines(EditSession &session, std::span<const core::TextImportLine> lines,
                                                 std::u8string_view style)
{
    if (session.document().format() != core::SubtitleFormat::Ass || lines.empty())
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<core::LineId> ids;
    const auto ran = session.run(Command{"Import text", session.revision(), {}, [&](core::Document &d) {
                                             ids.clear();
                                             for (const core::TextImportLine &line : lines) {
                                                 const auto id = d.appendLine(record(line, style));
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
