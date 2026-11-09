#include "hikari/application/qc_notes.h"

#include "hikari/core/ass_save.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <set>

namespace hikari::application {

namespace {

using u8 = std::u8string;
using u8v = std::u8string_view;

constexpr u8v kKeyPrefix = u8"Hikari QC ";
constexpr std::int64_t kNearestStartLimitMs = 15000;

bool isDigit(char8_t c) { return c >= u8'0' && c <= u8'9'; }
bool isBlankChar(char8_t c) { return c == u8' ' || c == u8'\t' || c == u8'\r'; }

u8v trimmed(u8v s)
{
    while (!s.empty() && isBlankChar(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isBlankChar(s.back()))
        s.remove_suffix(1);
    return s;
}

bool blank(u8v s) { return trimmed(s).empty(); }

std::optional<std::int64_t> number(u8v digits)
{
    std::int64_t value = 0;
    const char *first = reinterpret_cast<const char *>(digits.data());
    const auto [end, error] = std::from_chars(first, first + digits.size(), value);
    if (digits.empty() || error != std::errc{} || end != first + digits.size())
        return std::nullopt;
    return value;
}

u8v takeDigits(u8v &s)
{
    std::size_t n = 0;
    while (n < s.size() && isDigit(s[n]))
        ++n;
    const u8v digits = s.substr(0, n);
    s.remove_prefix(n);
    return digits;
}

std::optional<std::int64_t> takeTime(u8v &s)
{
    u8v rest = s;
    std::vector<std::int64_t> parts;
    while (parts.size() < 3) {
        u8v after = rest;
        if (!parts.empty()) {
            if (after.empty() || after.front() != u8':')
                break;
            after.remove_prefix(1);
        }
        const auto part = number(takeDigits(after));
        if (!part)
            break;
        parts.push_back(*part);
        rest = after;
    }
    if (parts.size() < 2)
        return std::nullopt;
    std::int64_t fraction = 0;
    if (!rest.empty() && rest.front() == u8'.' && rest.size() > 1 && isDigit(rest[1])) {
        rest.remove_prefix(1);
        const u8v digits = takeDigits(rest);
        u8 ms(digits.substr(0, 3));
        ms.resize(3, u8'0');
        fraction = *number(ms);
    }
    if (parts.size() == 2)
        parts.insert(parts.begin(), 0);
    s = rest;
    return ((parts[0] * 60 + parts[1]) * 60 + parts[2]) * 1000 + fraction;
}

u8 sectionName(u8v name)
{
    u8 out(trimmed(name));
    std::ranges::replace(out, u8',', u8' ');
    return out.empty() ? u8(kQcGeneralSection) : out;
}

u8v noteText(u8v rest)
{
    for (;;) {
        rest = trimmed(rest);
        if (!rest.empty() && (rest.front() == u8'-' || rest.front() == u8':' || rest.front() == u8'.'))
            rest.remove_prefix(1);
        else if (rest.starts_with(u8"–") || rest.starts_with(u8"—"))
            rest.remove_prefix(u8v(u8"–").size());
        else
            return rest;
    }
}

std::vector<u8v> splitLines(u8v text)
{
    std::vector<u8v> lines;
    while (!text.empty()) {
        const auto end = text.find(u8'\n');
        u8v line = text.substr(0, end);
        if (!line.empty() && line.back() == u8'\r')
            line.remove_suffix(1);
        lines.push_back(line);
        if (end == u8v::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return lines;
}

bool isMpvQc(const std::vector<u8v> &lines)
{
    return std::ranges::any_of(lines, [](u8v l) { return l.starts_with(u8"generator") && l.find(u8"mpvQC") != u8v::npos; });
}

std::vector<QcNote> parseMpvQc(const std::vector<u8v> &lines)
{
    std::vector<QcNote> notes;
    for (u8v line : lines) {
        if (!line.starts_with(u8'['))
            continue;
        line.remove_prefix(1);
        const auto ms = takeTime(line);
        if (!ms || !line.starts_with(u8"] ["))
            continue;
        line.remove_prefix(3);
        const auto close = line.find(u8']');
        if (close == u8v::npos)
            continue;
        notes.push_back({*ms, sectionName(line.substr(0, close)), false, u8(trimmed(line.substr(close + 1)))});
    }
    return notes;
}

std::vector<QcNote> parseNoteBrowser(const std::vector<u8v> &lines)
{
    std::vector<QcNote> notes;
    u8 section(kQcGeneralSection);
    std::optional<QcNote> current;
    bool previousIndented = false;
    bool blankKept = false;
    const auto finish = [&] {
        if (current)
            notes.push_back(std::move(*current));
        current.reset();
    };
    for (const u8v line : lines) {
        u8v rest = line;
        const auto ms = !line.empty() && isDigit(line.front()) ? takeTime(rest) : std::nullopt;
        if (ms) {
            finish();
            current = QcNote{*ms, section, false, u8(noteText(rest))};
            previousIndented = false;
            blankKept = false;
            continue;
        }
        if (line.starts_with(u8'[')) {
            finish();
            if (const auto close = line.find(u8']'); close == line.size() - 1)
                section = sectionName(line.substr(1, close - 1));
            continue;
        }
        if (blank(line)) {
            if (current && previousIndented)
                blankKept = true;
            else
                finish();
            continue;
        }
        if (!current)
            continue;
        if (blankKept)
            current->text += u8"\\N";
        current->text += u8"\\N";
        current->text += trimmed(line);
        previousIndented = isBlankChar(line.front());
        blankKept = false;
    }
    finish();
    return notes;
}

bool isNoteKey(u8v key)
{
    if (!key.starts_with(kKeyPrefix))
        return false;
    key.remove_prefix(kKeyPrefix.size());
    return !key.empty() && std::ranges::all_of(key, isDigit);
}

std::optional<QcNote> noteFromValue(u8v value)
{
    const auto field = [&value]() -> std::optional<u8v> {
        const auto comma = value.find(u8',');
        if (comma == u8v::npos)
            return std::nullopt;
        const u8v f = value.substr(0, comma);
        value.remove_prefix(comma + 1);
        return f;
    };
    const auto time = field();
    const auto section = field();
    const auto state = field();
    if (!time || !section || !state || (*state != u8"open" && *state != u8"done"))
        return std::nullopt;
    u8v t = trimmed(*time);
    const auto ms = takeTime(t);
    if (!ms || !t.empty())
        return std::nullopt;
    return QcNote{*ms, sectionName(*section), *state == u8"done", u8(value)};
}

u8 valueOf(const QcNote &note)
{
    return core::legacy::assTimeText(note.ms) + u8',' + sectionName(note.section) + u8',' +
           (note.done ? u8"done" : u8"open") + u8',' + note.text;
}

void sortByTime(std::vector<QcNote> &notes)
{
    std::ranges::stable_sort(notes, {}, &QcNote::ms);
}

std::vector<const core::PropertyRecord *> scriptInfo(const core::Document &document)
{
    std::vector<const core::PropertyRecord *> out;
    for (const auto &section : document.sections())
        if (section.kind == core::SectionKind::ScriptInfo)
            for (const auto &record : section.records)
                if (const auto *p = std::get_if<core::PropertyRecord>(&record))
                    out.push_back(p);
    return out;
}

bool sameNote(const QcNote &a, const QcNote &b)
{
    return a.ms == b.ms && a.section == b.section && a.text == b.text;
}

}

std::vector<QcNote> parseQcNoteText(std::u8string_view text)
{
    const auto lines = splitLines(text);
    auto notes = isMpvQc(lines) ? parseMpvQc(lines) : parseNoteBrowser(lines);
    sortByTime(notes);
    return notes;
}

std::u8string formatQcNoteText(std::span<const QcNote> notes)
{
    std::vector<u8> sections;
    for (const auto &note : notes)
        if (std::ranges::find(sections, sectionName(note.section)) == sections.end())
            sections.push_back(sectionName(note.section));
    std::vector<QcNote> ordered(notes.begin(), notes.end());
    sortByTime(ordered);
    u8 out;
    for (const auto &section : sections) {
        if (!out.empty())
            out += u8'\n';
        out += u8'[' + section + u8"]\n";
        for (const auto &note : ordered) {
            if (sectionName(note.section) != section)
                continue;
            out += qcNoteTimeText(note.ms) + u8" - ";
            u8v text = note.text;
            for (bool first = true;; first = false) {
                const auto br = text.find(u8"\\N");
                const u8v part = text.substr(0, br);
                if (!first)
                    out += part.empty() ? u8"" : u8"    ";
                out += part;
                out += u8'\n';
                if (br == u8v::npos)
                    break;
                text.remove_prefix(br + 2);
            }
        }
    }
    return out;
}

QcNote normalisedQcNote(QcNote note)
{
    note.ms -= note.ms % 10;
    note.section = sectionName(note.section);
    u8 text;
    for (const u8v line : splitLines(note.text)) {
        if (!text.empty())
            text += u8"\\N";
        text += line;
    }
    note.text = u8(trimmed(text));
    return note;
}

std::vector<QcNote> documentQcNotes(const core::Document &document)
{
    std::vector<QcNote> notes;
    for (const auto *p : scriptInfo(document))
        if (isNoteKey(p->key))
            if (auto note = noteFromValue(p->value))
                notes.push_back(std::move(*note));
    sortByTime(notes);
    return notes;
}

bool setDocumentQcNotes(core::Document &document, std::vector<QcNote> notes)
{
    auto properties = scriptInfo(document);
    if (properties.empty() && !notes.empty()) {
        if (!document.setScriptInfo(u8"ScriptType", u8"v4.00+"))
            return false;
        properties = scriptInfo(document);
    }
    std::set<std::int64_t> taken;
    std::map<u8, std::vector<std::size_t>> existing;
    std::vector<core::Document::PropertySlot> slots;
    for (std::size_t i = 0; i < properties.size(); ++i) {
        const auto *p = properties[i];
        if (isNoteKey(p->key) && noteFromValue(p->value)) {
            existing[p->key + u8':' + p->value].push_back(i);
            continue;
        }
        if (isNoteKey(p->key))
            taken.insert(*number(u8v(p->key).substr(kKeyPrefix.size())));
        slots.push_back({i, std::nullopt});
    }
    if (existing.empty() && notes.empty())
        return true;
    for (auto &note : notes)
        note = normalisedQcNote(std::move(note));
    sortByTime(notes);
    std::int64_t n = 0;
    for (const auto &note : notes) {
        do
            ++n;
        while (taken.contains(n));
        const u8 key = u8(kKeyPrefix) + reinterpret_cast<const char8_t *>(std::to_string(n).c_str());
        const u8 value = valueOf(note);
        auto same = existing.find(key + u8':' + value);
        if (same != existing.end() && !same->second.empty()) {
            slots.push_back({same->second.back(), std::nullopt});
            same->second.pop_back();
        } else {
            slots.push_back({std::nullopt, std::pair{key, value}});
        }
    }
    return document.rearrangeScriptInfo(slots);
}

QcImport importQcNotes(std::vector<QcNote> current, std::span<const QcNote> incoming, bool replace)
{
    QcImport out;
    if (replace)
        current.clear();
    const std::size_t before = current.size();
    for (QcNote note : incoming) {
        note = normalisedQcNote(std::move(note));
        const auto first = current.begin(), last = current.begin() + static_cast<std::ptrdiff_t>(before);
        if (std::any_of(first, last, [&](const QcNote &n) { return sameNote(n, note); })) {
            ++out.skipped;
            continue;
        }
        current.push_back(std::move(note));
        ++out.added;
    }
    sortByTime(current);
    out.notes = std::move(current);
    return out;
}

std::optional<core::LineId> qcNoteLine(std::span<const core::LineRecord *const> lines, std::int64_t ms)
{
    const core::LineRecord *shown = nullptr;
    const core::LineRecord *nearest = nullptr;
    std::int64_t nearestDistance = kNearestStartLimitMs;
    for (const auto *line : lines) {
        if (line->comment || line->unparsed)
            continue;
        const std::int64_t start = line->start.value.microseconds() / 1000;
        const std::int64_t end = line->end.value.microseconds() / 1000;
        if (start <= ms && ms < end && (!shown || start > shown->start.value.microseconds() / 1000))
            shown = line;
        if (const std::int64_t distance = std::abs(start - ms); distance < nearestDistance) {
            nearest = line;
            nearestDistance = distance;
        }
    }
    if (const auto *line = shown ? shown : nearest)
        return line->id;
    return std::nullopt;
}

std::u8string qcNoteTimeText(std::int64_t ms)
{
    u8 text = core::legacy::assTimeText(ms);
    if (ms % 1000 < 10)
        text.resize(text.size() - 3);
    return text;
}

}
