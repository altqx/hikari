#include "hikari/application/template_preview.h"

#include "hikari/application/edit_session.h"
#include "hikari/application/macro_transaction.h"

#include <algorithm>
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

std::optional<MacroSnapshot> templatePreviewSnapshot(const core::Document &doc, core::LineId source,
                                                     const std::map<core::LineId, std::u8string> &texts)
{
    MacroSnapshot s = macroSnapshotOf(doc, Selection{});
    s.dialogues.clear();
    const int offset = static_cast<int>(s.info.size() + s.styles.size()) + 1;
    bool found = false;
    for (const auto *l : doc.lines()) {
        if (l->id == source) {
            found = true;
            s.active = offset + static_cast<int>(s.dialogues.size());
            s.selected = {s.active};
            s.dialogues.push_back(macroLineOf(*l));
        } else if (templateKindOf(*l)) {
            if (const auto text = texts.find(l->id); text != texts.end()) {
                core::LineRecord edited = *l;
                edited.text = text->second;
                s.dialogues.push_back(macroLineOf(edited));
            } else {
                s.dialogues.push_back(macroLineOf(*l));
            }
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
                                            std::int64_t spanEnd)
{
    if (!playing)
        return {timeMs, timeMs};
    if (timeMs >= spanStart && timeMs < spanEnd)
        return {spanStart, spanEnd};
    return {timeMs, std::numeric_limits<std::int64_t>::max()};
}

core::Document templatePreviewDocument(const core::Document &doc, core::LineId source,
                                       const std::vector<core::LineRecord> &preview,
                                       const std::set<core::LineId> &replaced, TemplatePreviewWindow window)
{
    core::Document out = doc;
    // The source too when its templates make Lines: Apply turns it into a
    // Comment then.
    const bool sourceGoes = !preview.empty();
    out.removeLinesIf([&](const core::LineRecord &l) {
        return l.comment || l.generatedFrom == source || replaced.contains(l.id) || !over(l, window) ||
               (sourceGoes && l.id == source);
    });
    for (const auto &l : preview)
        if (!l.comment && over(l, window))
            out.appendLine(l, core::ChangeMark::Kept);
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
