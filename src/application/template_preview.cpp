#include "hikari/application/template_preview.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"

#include <algorithm>
#include <deque>
#include <limits>

namespace hikari::application {

namespace {

// Lua's %s
bool space(char8_t c)
{
    return c == u8' ' || c == u8'\t' || c == u8'\n' || c == u8'\v' || c == u8'\f' || c == u8'\r';
}

// string.headtail(effect):lower(): the Effect's first word.
std::u8string firstWord(const std::u8string &effect)
{
    std::u8string word(effect.begin(), std::ranges::find_if(effect, space));
    for (auto &c : word)
        if (c >= u8'A' && c <= u8'Z')
            c = static_cast<char8_t>(c - u8'A' + u8'a');
    return word;
}

// kara-templater.lua's line_key: a dialogue line as it reads back once
// saved, spaces trimmed around the actor, effect and texts.
std::string lineKey(const MacroDialogueLine &l)
{
    const auto trim = [](const std::string &s) {
        const auto first = std::ranges::find_if_not(s, [](char c) { return space(static_cast<char8_t>(c)); });
        const auto last = std::find_if_not(s.rbegin(), std::string::const_reverse_iterator(first),
                                           [](char c) { return space(static_cast<char8_t>(c)); });
        return std::string(first, last.base());
    };
    std::string key;
    for (const auto &field : {std::string(l.comment ? "1" : "0"), std::to_string(l.layer), std::to_string(l.startMs),
                              std::to_string(l.endMs), l.style, trim(l.actor), std::to_string(l.marginL),
                              std::to_string(l.marginR), std::to_string(l.marginV), trim(l.effect), trim(l.text),
                              trim(l.translation)})
        key += field + '\0';
    return key;
}

std::int64_t msOf(const core::TimeField &t)
{
    return t.value.microseconds() / 1000;
}

bool over(const core::LineRecord &l, TemplatePreviewWindow window)
{
    return msOf(l.start) <= window.toMs && msOf(l.end) > window.fromMs;
}

} // namespace

std::optional<TemplateKind> templateKindOf(const core::LineRecord &line)
{
    if (!line.comment)
        return std::nullopt;
    const std::u8string word = firstWord(line.effect);
    if (word == u8"template")
        return TemplateKind::Template;
    if (word == u8"code")
        return TemplateKind::Code;
    return std::nullopt;
}

bool isKaraokeLine(const core::LineRecord &line)
{
    return (line.effect.empty() && !line.comment) || line.effect.find(u8"karaoke") != std::u8string::npos ||
           line.effect.find(u8"Karaoke") != std::u8string::npos;
}

bool isGeneratedLine(const core::LineRecord &line)
{
    return !line.comment && line.effect == u8"fx";
}

std::optional<core::LineId> templatePreviewSource(const core::Document &doc, std::optional<core::LineId> active,
                                                  std::optional<core::LineId> last)
{
    const auto lines = doc.lines();
    const auto find = [&](std::optional<core::LineId> id) -> const core::LineRecord * {
        if (!id)
            return nullptr;
        const auto it = std::ranges::find_if(lines, [&](const core::LineRecord *l) { return l->id == *id; });
        return it == lines.end() ? nullptr : *it;
    };
    if (const auto *l = find(active)) {
        if (isKaraokeLine(*l))
            return l->id;
        if (const auto *source = find(l->generatedFrom); source && isKaraokeLine(*source))
            return source->id;
    }
    if (const auto *l = find(last); l && isKaraokeLine(*l))
        return l->id;
    const auto first = std::ranges::find_if(lines, [](const core::LineRecord *l) { return isKaraokeLine(*l); });
    return first == lines.end() ? std::nullopt : std::optional((*first)->id);
}

std::optional<MacroSnapshot> templateOwnersSnapshot(const core::Document &doc)
{
    core::Document plain = doc;
    plain.removeLinesIf([](const core::LineRecord &l) { return isGeneratedLine(l); });
    Selection all;
    for (const auto *l : plain.lines())
        if (isKaraokeLine(*l)) { // a `template line karaoke` too
            all.selected.insert(l->id);
            if (!all.active)
                all.active = l->id;
        }
    if (all.selected.empty())
        return std::nullopt;
    return macroSnapshotOf(plain, all);
}

std::map<core::LineId, core::LineId> templateOwners(const core::Document &doc, const MacroResult &result)
{
    std::map<std::string, std::deque<core::LineId>> free;
    for (const auto *l : doc.lines())
        if (isGeneratedLine(*l) && l->generatedFrom.value == 0)
            free[lineKey(macroLineOf(*l))].push_back(l->id);
    std::map<std::uint64_t, std::vector<const MacroDialogueLine *>> made;
    for (const auto &d : result.dialogues)
        if (d.id == 0 && d.generatedFrom != 0)
            made[d.generatedFrom].push_back(&d);
    // A karaoke Line whose Lines are marked (selected Lines applied since)
    // takes none of the unmarked ones, as KE1's run finds them.
    std::set<core::LineId> marked;
    for (const auto *l : doc.lines())
        if (isGeneratedLine(*l) && l->generatedFrom.value != 0)
            marked.insert(l->generatedFrom);
    // One the file holds none of the Lines of made none when it was written
    // (a template since gives it Lines): it takes none either.
    std::set<std::string> held;
    for (const auto &[key, list] : free)
        held.insert(key);
    std::map<core::LineId, core::LineId> owners;
    for (const auto *l : doc.lines()) {
        const auto lines = made.find(l->id.value);
        if (lines == made.end() || marked.contains(l->id) ||
            std::ranges::none_of(lines->second, [&](const auto *d) { return held.contains(lineKey(*d)); }))
            continue;
        for (const auto *d : lines->second) {
            auto &list = free[lineKey(*d)];
            if (list.empty())
                return {}; // the templates give what the file does not hold
            owners[list.front()] = l->id;
            list.pop_front();
        }
    }
    return owners;
}

MacroDialogueLine templatePlaceholder()
{
    core::LineRecord l;
    l.comment = true;
    return macroLineOf(l);
}

std::optional<MacroSnapshot> templatePreviewSnapshot(const core::Document &doc, core::LineId source,
                                                     const std::map<core::LineId, std::u8string> &texts)
{
    MacroSnapshot s = macroSnapshotOf(doc, Selection{});
    s.dialogues.clear();
    const int offset = static_cast<int>(s.info.size() + s.styles.size()) + 1;
    // A template with its text not yet in the Document (a `template line
    // karaoke` source too).
    const auto withText = [&](const core::LineRecord &l) {
        const auto text = texts.find(l.id);
        if (text == texts.end())
            return macroLineOf(l);
        core::LineRecord edited = l;
        // In TLMode the script reads the translation (macroLineOf).
        (edited.translation.empty() ? edited.text : edited.translation) = text->second;
        return macroLineOf(edited);
    };
    bool found = false;
    for (const auto *l : doc.lines()) {
        if (l->id == source) {
            found = true;
            s.active = offset + static_cast<int>(s.dialogues.size());
            s.selected = {s.active};
            s.dialogues.push_back(withText(*l));
        } else if (templateKindOf(*l)) {
            s.dialogues.push_back(withText(*l));
        } else if (!found && !isGeneratedLine(*l)) {
            s.dialogues.push_back(templatePlaceholder()); // $li as a whole run gives it
        }
    }
    if (!found)
        return std::nullopt;
    return s;
}

std::vector<core::LineRecord> templatePreviewLines(const MacroResult &result, core::LineId source)
{
    std::vector<core::LineRecord> out;
    for (const auto &d : result.dialogues)
        if (d.id == 0 && d.generatedFrom == source.value)
            out.push_back(lineOfMacro(d));
    return out;
}

std::optional<core::LineId> templatePreviewLineOf(const MacroSnapshot &snapshot, int number)
{
    if (number < 1 || static_cast<std::size_t>(number) > snapshot.dialogues.size())
        return std::nullopt;
    const auto id = snapshot.dialogues[static_cast<std::size_t>(number) - 1].id;
    return id == 0 ? std::nullopt : std::optional(core::LineId{id});
}

TemplatePreviewWindow templatePreviewWindow(std::int64_t timeMs, bool playing, std::int64_t spanStart,
                                            std::int64_t spanEnd, const std::vector<std::int64_t> &ends)
{
    if (!playing)
        return {timeMs, timeMs};
    if (timeMs >= spanStart && timeMs < spanEnd)
        return {spanStart, spanEnd};
    // The Lines over after timeMs are those over after the last end passed.
    const auto passed = std::ranges::upper_bound(ends, timeMs);
    return {passed == ends.begin() ? std::numeric_limits<std::int64_t>::min() : *std::prev(passed),
            std::numeric_limits<std::int64_t>::max()};
}

std::vector<MacroStyleLine> templatePreviewStyles(const MacroSnapshot &snapshot, const MacroResult &result)
{
    std::vector<MacroStyleLine> out;
    for (const auto &style : result.styles)
        if (std::ranges::find(snapshot.styles, style) == snapshot.styles.end())
            out.push_back(style);
    return out;
}

core::Document templatePreviewDocument(const core::Document &doc, core::LineId source,
                                       const std::vector<core::LineRecord> &preview,
                                       const std::set<core::LineId> &replaced, TemplatePreviewWindow window,
                                       const std::map<core::LineId, core::LineId> &owners,
                                       const std::vector<MacroStyleLine> &styles)
{
    core::Document out = doc;
    for (const auto &style : styles) {
        if (style.fields.empty())
            continue;
        std::vector<std::u8string> fields;
        for (const auto &f : style.fields)
            fields.emplace_back(f.begin(), f.end());
        std::optional<std::size_t> index;
        std::size_t i = 0;
        for (const auto &section : out.sections())
            for (const auto &record : section.records)
                if (const auto *st = std::get_if<core::StyleRecord>(&record)) {
                    if (!index && st->name == fields.front())
                        index = i;
                    ++i;
                }
        if (index)
            out.editStyle(*index, std::move(fields));
        else
            out.appendStyle(std::move(fields));
    }
    // The source too when its templates make Lines: Apply turns it into a
    // Comment then.
    const bool sourceGoes = !preview.empty();
    // A marked Line edited out of `fx` stays: Apply replaces only generated
    // ones.
    const auto replacedHere = [&](const core::LineRecord &l) {
        return (l.generatedFrom == source && isGeneratedLine(l)) || replaced.contains(l.id);
    };
    const auto goes = [&](const core::LineRecord &l) {
        return l.comment || replacedHere(l) || !over(l, window) || (sourceGoes && l.id == source);
    };
    // The preview stands where the Lines it replaces stood, as Apply writes
    // it (read order decides collisions and stacking): before the first Line
    // after them that stays. With none, before the first Line of a karaoke
    // Line after the source (Apply writes the Lines in karaoke Line order;
    // an unmarked Line's karaoke Line from `owners`); at the end when there
    // is none either.
    std::map<core::LineId, std::size_t> place;
    for (const auto *l : doc.lines())
        place.emplace(l->id, place.size());
    const bool replacing = std::ranges::any_of(doc.lines(), [&](const core::LineRecord *l) { return replacedHere(*l); });
    const auto later = [&](const core::LineRecord &l) {
        core::LineId owner = l.generatedFrom;
        if (const auto known = owners.find(l.id); owner.value == 0 && known != owners.end())
            owner = known->second;
        const auto from = place.find(owner);
        return owner.value != 0 && from != place.end() && place.contains(source) && from->second > place.at(source);
    };
    std::optional<core::LineId> before;
    bool past = false;
    for (const auto *l : doc.lines()) {
        if (replacing ? replacedHere(*l) : later(*l))
            past = true;
        if (past && !replacedHere(*l) && !goes(*l)) {
            before = l->id;
            break;
        }
    }
    out.removeLinesIf(goes);
    for (const auto &l : preview)
        if (!l.comment && over(l, window)) {
            if (before)
                out.insertLineBefore(*before, l, core::ChangeMark::Kept);
            else
                out.appendLine(l, core::ChangeMark::Kept);
        }
    return out;
}

bool isPreviewService(HostService service)
{
    switch (service) {
    case HostService::TextExtents:
    case HostService::FrameFromMs:
    case HostService::MsFromFrame:
    case HostService::VideoSize:
    case HostService::Keyframes:
    case HostService::ProjectProperties:
    case HostService::AudioSelection:
    case HostService::DecodePath:
    case HostService::FileName:
    case HostService::Gettext:
        return true;
    default:
        return false;
    }
}

} // namespace hikari::application
