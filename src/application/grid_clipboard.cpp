#include "hikari/application/grid_clipboard.h"

#include "hikari/application/position_commands.h"

#include "hikari/core/extradata.h"
#include "hikari/core/line_groups.h"

#include <algorithm>
#include <ranges>
#include <vector>

namespace hikari::application {

namespace {

std::vector<const core::LineRecord *> shownSelected(const EditSession &session, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *line : session.document().lines())
        if (session.selection().selected.contains(line->id) && (!visible || visible(line->id)))
            out.push_back(line);
    return out;
}

}

bool translationMode(const EditSession &session)
{
    return session.document().scriptInfo(u8"TLMode") == u8"Yes";
}

std::u8string copyRows(const EditSession &session)
{
    std::vector<core::LineId> ids;
    for (const auto *line : session.document().lines())
        if (session.selection().selected.contains(line->id))
            ids.push_back(line->id);
    return core::clipboardRows(session.document(), ids, translationMode(session));
}

std::u8string copyRowsExtradata(const EditSession &session)
{
    std::vector<std::vector<core::Extradata>> rows;
    bool any = false;
    for (const auto *line : session.document().lines())
        if (session.selection().selected.contains(line->id)) {
            rows.push_back(line->extra);
            any = any || !line->extra.empty();
        }
    return any ? core::extradataClipboard(rows) : std::u8string{};
}

namespace {

bool copiesTags(int columns)
{
    using namespace core::column;
    return (columns & Tags) != 0 && (columns & (Text | Translation)) == 0;
}

std::u8string copiedTags(std::u8string_view text, int columns, std::span<const std::u8string> tagNames)
{
    if (columns & core::column::StartBlock) {
        const auto block = position::startBlock(text);
        return block.empty() ? u8"{}" : block;
    }
    std::u8string tags;
    if (columns & core::column::Clip) {
        tags = position::startTag(text, u8"clip");
        if (tags.empty())
            tags = position::startTag(text, u8"iclip");
    }
    if (columns & core::column::NamedTags)
        for (const auto &name : tagNames)
            tags += position::startTag(text, name);
    return u8"{" + tags + u8"}";
}

std::u8string pastedTags(std::u8string_view text, std::u8string_view pasted, int columns,
                         std::span<const std::u8string> tagNames)
{
    if (columns & core::column::StartBlock) {
        const auto block = position::startBlock(pasted);
        return position::withStartBlock(text, block == u8"{}" ? std::u8string() : block);
    }
    std::u8string out(text);
    if (columns & core::column::Clip)
        for (const std::u8string_view name : {std::u8string_view(u8"clip"), std::u8string_view(u8"iclip")})
            if (const auto clip = position::startTag(pasted, name); !clip.empty()) {
                const auto other = name == u8"clip" ? std::u8string_view(u8"iclip") : std::u8string_view(u8"clip");
                if (const auto old = position::startTag(out, other); !old.empty())
                    out = position::withStartTag(out, other, clip);
                else
                    out = position::withStartTag(out, name, clip);
                break;
            }
    if (columns & core::column::NamedTags)
        for (const auto &name : tagNames)
            if (const auto tag = position::startTag(pasted, name); !tag.empty())
                out = position::withStartTag(out, name, tag);
    return out;
}

}

std::u8string copyColumns(const EditSession &session, int columns, std::span<const std::u8string> tagNames)
{
    std::vector<core::LineId> ids;
    for (const auto *line : session.document().lines())
        if (session.selection().selected.contains(line->id))
            ids.push_back(line->id);
    if (!copiesTags(columns))
        return core::clipboardColumns(session.document(), ids, columns & ~core::column::Tags, translationMode(session));
    core::Document tagged = session.document();
    const bool tl = translationMode(session);
    for (const auto id : ids)
        tagged.editLine(id, [&](core::LineRecord &l) {
            auto &text = tl && !l.translation.empty() ? l.translation : l.text;
            text = copiedTags(text, columns, tagNames);
        });
    return core::clipboardColumns(tagged, ids, (columns & ~core::column::Tags) | core::column::Text, tl);
}

std::expected<void, CommandRefusal> pasteRows(EditSession &session, std::u8string_view text, const LineVisible &visible,
                                              const core::PasteConversion &conversion, std::u8string_view extradata)
{
    const auto selected = shownSelected(session, visible);
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    auto pasted = core::parseClipboardRows(text, session.document().format(), conversion);
    if (pasted.empty())
        return {};
    if (auto extras = core::parseExtradataClipboard(extradata); extras.size() == pasted.size())
        for (std::size_t i = 0; i < pasted.size(); ++i)
            pasted[i].extra = std::move(extras[i]);
    const auto lines = session.document().lines();
    std::size_t row = 0;
    while (lines[row]->id != selected.front()->id)
        ++row;
    const core::LineRecord *after = lines[row];
    const auto context = core::groupMemberships(*after);
    const auto base = std::ranges::min(pasted | std::views::transform([](const core::LineRecord &l) {
                                           return core::groupDepth(l);
                                       }));
    // MUST KILL: only the Line after the paste position sets visibility
    const core::LineVisibility visibility = after->visibility;
    for (auto &line : pasted) {
        if (!context.empty() || base > 0) {
            const auto own = core::groupMemberships(line);
            auto markers = context;
            markers.insert(markers.end(), own.begin() + static_cast<std::ptrdiff_t>(base), own.end());
            core::setGroupMarkers(line, std::move(markers), line.group == core::GroupMarker::Description);
        }
        if (visibility != core::LineVisibility::Visible)
            line.visibility = visibility;
    }
    std::vector<core::LineId> added;
    const core::LineId at = after->id;
    const auto ran = session.run(Command{"Pasting lines", session.revision(), {at}, [&](core::Document &d) {
                                             added.clear();
                                             for (const auto &line : pasted) {
                                                 const auto id = d.insertLineBefore(at, line);
                                                 if (!id)
                                                     return false;
                                                 added.push_back(*id);
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    Selection selection;
    selection.active = selection.anchor = added.front();
    selection.selected = {added.begin(), added.end()};
    session.setSelection(std::move(selection));
    return {};
}

std::expected<void, CommandRefusal> pasteColumns(EditSession &session, std::u8string_view text, int columns,
                                                 const LineVisible &visible, const core::PasteConversion &conversion,
                                                 std::span<const std::u8string> tagNames)
{
    const int tags = copiesTags(columns) ? columns & core::column::Tags : 0;
    const bool tl = translationMode(session);
    columns &= ~core::column::Tags;
    const auto selected = shownSelected(session, visible);
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto pasted = core::parseClipboardRows(text, session.document().format(), conversion,
                                                 (columns & core::column::Translation) != 0);
    std::vector<std::pair<core::LineId, const core::LineRecord *>> changes;
    for (std::size_t i = 0; i < pasted.size() && i < selected.size(); ++i)
        changes.emplace_back(selected[i]->id, &pasted[i]);
    auto apply = [&](core::LineRecord &l, const core::LineRecord &from) {
        using namespace core::column;
        if (tags) {
            auto &target = tl && !l.translation.empty() ? l.translation : l.text;
            target = pastedTags(target, from.text, tags, tagNames);
        }
        if (columns & Layer)
            l.layer.value = from.layer.value;
        if (columns & Start) {
            l.start.value = from.start.value;
            l.startFrame = from.startFrame;
        }
        if (columns & End) {
            l.end.value = from.end.value;
            l.endFrame = from.endFrame;
        }
        if (columns & Style)
            l.style = from.style;
        if (columns & Actor)
            l.actor = from.actor;
        if (columns & MarginLeft)
            l.marginLeft.value = from.marginLeft.value;
        if (columns & MarginRight)
            l.marginRight.value = from.marginRight.value;
        if (columns & MarginVertical)
            l.marginVertical.value = from.marginVertical.value;
        if (columns & Effect)
            l.effect = from.effect;
        if (columns & Text)
            l.text = from.text;
        if (columns & Translation)
            l.translation = from.translation;
    };
    std::vector<core::LineId> touched;
    for (const auto &[id, from] : changes)
        touched.push_back(id);
    if (!changes.empty()) {
        const auto ran = session.run(Command{"Pasting columns", session.revision(), {touched.begin(), touched.end()}, [&](core::Document &d) {
                                                 for (const auto &[id, from] : changes)
                                                     if (!d.editLine(id, [&](core::LineRecord &l) { apply(l, *from); }))
                                                         return false;
                                                 return true;
                                             }});
        if (!ran)
            return std::unexpected(ran.error());
    }
    Selection selection = session.selection();
    selection.active = selected.front()->id;
    session.setSelection(std::move(selection));
    return {};
}

}
