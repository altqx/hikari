#include "hikari/application/ai_review.h"

#include "hikari/core/style.h"

#include <absl/container/flat_hash_map.h>
#include <boost/dynamic_bitset.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <variant>

namespace hikari::application {

namespace {

using Lines = std::vector<const core::LineRecord *>;
using Fields = std::vector<std::u8string>;

const core::LineRecord *findLine(const Lines &lines, core::LineId id)
{
    for (const auto *line : lines)
        if (line->id == id)
            return line;
    return nullptr;
}

std::optional<core::LineId> nextOf(const Lines &lines, core::LineId id)
{
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i]->id == id)
            return i + 1 < lines.size() ? std::optional(lines[i + 1]->id) : std::nullopt;
    return std::nullopt;
}

std::map<std::u8string, Fields> stylesOf(const core::Document &document)
{
    std::map<std::u8string, Fields> out;
    for (const core::StyleValues &style : core::decodeStyles(document))
        out.try_emplace(style.name, core::legacy::styleRawFields(style));
    return out;
}

std::map<std::u8string, std::u8string> infoOf(const core::Document &document)
{
    std::map<std::u8string, std::u8string> out;
    for (const core::Section &section : document.sections())
        if (section.kind == core::SectionKind::ScriptInfo)
            for (const core::Record &record : section.records)
                if (const auto *property = std::get_if<core::PropertyRecord>(&record))
                    out[property->key] = property->value;
    return out;
}

template <typename T>
std::optional<T> lookup(const std::map<std::u8string, T> &map, const std::u8string &key)
{
    const auto found = map.find(key);
    return found == map.end() ? std::nullopt : std::optional(found->second);
}

// The common Lines that left the longest run kept in order moved.
std::set<core::LineId> movedLines(const Lines &before, const Lines &after)
{
    absl::flat_hash_map<std::uint64_t, std::size_t> at;
    for (std::size_t i = 0; i < before.size(); ++i)
        at[before[i]->id.value] = i;
    std::vector<std::pair<std::size_t, core::LineId>> common;
    for (const auto *line : after)
        if (const auto found = at.find(line->id.value); found != at.end())
            common.emplace_back(found->second, line->id);
    std::vector<std::size_t> tails, tailAt, previous(common.size(), common.size());
    for (std::size_t i = 0; i < common.size(); ++i) {
        const auto slot = std::ranges::lower_bound(tails, common[i].first) - tails.begin();
        if (std::size_t(slot) == tails.size()) {
            tails.push_back(common[i].first);
            tailAt.push_back(i);
        } else {
            tails[slot] = common[i].first;
            tailAt[slot] = i;
        }
        if (slot > 0)
            previous[i] = tailAt[slot - 1];
    }
    std::set<core::LineId> moved;
    for (const auto &[index, id] : common)
        moved.insert(id);
    for (std::size_t i = tailAt.empty() ? common.size() : tailAt.back(); i < common.size(); i = previous[i])
        moved.erase(common[i].second);
    return moved;
}

std::vector<AiReview::Item> diff(const core::Document &beforeDocument, const core::Document &afterDocument)
{
    using Item = AiReview::Item;
    const Lines before = beforeDocument.lines(), after = afterDocument.lines();
    absl::flat_hash_map<std::uint64_t, std::size_t> beforeAt;
    for (std::size_t i = 0; i < before.size(); ++i)
        beforeAt[before[i]->id.value] = i;
    const std::set<core::LineId> moved = movedLines(before, after);
    const auto nextInBefore = [&](std::size_t i) {
        return i + 1 < before.size() ? std::optional(before[i + 1]->id) : std::nullopt;
    };
    std::vector<Item> items;
    std::set<std::uint64_t> kept;
    for (const auto *line : after) {
        const auto found = beforeAt.find(line->id.value);
        if (found == beforeAt.end()) {
            items.push_back(Item{.line = line->id, .after = *line});
            continue;
        }
        kept.insert(line->id.value);
        const core::LineRecord &old = *before[found->second];
        const bool isMoved = moved.contains(line->id);
        if (isMoved || !sameLineFields(old, *line))
            items.push_back(Item{.line = line->id, .before = old, .after = *line,
                                 .beforeNext = nextInBefore(found->second), .beforeRow = found->second,
                                 .moved = isMoved});
    }
    for (std::size_t i = 0; i < before.size(); ++i)
        if (!kept.contains(before[i]->id.value))
            items.push_back(Item{.line = before[i]->id, .before = *before[i], .beforeNext = nextInBefore(i),
                                 .beforeRow = i});

    const auto stylesBefore = stylesOf(beforeDocument), stylesAfter = stylesOf(afterDocument);
    std::set<std::u8string> styleNames;
    for (const auto *map : {&stylesBefore, &stylesAfter})
        for (const auto &[name, fields] : *map)
            styleNames.insert(name);
    for (const std::u8string &name : styleNames) {
        auto was = lookup(stylesBefore, name), is = lookup(stylesAfter, name);
        if (was != is)
            items.push_back(Item{.kind = AiReview::ItemKind::Style, .key = name, .styleBefore = std::move(was),
                                 .styleAfter = std::move(is)});
    }
    const auto infoBefore = infoOf(beforeDocument), infoAfter = infoOf(afterDocument);
    std::set<std::u8string> keys;
    for (const auto *map : {&infoBefore, &infoAfter})
        for (const auto &[key, value] : *map)
            keys.insert(key);
    for (const std::u8string &key : keys) {
        auto was = lookup(infoBefore, key), is = lookup(infoAfter, key);
        if (was != is)
            items.push_back(Item{.kind = AiReview::ItemKind::Info, .key = key, .infoBefore = std::move(was),
                                 .infoAfter = std::move(is)});
    }
    return items;
}

bool onLine(const AiReview::Item &item, core::LineId line)
{
    return item.kind == AiReview::ItemKind::Line && item.line == line;
}

std::string keyOf(const AiReview::Item &item)
{
    switch (item.kind) {
    case AiReview::ItemKind::Line: return "l" + std::to_string(item.line.value);
    case AiReview::ItemKind::Style: return "s" + std::string(item.key.begin(), item.key.end());
    case AiReview::ItemKind::Info: return "i" + std::string(item.key.begin(), item.key.end());
    }
    return {};
}

// What the Document holds now for an item, against the item's before and after.
struct Now {
    bool applied = false; // differs from before: undoing it would change something
    bool matchesAfter = false;
};

class Snapshot {
public:
    explicit Snapshot(const core::Document &document) : m_document(document), m_lines(document.lines()) {}

    Now of(const AiReview::Item &item) const
    {
        switch (item.kind) {
        case AiReview::ItemKind::Line: {
            const core::LineRecord *line = findLine(m_lines, item.line);
            if (!item.before)
                return {line != nullptr, line && sameLineFields(*line, *item.after)};
            if (!item.after)
                return {line == nullptr, line == nullptr};
            if (!line)
                return {};
            const bool fieldsBack = sameLineFields(*line, *item.before);
            const bool placeBack = !item.moved || nextOf(m_lines, item.line) == item.beforeNext;
            return {!fieldsBack || !placeBack, sameLineFields(*line, *item.after)};
        }
        case AiReview::ItemKind::Style: {
            const auto now = lookup(styles(), item.key);
            return {now != item.styleBefore, now == item.styleAfter};
        }
        case AiReview::ItemKind::Info: {
            const auto now = lookup(info(), item.key);
            return {now != item.infoBefore, now == item.infoAfter};
        }
        }
        return {};
    }

    const Lines &lines() const { return m_lines; }

private:
    const std::map<std::u8string, Fields> &styles() const
    {
        if (!m_styles)
            m_styles = stylesOf(m_document);
        return *m_styles;
    }
    const std::map<std::u8string, std::u8string> &info() const
    {
        if (!m_info)
            m_info = infoOf(m_document);
        return *m_info;
    }

    const core::Document &m_document;
    Lines m_lines;
    mutable std::optional<std::map<std::u8string, Fields>> m_styles;
    mutable std::optional<std::map<std::u8string, std::u8string>> m_info;
};

struct Live {
    std::size_t change = 0; // index into the list given
    std::size_t item = 0;
    bool editedSince = false;
};

// The applied items among `items` (oldest first). An item counts as edited since
// when the Document no longer holds what the newest applied change on it left.
template <typename C, typename Pick>
std::vector<Live> liveItems(const std::vector<C *> &changes, const Snapshot &now, Pick pick)
{
    std::vector<Live> out;
    std::map<std::string, std::size_t> newest;
    boost::dynamic_bitset<> matches;
    for (std::size_t c = 0; c < changes.size(); ++c)
        for (std::size_t i = 0; i < changes[c]->items.size(); ++i) {
            const AiReview::Item &item = changes[c]->items[i];
            if (!pick(item))
                continue;
            const Now state = now.of(item);
            if (!state.applied)
                continue;
            newest[keyOf(item)] = out.size();
            out.push_back(Live{c, i, false});
            matches.push_back(state.matchesAfter);
        }
    for (std::size_t k = 0; k < out.size(); ++k) {
        const std::size_t last = newest[keyOf(changes[out[k].change]->items[out[k].item])];
        out[k].editedSince = !matches[last];
    }
    return out;
}

const core::LineRecord *findIn(const core::Document &document, core::LineId id)
{
    return findLine(document.lines(), id);
}

std::optional<std::size_t> styleIndex(const core::Document &document, const std::u8string &name)
{
    const auto styles = core::decodeStyles(document);
    for (std::size_t i = 0; i < styles.size(); ++i)
        if (styles[i].name == name)
            return i;
    return std::nullopt;
}

// Puts each change's items back as they were before it, newest change first.
// Places come back from the last original row up, so each Line's old
// neighbour is already where it belongs.
bool undoItems(core::Document &doc, const std::vector<std::vector<const AiReview::Item *>> &newestFirst)
{
    std::map<std::uint64_t, core::LineId> renamed;
    const auto current = [&](core::LineId id) {
        const auto found = renamed.find(id.value);
        return found == renamed.end() ? id : found->second;
    };
    const auto place = [&](const AiReview::Item &item) -> std::optional<core::LineId> {
        if (!item.beforeNext)
            return std::nullopt;
        const core::LineId next = current(*item.beforeNext);
        return findIn(doc, next) ? std::optional(next) : std::nullopt;
    };
    for (const auto &items : newestFirst) {
        std::vector<const AiReview::Item *> placed;
        for (const AiReview::Item *item : items) {
            switch (item->kind) {
            case AiReview::ItemKind::Line: {
                const core::LineId id = current(item->line);
                if (!item->before)
                    doc.removeLine(id);
                else if (!item->after || item->moved)
                    placed.push_back(item);
                if (item->before && item->after &&
                    !doc.editLine(id, [&](core::LineRecord &line) { line = *item->before; line.id = id; },
                                  core::ChangeMark::Restored))
                    return false;
                break;
            }
            case AiReview::ItemKind::Style: {
                const auto at = styleIndex(doc, item->key);
                if (!item->styleBefore) {
                    if (at)
                        doc.removeStyle(item->key);
                } else if (at) {
                    doc.editStyle(*at, *item->styleBefore);
                } else {
                    doc.appendStyle(*item->styleBefore);
                }
                break;
            }
            case AiReview::ItemKind::Info:
                if (item->infoBefore)
                    doc.setScriptInfo(item->key, *item->infoBefore);
                else
                    doc.removeScriptInfo(item->key);
                break;
            }
        }
        std::ranges::sort(placed, std::greater{}, &AiReview::Item::beforeRow);
        for (const AiReview::Item *item : placed) {
            const auto next = place(*item);
            if (item->after) {
                if (!doc.moveLine(current(item->line), next))
                    return false;
                continue;
            }
            const auto inserted = next ? doc.insertLineBefore(*next, *item->before, core::ChangeMark::Kept)
                                       : doc.appendLine(*item->before, core::ChangeMark::Kept);
            if (!inserted)
                return false;
            renamed[item->line.value] = *inserted;
        }
    }
    return true;
}

// Groups (change, item) pairs given oldest first into items per change, newest change first.
template <typename C>
std::vector<std::vector<const AiReview::Item *>> byChangeNewestFirst(const std::vector<std::pair<C *, std::size_t>> &picked)
{
    std::vector<std::vector<const AiReview::Item *>> out;
    const void *last = nullptr;
    for (auto it = picked.rbegin(); it != picked.rend(); ++it) {
        if (it->first != last)
            out.emplace_back();
        last = it->first;
        out.back().push_back(&it->first->items[it->second]);
    }
    return out;
}

std::string text(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::size_t rowOf(const core::Document &document, core::LineId id)
{
    const auto lines = document.lines();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i]->id == id)
            return i + 1;
    return 0;
}

}

std::uint64_t AiReview::beginTurn(const std::string &caller, std::string label)
{
    const std::uint64_t id = m_nextId++;
    m_turns.push_back(Turn{id, m_turns.size() + 1, caller, std::move(label)});
    const auto open = std::ranges::find(m_open, caller, &std::pair<std::string, Open>::first);
    if (open == m_open.end())
        m_open.emplace_back(caller, Open{id, {}});
    else
        open->second = Open{id, {}};
    return id;
}

void AiReview::record(DocumentId document, const std::string &callerId, const std::string &callerLabel,
                      std::string name, const core::Document &before, ContentId previous, const EditSession &session)
{
    std::vector<Item> items = diff(before, session.document());
    if (items.empty())
        return;
    auto open = std::ranges::find(m_open, callerId, &std::pair<std::string, Open>::first);
    bool fresh = open == m_open.end();
    if (!fresh) {
        const auto &last = open->second.last;
        const auto here = std::ranges::find(last, document, &std::pair<DocumentId, ContentId>::first);
        fresh = here != last.end() && here->second != previous;
    }
    if (fresh) {
        beginTurn(callerId, callerLabel);
        open = std::ranges::find(m_open, callerId, &std::pair<std::string, Open>::first);
    }
    Open &turn = open->second;
    const ContentId step = session.contentId();
    if (auto here = std::ranges::find(turn.last, document, &std::pair<DocumentId, ContentId>::first);
        here != turn.last.end())
        here->second = step;
    else
        turn.last.emplace_back(document, step);
    m_changes.push_back(Change{m_nextId++, turn.turn, document, callerLabel, std::move(name), step, std::move(items)});
}

const AiReview::Turn *AiReview::turn(std::uint64_t id) const
{
    const auto found = std::ranges::find(m_turns, id, &Turn::id);
    return found == m_turns.end() ? nullptr : &*found;
}

const AiReview::Change *AiReview::change(std::uint64_t id) const
{
    const auto found = std::ranges::find(m_changes, id, &Change::id);
    return found == m_changes.end() ? nullptr : &*found;
}

AiReview::Change *AiReview::find(std::uint64_t id)
{
    const auto found = std::ranges::find(m_changes, id, &Change::id);
    return found == m_changes.end() ? nullptr : &*found;
}

std::vector<const AiReview::Change *> AiReview::changes(DocumentId document) const
{
    std::vector<const Change *> out;
    for (const Change &c : m_changes)
        if (c.document == document)
            out.push_back(&c);
    return out;
}

std::vector<std::pair<AiReview::Change *, std::size_t>> AiReview::waiting(DocumentId document,
                                                                         const core::Document &current, Target target)
{
    std::vector<Change *> mine;
    for (Change &c : m_changes)
        if (c.document == document)
            mine.push_back(&c);
    const Snapshot now(current);
    std::vector<std::pair<Change *, std::size_t>> out;
    for (const Live &live : liveItems(mine, now, [](const Item &item) { return !item.settled; })) {
        Change *c = mine[live.change];
        if (target.change && c->id != target.change)
            continue;
        if (target.item && *target.item != live.item)
            continue;
        if (target.line && !onLine(c->items[live.item], *target.line))
            continue;
        out.emplace_back(c, live.item);
    }
    return out;
}

std::vector<AiReview::Pending> AiReview::pending(DocumentId document, const core::Document &current) const
{
    std::vector<const Change *> mine;
    for (const Change &c : m_changes)
        if (c.document == document)
            mine.push_back(&c);
    const Snapshot now(current);
    std::vector<Pending> out;
    for (const Live &live : liveItems(mine, now, [](const Item &item) { return !item.settled; }))
        out.push_back(Pending{mine[live.change]->id, live.item, live.editedSince});
    return out;
}

std::size_t AiReview::pendingLines(DocumentId document, const core::Document &current) const
{
    std::set<std::uint64_t> lines;
    for (const Pending &p : pending(document, current)) {
        const Item &item = change(p.change)->items[p.item];
        if (item.kind == ItemKind::Line)
            lines.insert(item.line.value);
    }
    return lines.size();
}

std::optional<std::uint64_t> AiReview::turnStartingAt(DocumentId document, ContentId step) const
{
    std::set<std::uint64_t> seen;
    for (const Change &c : m_changes) {
        if (c.document != document)
            continue;
        if (seen.insert(c.turn).second && c.step == step)
            return c.turn;
    }
    return std::nullopt;
}

bool AiReview::isAiStep(DocumentId document, ContentId step) const
{
    return std::ranges::any_of(m_changes, [&](const Change &c) { return c.document == document && c.step == step; });
}

void AiReview::keep(DocumentId document, const core::Document &current, Target target)
{
    for (auto &[c, item] : waiting(document, current, target))
        c->items[item].settled = true;
}

std::vector<AiReview::Pending> AiReview::editedSince(DocumentId document, const core::Document &current,
                                                     Target target) const
{
    std::vector<Pending> out;
    for (const Pending &p : pending(document, current))
        if (p.editedSince && (!target.change || p.change == target.change) &&
            (!target.item || *target.item == p.item) &&
            (!target.line || onLine(change(p.change)->items[p.item], *target.line)))
            out.push_back(p);
    return out;
}

std::expected<bool, CommandRefusal> AiReview::undo(DocumentId document, EditSession &session, Target target,
                                                   bool force)
{
    const core::Document &current = session.document();
    std::vector<std::pair<Change *, std::size_t>> picked;
    std::set<std::pair<std::uint64_t, std::size_t>> skip;
    for (const Pending &p : editedSince(document, current, target))
        skip.emplace(p.change, p.item);
    for (const auto &[c, item] : waiting(document, current, target))
        if (force || !skip.contains({c->id, item}))
            picked.emplace_back(c, item);
    if (picked.empty())
        return false;

    const auto newestFirst = byChangeNewestFirst(picked);
    Command command;
    if (target.line) {
        const Change &c = *picked.front().first;
        command.name = "Undo " + c.caller + " change to Line " + std::to_string(rowOf(current, *target.line));
    } else if (!target.change) {
        command.name = "Undo all AI changes";
    } else {
        const Change &c = *picked.front().first;
        const Item &item = c.items[picked.front().second];
        if (!target.item)
            command.name = "Undo " + c.caller + ": " + c.name;
        else if (item.kind == ItemKind::Line && item.after)
            command.name = "Undo " + c.caller + " change to Line " + std::to_string(rowOf(current, item.line));
        else if (item.kind == ItemKind::Line)
            command.name = "Undo " + c.caller + " deletion of a Line";
        else if (item.kind == ItemKind::Style)
            command.name = "Undo " + c.caller + " change to Style " + text(item.key);
        else
            command.name = "Undo " + c.caller + " change to Script Info " + text(item.key);
    }
    command.expectedRevision = session.revision();
    command.keepGroups = false;
    command.apply = [&](core::Document &doc) { return undoItems(doc, newestFirst); };
    if (auto ran = session.run(command); !ran)
        return std::unexpected(ran.error());
    for (auto &[c, item] : picked)
        c->items[item].settled = c->items[item].undone = true;
    return true;
}

std::vector<AiReview::CheckpointDocument> AiReview::checkpoint(std::uint64_t turn, const Sessions &sessions) const
{
    std::vector<CheckpointDocument> out;
    std::vector<DocumentId> documents;
    for (const Change &c : m_changes)
        if (c.turn >= turn && std::ranges::find(documents, c.document) == documents.end())
            documents.push_back(c.document);
    for (const DocumentId document : documents) {
        const EditSession *session = sessions(document);
        if (!session)
            continue;
        std::vector<const Change *> mine;
        for (const Change &c : m_changes)
            if (c.document == document && c.turn >= turn)
                mine.push_back(&c);
        const Snapshot now(session->document());
        CheckpointDocument entry{document};
        std::set<std::uint64_t> lines, edited;
        for (const Live &live : liveItems(mine, now, [](const Item &item) { return !item.undone; })) {
            const Item &item = mine[live.change]->items[live.item];
            if (item.kind != ItemKind::Line) {
                ++entry.others;
                continue;
            }
            lines.insert(item.line.value);
            if (live.editedSince && edited.insert(item.line.value).second)
                entry.editedSince.push_back(item.line);
        }
        entry.lines = lines.size();
        if (entry.lines || entry.others)
            out.push_back(std::move(entry));
    }
    return out;
}

std::expected<std::size_t, CommandRefusal> AiReview::restore(std::uint64_t turn, const Sessions &sessions, bool force)
{
    const Turn *from = this->turn(turn);
    std::size_t restored = 0;
    std::vector<DocumentId> documents;
    for (const Change &c : m_changes)
        if (c.turn >= turn && std::ranges::find(documents, c.document) == documents.end())
            documents.push_back(c.document);
    for (const DocumentId document : documents) {
        EditSession *session = sessions(document);
        if (!session)
            continue;
        std::vector<Change *> mine;
        for (Change &c : m_changes)
            if (c.document == document && c.turn >= turn)
                mine.push_back(&c);
        const Snapshot now(session->document());
        std::vector<Live> live = liveItems(mine, now, [](const Item &item) { return !item.undone; });
        std::erase_if(live, [&](const Live &l) { return l.editedSince && !force; });
        if (live.empty())
            continue;
        std::vector<std::pair<Change *, std::size_t>> picked;
        for (const Live &l : live)
            picked.emplace_back(mine[l.change], l.item);
        const auto newestFirst = byChangeNewestFirst(picked);
        Command command;
        command.name = "Restore checkpoint before turn " + std::to_string(from ? from->number : 0);
        command.expectedRevision = session->revision();
        command.keepGroups = false;
        command.apply = [&](core::Document &doc) { return undoItems(doc, newestFirst); };
        if (auto ran = session->run(command); !ran)
            return std::unexpected(ran.error());
        for (const Live &l : live)
            mine[l.change]->items[l.item].settled = mine[l.change]->items[l.item].undone = true;
        ++restored;
    }
    return restored;
}

std::vector<DocumentId> AiReview::documents() const
{
    std::set<DocumentId> out;
    for (const Change &c : m_changes)
        out.insert(c.document);
    return {out.begin(), out.end()};
}

void AiReview::forget(DocumentId document)
{
    std::erase_if(m_changes, [&](const Change &c) { return c.document == document; });
    for (auto &[caller, open] : m_open)
        std::erase_if(open.last, [&](const auto &entry) { return entry.first == document; });
}

}
