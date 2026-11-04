#include "hikari/application/fold_groups.h"

#include "hikari/core/aegisub_folds.h"
#include "hikari/core/line_groups.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace hikari::application {

namespace {

struct Plan {
    struct Fold {
        std::size_t from = 0, to = 0;
        core::GroupMarker state = core::GroupMarker::Opened;
    };
    std::vector<Fold> folds;
    std::size_t skipped = 0;
};

Plan planOf(const core::Document &document)
{
    const auto lines = document.lines();
    std::map<core::LineId, std::size_t> rows;
    for (std::size_t r = 0; r < lines.size(); ++r)
        rows.emplace(lines[r]->id, r);
    const auto found = core::aegisubFolds(document);
    Plan plan;
    plan.skipped = found.dropped.size();
    for (const auto &fold : found.folds) {
        const auto from = rows.at(fold.opener), to = rows.at(fold.ender);
        const bool grouped = std::any_of(lines.begin() + static_cast<std::ptrdiff_t>(from),
                                         lines.begin() + static_cast<std::ptrdiff_t>(to) + 1,
                                         [](const core::LineRecord *l) { return l->group != core::GroupMarker::None; });
        if (grouped)
            ++plan.skipped;
        else
            plan.folds.push_back({from, to, fold.collapsed ? core::GroupMarker::Closed : core::GroupMarker::Opened});
    }
    return plan;
}

std::u8string withoutOverrideBlocks(std::u8string_view text)
{
    std::u8string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u8'{') {
            if (const auto close = text.find(u8'}', i); close != std::u8string_view::npos) {
                i = close;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

}

FoldConversion foldConversion(const core::Document &document)
{
    const auto plan = planOf(document);
    return {plan.folds.size(), plan.skipped};
}

std::expected<FoldConversion, CommandRefusal> convertFoldsToGroups(EditSession &session)
{
    const auto plan = planOf(session.document());
    if (plan.folds.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto lines = session.document().lines();
    std::vector<std::vector<core::GroupMarker>> memberships(lines.size());
    std::vector<std::vector<core::GroupMarker>> outerOf(plan.folds.size());
    for (std::size_t f = 0; f < plan.folds.size(); ++f) {
        const auto &fold = plan.folds[f];
        outerOf[f] = memberships[fold.from];
        for (std::size_t r = fold.from; r <= fold.to; ++r)
            memberships[r].push_back(fold.state);
    }
    std::set<core::LineId> touched;
    for (const auto *l : lines)
        touched.insert(l->id);
    std::optional<core::LineId> firstDescription;
    const auto ran = session.run(Command{"Converting folds to line groups", session.revision(), touched, [&](core::Document &d) {
                                             for (std::size_t r = 0; r < lines.size(); ++r) {
                                                 if (memberships[r].empty())
                                                     continue;
                                                 if (!d.editLine(lines[r]->id, [&](core::LineRecord &l) {
                                                         core::setGroupMarkers(l, memberships[r], false);
                                                     }))
                                                     return false;
                                             }
                                             for (std::size_t f = 0; f < plan.folds.size(); ++f) {
                                                 const auto &fold = plan.folds[f];
                                                 for (const auto r : {fold.from, fold.to})
                                                     if (!d.editLine(lines[r]->id, [](core::LineRecord &l) {
                                                             std::erase_if(l.extra, [](const core::Extradata &e) {
                                                                 return e.key == core::kFoldKey;
                                                             });
                                                         }))
                                                         return false;
                                                 const auto &opener = *lines[fold.from];
                                                 core::LineRecord description;
                                                 description.comment = true;
                                                 description.style = opener.style;
                                                 description.text = withoutOverrideBlocks(opener.text);
                                                 core::setGroupMarkers(description, outerOf[f], true);
                                                 const auto id = d.insertLineBefore(opener.id, description);
                                                 if (!id)
                                                     return false;
                                                 if (!firstDescription)
                                                     firstDescription = id;
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    Selection selection = session.selection();
    for (const auto *l : session.document().lines())
        if (selection.active == l->id && l->visibility == core::LineVisibility::Hidden) {
            selection.active = firstDescription;
            session.setSelection(std::move(selection));
            break;
        }
    return FoldConversion{plan.folds.size(), plan.skipped};
}

}
