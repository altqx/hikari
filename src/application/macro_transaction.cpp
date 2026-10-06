#include "hikari/application/macro_transaction.h"

#include "hikari/core/ass_save.h"

#include <map>
#include <set>

namespace hikari::application {

namespace {

std::string narrow(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::u8string wide(const std::string &s)
{
    return std::u8string(s.begin(), s.end());
}

core::TimeField timeOf(std::int64_t ms)
{
    return core::TimeField{core::legacy::assTimeText(ms), core::DocumentTime(ms * 1000)};
}

core::IntField intOf(std::int64_t v)
{
    return core::IntField{wide(std::to_string(v)), v};
}

MacroDialogueLine macroLineOf(const core::LineRecord &l)
{
    MacroDialogueLine d;
    d.id = l.id.value;
    d.comment = l.comment;
    d.layer = static_cast<int>(l.layer.value);
    d.startMs = l.start.value.microseconds() / 1000;
    d.endMs = l.end.value.microseconds() / 1000;
    d.style = narrow(l.style);
    d.actor = narrow(l.actor);
    d.marginL = static_cast<int>(l.marginLeft.value);
    d.marginR = static_cast<int>(l.marginRight.value);
    d.marginV = static_cast<int>(l.marginVertical.value);
    d.effect = narrow(l.effect);
    // Legacy AutoToFile: in TLMode the script's "text" is the translation and
    // "text_translation" the original.
    if (!l.translation.empty()) {
        d.text = narrow(l.translation);
        d.translation = narrow(l.text);
    } else {
        d.text = narrow(l.text);
    }
    d.raw = narrow(core::legacy::assLineText(l));
    return d;
}

// Writes the script-visible fields into a Line (legacy LuaToLine).
void assign(core::LineRecord &l, const MacroDialogueLine &d)
{
    l.comment = d.comment;
    l.layer = intOf(d.layer);
    l.start = timeOf(d.startMs);
    l.end = timeOf(d.endMs);
    l.style = wide(d.style);
    l.actor = wide(d.actor);
    l.marginLeft = intOf(d.marginL);
    l.marginRight = intOf(d.marginR);
    l.marginVertical = intOf(d.marginV);
    l.effect = wide(d.effect);
    if (d.translation.empty()) {
        l.text = wide(d.text);
        l.translation.clear();
    } else {
        l.translation = wide(d.text);
        l.text = wide(d.translation);
    }
    l.unparsed = false;
}

// Pairs each staged entry with the next equal one of the snapshot, in order,
// so unchanged Styles and Script Info properties keep their records and
// bytes; the others are written from their fields.
template <class T>
std::vector<std::optional<std::size_t>> matchInOrder(const std::vector<T> &before, const std::vector<T> &after)
{
    std::vector<std::optional<std::size_t>> from;
    std::size_t next = 0;
    for (const auto &entry : after) {
        std::optional<std::size_t> found;
        for (std::size_t k = next; k < before.size() && !found; ++k)
            if (before[k] == entry)
                found = k;
        if (found)
            next = *found + 1;
        from.push_back(found);
    }
    return from;
}

// S4: the macro's Script Info and Styles, applied in the macro's one step.
// Legacy AutoToFile edits the file's SInfo and Styles lists in place
// (AutomationToFile.cpp: index write 611-625, delete 713-747, deleterange
// 777-796, append 822-830, insert 863-889); the staged lists are those lists.
bool applyInfoAndStyles(core::Document &doc, const MacroSnapshot &snapshot, const MacroResult &result)
{
    if (result.info != snapshot.info) {
        const auto from = matchInOrder(snapshot.info, result.info);
        std::vector<core::Document::PropertySlot> slots;
        for (std::size_t i = 0; i < result.info.size(); ++i) {
            core::Document::PropertySlot slot;
            slot.from = from[i];
            if (!from[i])
                slot.property = std::pair{wide(result.info[i].key), wide(result.info[i].value)};
            slots.push_back(std::move(slot));
        }
        if (!doc.rearrangeScriptInfo(slots))
            return false;
    }
    if (result.styles != snapshot.styles) {
        const auto from = matchInOrder(snapshot.styles, result.styles);
        std::vector<core::Document::StyleSlot> slots;
        for (std::size_t i = 0; i < result.styles.size(); ++i) {
            core::Document::StyleSlot slot;
            slot.from = from[i];
            if (!from[i]) {
                std::vector<std::u8string> fields;
                for (const auto &f : result.styles[i].fields)
                    fields.push_back(wide(f));
                if (fields.empty())
                    return false;
                slot.fields = std::move(fields);
            }
            slots.push_back(std::move(slot));
        }
        if (!doc.rearrangeStyles(slots))
            return false;
    }
    return true;
}

bool sameFields(const MacroDialogueLine &a, const MacroDialogueLine &b)
{
    return a.comment == b.comment && a.layer == b.layer && a.startMs == b.startMs && a.endMs == b.endMs &&
           a.style == b.style && a.actor == b.actor && a.marginL == b.marginL && a.marginR == b.marginR &&
           a.marginV == b.marginV && a.effect == b.effect && a.text == b.text && a.translation == b.translation;
}

} // namespace

std::expected<MacroSnapshot, CommandRefusal> snapshotForMacro(EditSession &session)
{
    if (session.draftLine() && !session.commitDraft())
        return std::unexpected(CommandRefusal::InvalidDraft);
    const core::Document &doc = session.document();
    MacroSnapshot s;
    s.revision = session.revision();
    for (const auto &section : doc.sections()) {
        for (const auto &record : section.records) {
            if (section.kind == core::SectionKind::ScriptInfo) {
                if (const auto *p = std::get_if<core::PropertyRecord>(&record))
                    s.info.push_back({narrow(p->key), narrow(p->value)});
            } else if (section.kind == core::SectionKind::Styles) {
                if (const auto *st = std::get_if<core::StyleRecord>(&record)) {
                    MacroStyleLine line;
                    for (const auto &f : st->fields)
                        line.fields.push_back(narrow(f));
                    // Legacy Styles::parseStyle: the name is the text after "Style: ".
                    if (!line.fields.empty())
                        line.fields.front() = narrow(st->name);
                    s.styles.push_back(std::move(line));
                }
            }
        }
    }
    for (const auto *l : doc.lines())
        s.dialogues.push_back(macroLineOf(*l));
    const int offset = static_cast<int>(s.info.size() + s.styles.size()) + 1;
    const Selection &sel = session.selection();
    for (std::size_t i = 0; i < s.dialogues.size(); ++i) {
        const core::LineId id{s.dialogues[i].id};
        if (sel.selected.contains(id))
            s.selected.push_back(offset + static_cast<int>(i));
        if (sel.active == id)
            s.active = offset + static_cast<int>(i);
    }
    return s;
}

std::expected<void, MacroApplyFailure> applyMacroResult(EditSession &session, const MacroSnapshot &snapshot,
                                                        const MacroResult &result, const std::string &name)
{
    // S4-validation-edits: validation answered false, so whatever it staged
    // is dropped and the Document stays as it was.
    if (!result.valid)
        return std::unexpected(MacroApplyFailure{MacroApplyError::Refused, CommandRefusal::Invalid});
    std::map<std::uint64_t, const MacroDialogueLine *> before;
    for (const auto &d : snapshot.dialogues)
        before[d.id] = &d;
    const bool headerChanged = result.info != snapshot.info || result.styles != snapshot.styles;
    bool changed = headerChanged || result.dialogues.size() != snapshot.dialogues.size();
    std::set<std::uint64_t> kept;
    for (std::size_t i = 0; i < result.dialogues.size(); ++i) {
        const auto &d = result.dialogues[i];
        if (d.id == 0 || !before.contains(d.id) || kept.contains(d.id)) {
            changed = true;
            continue;
        }
        kept.insert(d.id);
        if (!sameFields(d, *before[d.id]) || snapshot.dialogues[i].id != d.id)
            changed = true;
    }

    if (changed) {
        Command command;
        command.keepGroups = false; // G56 approves no change to the macro contract
        command.name = "Automation: " + name;
        command.expectedRevision = snapshot.revision;
        for (const auto &d : snapshot.dialogues)
            command.touches.insert(core::LineId{d.id});
        command.apply = [&](core::Document &doc) {
            if (headerChanged && !applyInfoAndStyles(doc, snapshot, result))
                return false;
            // Lines the macro removed.
            for (const auto &d : snapshot.dialogues)
                if (!kept.contains(d.id) && !doc.removeLine(core::LineId{d.id}))
                    return false;
            // Rebuild the order: each staged line after the previous one.
            std::optional<core::LineId> previous;
            for (const auto &d : result.dialogues) {
                const bool existing = d.id != 0 && kept.contains(d.id);
                core::LineId id{d.id};
                if (existing) {
                    const auto *old = before[d.id];
                    if (!sameFields(d, *old) && !doc.editLine(id, [&](core::LineRecord &l) { assign(l, d); }))
                        return false;
                    // A line staged out of order moves: remove and re-insert it.
                    const auto lines = doc.lines();
                    std::size_t at = 0, prevAt = 0;
                    for (std::size_t k = 0; k < lines.size(); ++k) {
                        if (lines[k]->id == id)
                            at = k;
                        if (previous && lines[k]->id == *previous)
                            prevAt = k;
                    }
                    const bool inPlace = previous ? at == prevAt + 1 : at == 0;
                    if (!inPlace) {
                        // Moves keep the Line's identity and source bytes.
                        const std::size_t target = previous ? prevAt + 1 : 0;
                        const std::optional<core::LineId> before =
                            target < lines.size() ? std::optional(lines[target]->id) : std::nullopt;
                        if (!doc.moveLine(id, before))
                            return false;
                    }
                } else {
                    core::LineRecord fresh;
                    assign(fresh, d);
                    const auto newId = previous ? doc.insertLineAfter(*previous, fresh)
                                                : (doc.lines().empty() ? doc.appendLine(fresh)
                                                                       : doc.insertLineBefore(doc.lines().front()->id, fresh));
                    if (!newId)
                        return false;
                    id = *newId;
                }
                previous = id;
            }
            return true;
        };
        if (auto ran = session.run(command); !ran)
            return std::unexpected(MacroApplyFailure{MacroApplyError::Refused, ran.error()});
    } else if (session.revision() != snapshot.revision) {
        return std::unexpected(MacroApplyFailure{MacroApplyError::Refused, CommandRefusal::StaleRevision});
    }

    // The returned selection, by the legacy rules: an active row in range
    // replaces the selection; selected rows stop at the first out of range;
    // the first selected becomes active when none was given. The rows are
    // read against the lists the macro left (legacy recounts SInfoSize and
    // StylesSize after the run, Automation.cpp:1010).
    if (result.selected || result.active) {
        const int offset = static_cast<int>(result.info.size() + result.styles.size()) + 1;
        const auto lines = session.document().lines();
        auto lineAt = [&](int scriptIndex) -> std::optional<core::LineId> {
            const int i = scriptIndex - offset;
            if (i < 0 || i >= static_cast<int>(lines.size()))
                return std::nullopt;
            return lines[static_cast<std::size_t>(i)]->id;
        };
        Selection selection = session.selection();
        std::optional<core::LineId> active;
        if (result.active)
            if ((active = lineAt(*result.active)))
                selection.selected.clear();
        if (result.selected) {
            selection.selected.clear();
            for (int index : *result.selected) {
                const auto id = lineAt(index);
                if (!id)
                    break;
                if (!active)
                    active = id;
                selection.selected.insert(*id);
            }
        }
        if (active)
            selection.active = active;
        session.setSelection(std::move(selection));
    }
    return {};
}

} // namespace hikari::application
