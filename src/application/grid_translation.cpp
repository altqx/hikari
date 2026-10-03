#include "hikari/application/grid_translation.h"

#include "hikari/core/style.h"

#include <cstdio>
#include <optional>
#include <set>

namespace hikari::application {

namespace {

using u8 = std::u8string;
using u8v = std::u8string_view;

u8 hexColour(const core::Colour &c)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "&H%02X%02X%02X%02X", static_cast<unsigned>(c.a & 0xFF), static_cast<unsigned>(c.b & 0xFF),
                  static_cast<unsigned>(c.g & 0xFF), static_cast<unsigned>(c.r & 0xFF));
    return u8(reinterpret_cast<const char8_t *>(buf));
}

// Styles::GetRaw's fields, name first.
std::vector<u8> styleFields(const core::StyleValues &s)
{
    const auto flag = [](bool v) { return v ? u8(u8"-1") : u8(u8"0"); };
    return {s.name,         s.fontname,       s.fontsize,          hexColour(s.primary), hexColour(s.secondary),
            hexColour(s.outline), hexColour(s.back), flag(s.bold), flag(s.italic),   flag(s.underline),
            flag(s.strikeOut), s.scaleX,       s.scaleY,            s.spacing,            s.angle,
            s.borderStyle ? u8(u8"3") : u8(u8"1"), s.outlineWidth, s.shadow, s.alignment, s.marginLeft,
            s.marginRight,  s.marginVertical, s.encoding};
}

// wxString::Trim(): trailing whitespace.
u8 trimRight(u8v s)
{
    while (!s.empty() && (s.back() == u8' ' || s.back() == u8'\t' || s.back() == u8'\r' || s.back() == u8'\n'))
        s.remove_suffix(1);
    return u8(s);
}

// Legacy IsNumber: digits only (an empty text counts).
bool isNumber(u8v s)
{
    for (const char8_t c : s)
        if (c < u8'0' || c > u8'9')
            return false;
    return true;
}

// wxStringTokenizer(text, "\n", wxTOKEN_STRTOK).
std::vector<u8v> tokens(u8v text)
{
    std::vector<u8v> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto j = std::min(text.find(u8'\n', i), text.size());
        if (j > i)
            out.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// The entries OnPasteTextTl reads, as Dialogue raw text.
std::vector<u8> pastedEntries(u8v text, u8v extension)
{
    std::vector<u8> out;
    const auto all = tokens(text);
    if (extension == u8"srt") {
        u8 block;
        // The first token (the first cue number) is skipped; a block is
        // pasted when the next number (or an empty line) is reached.
        for (std::size_t i = 1; i < all.size(); ++i) {
            const u8 line = trimRight(all[i]);
            if (isNumber(line)) {
                if (!block.empty()) {
                    out.push_back(trimRight(block));
                    block.clear();
                }
            } else {
                block += line + u8"\r\n";
            }
        }
        return out;
    }
    for (const u8v token : all)
        if (!(extension == u8"ass" && !token.starts_with(u8"Dialogue")))
            out.emplace_back(token);
    return out;
}

} // namespace

bool turnOnTranslationMode(core::Document &document)
{
    if (!document.scriptInfo(u8"TLMode")) {
        const auto styles = core::decodeStyles(document);
        if (!styles.empty()) {
            // GetStyle(0, "Default"): that Style, else the first.
            core::StyleValues tl = styles.front();
            for (const auto &s : styles)
                if (s.name == u8"Default") {
                    tl = s;
                    break;
                }
            for (std::size_t i = 0; i < styles.size(); ++i) {
                u8 name = u8"TLmode";
                if (i > 0) {
                    const auto n = std::to_string(i);
                    name += u8(n.begin(), n.end());
                }
                bool taken = false;
                for (const auto &s : styles)
                    taken = taken || s.name == name;
                if (!taken) {
                    tl.name = name;
                    if (!document.setScriptInfo(u8"TLMode Style", name))
                        return false;
                    break;
                }
            }
            tl.alignment = u8"8";
            document.appendStyle(styleFields(tl));
        }
    }
    return document.setScriptInfo(u8"TLMode", u8"Yes");
}

std::expected<void, CommandRefusal> turnOffTranslationMode(EditSession &session)
{
    if (session.document().scriptInfo(u8"TLMode") != std::optional<u8>(u8"Yes"))
        return std::unexpected(CommandRefusal::Invalid);
    std::set<core::LineId> touches;
    for (const auto *line : session.document().lines())
        if (!line->translation.empty() || line->unconfirmed)
            touches.insert(line->id);
    return session.run(Command{"Turning off translator mode", session.revision(), touches, [&](core::Document &d) {
        d.removeScriptInfo(u8"TLMode");
        if (const auto style = d.scriptInfo(u8"TLMode Style")) {
            d.removeStyle(*style);
            d.removeScriptInfo(u8"TLMode Style");
        }
        for (const auto id : touches)
            if (!d.editLine(id, [](core::LineRecord &l) {
                    if (!l.translation.empty()) {
                        l.text = l.translation;
                        l.translation.clear();
                    }
                    l.unconfirmed = false;
                }))
                return false;
        return true;
    }});
}

std::expected<void, CommandRefusal> pasteTranslation(EditSession &session, std::u8string_view fileText,
                                                     std::u8string_view extension, const LineVisible &visible,
                                                     const core::PasteConversion &conversion)
{
    const auto format = session.document().format();
    if (format != core::SubtitleFormat::Ass && format != core::SubtitleFormat::PlainText)
        return std::unexpected(CommandRefusal::Invalid);
    // Legacy reads the file in text mode (wxFFile "r" on Windows): CRLF becomes LF.
    u8 normalized(fileText);
    for (std::size_t p; (p = normalized.find(u8"\r\n")) != u8::npos;)
        normalized.erase(p, 1);
    fileText = normalized;
    std::vector<u8> texts;
    for (const auto &entry : pastedEntries(fileText, extension))
        texts.push_back(core::dialogueFromRaw(entry, format, conversion).text);
    // Appended entries keep the other fields legacy's Dialogue(raw) read.
    std::vector<core::LineRecord> appended;
    std::vector<std::pair<core::LineId, u8>> translations;
    std::set<core::LineId> touched;
    {
        std::vector<core::LineId> shown;
        for (const auto *l : session.document().lines())
            if (!visible || visible(l->id))
                shown.push_back(l->id);
        // GetStyle(0, "Default")->Name.
        u8 styleName = u8"Default";
        const auto styles = core::decodeStyles(session.document());
        bool hasDefault = false;
        for (const auto &s : styles)
            hasDefault = hasDefault || s.name == u8"Default";
        if (!hasDefault && !styles.empty())
            styleName = styles.front().name;
        const auto entries = pastedEntries(fileText, extension);
        for (std::size_t k = 0; k < entries.size(); ++k) {
            if (k < shown.size()) {
                translations.emplace_back(shown[k], texts[k]);
                touched.insert(shown[k]);
                continue;
            }
            core::LineRecord line = core::dialogueFromRaw(entries[k], format, conversion);
            line.start.value = core::DocumentTime{};
            line.end.value = core::DocumentTime{};
            line.style = styleName;
            line.translation = line.text;
            line.text.clear();
            appended.push_back(std::move(line));
        }
    }
    const auto ran = session.run(Command{"Pasting translation", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto &[id, text] : translations)
                                                 if (!d.editLine(id, [&](core::LineRecord &l) { l.translation = text; }))
                                                     return false;
                                             for (const auto &line : appended)
                                                 if (!d.appendLine(line))
                                                     return false;
                                             return turnOnTranslationMode(d) && d.setScriptInfo(u8"TLMode Showtl", u8"Yes");
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

namespace {

struct Row {
    std::optional<core::LineId> id;
    core::LineRecord line;
    bool visible = true;
    bool changed = false;
};

// GetKeyFromPosition(i, delta, false) for delta >= 0: the delta-th shown row after i.
std::optional<std::size_t> rowWithOffset(const std::vector<Row> &rows, std::size_t i, int delta)
{
    if (i > rows.size())
        return std::nullopt;
    if (delta <= 0)
        return delta == 0 ? std::optional(i) : std::nullopt;
    int shown = 0;
    for (std::size_t k = i + 1; k < rows.size(); ++k) {
        if (rows[k].visible)
            ++shown;
        if (shown == delta)
            return k;
    }
    return std::nullopt;
}

} // namespace

std::expected<void, CommandRefusal> moveTranslation(EditSession &session, TranslationMove move, const LineVisible &visible)
{
    const auto &document = session.document();
    if (document.scriptInfo(u8"TLMode") != u8"Yes" || document.scriptInfo(u8"TLMode Showtl") != u8"Yes")
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<Row> rows;
    std::vector<std::size_t> selected;
    for (const auto *l : document.lines()) {
        if (session.selection().selected.contains(l->id))
            selected.push_back(rows.size());
        rows.push_back(Row{l->id, *l, !visible || visible(l->id), false});
    }
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const int mode = static_cast<int>(move);
    const std::size_t first = selected[0];
    int count = 1;
    if (selected.size() > 1)
        count = static_cast<int>(selected[1] - first);
    auto setTranslation = [&](std::size_t i, u8 text) {
        rows[i].line.translation = std::move(text);
        rows[i].changed = true;
    };
    if (mode < 3) {
        if (mode == 2) {
            Row blank = rows[first];
            blank.id.reset();
            blank.line.text.clear();
            blank.changed = true;
            rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(first), static_cast<std::size_t>(count), blank);
        }
        for (std::size_t i = first; i < rows.size(); ++i) {
            if (!rows[i].visible)
                continue;
            const auto next = rowWithOffset(rows, i, 1);
            const auto last = rowWithOffset(rows, i, count);
            if (i < first + static_cast<std::size_t>(std::max(count, 0))) {
                if (mode == 1) {
                    if (next) {
                        const u8 mid = !rows[first].line.translation.empty() && !rows[*next].line.translation.empty()
                                           ? u8(u8"\\N")
                                           : u8();
                        setTranslation(first, rows[first].line.translation + mid + rows[*next].line.translation);
                        if (i != first && last)
                            setTranslation(i, rows[*last].line.translation);
                    }
                } else if (last) {
                    setTranslation(i, rows[*last].line.translation);
                }
            } else if (last) {
                setTranslation(i, rows[*last].line.translation);
            } else if (!rows[i].line.text.empty()) {
                --count;
            }
        }
        if (count > 0)
            rows.erase(rows.end() - std::min<std::ptrdiff_t>(count, static_cast<std::ptrdiff_t>(rows.size())), rows.end());
    } else {
        Row blank;
        blank.line.style = document.scriptInfo(u8"TLMode Style").value_or(u8());
        blank.changed = true;
        for (int i = 0; i < count; ++i)
            rows.push_back(blank);
        bool once = true;
        for (std::size_t i = rows.size(); i-- > first;) {
            if (i < first + static_cast<std::size_t>(std::max(count, 0))) {
                if (mode == 3) {
                    setTranslation(i, u8());
                } else {
                    if (mode == 4) {
                        const std::size_t into = first + static_cast<std::size_t>(count);
                        if (once) {
                            rows[into].line.start = rows[first].line.start;
                            once = false;
                        }
                        rows[into].line.text = rows[i].line.text + u8"\\N" + rows[into].line.text;
                        rows[into].changed = true;
                        --count;
                    }
                    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(i));
                }
            } else {
                setTranslation(i, rows[i - static_cast<std::size_t>(count)].line.translation);
            }
        }
    }
    // Apply: Lines that are gone are removed, changed ones edited, new ones
    // inserted before the next kept Line (or appended).
    std::set<core::LineId> kept, touched;
    for (const auto &r : rows)
        if (r.id)
            kept.insert(*r.id);
    for (const auto *l : document.lines())
        if (!kept.contains(l->id))
            touched.insert(l->id);
    for (const auto &r : rows)
        if (r.id && r.changed)
            touched.insert(*r.id);
    std::optional<core::LineId> active;
    const std::size_t activeRow = std::min(first, rows.empty() ? 0 : rows.size() - 1);
    const auto ran = session.run(Command{"Moving translation text", session.revision(), touched, [&](core::Document &d) {
        for (const auto id : touched)
            if (!kept.contains(id) && !d.removeLine(id))
                return false;
        std::vector<std::optional<core::LineId>> ids;
        for (const auto &r : rows)
            ids.push_back(r.id);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto &r = rows[i];
            if (r.id) {
                if (r.changed && !d.editLine(*r.id, [&](core::LineRecord &l) {
                        l.translation = r.line.translation;
                        l.text = r.line.text;
                        l.start = r.line.start;
                    }))
                    return false;
                continue;
            }
            std::optional<core::LineId> before;
            for (std::size_t k = i + 1; k < rows.size() && !before; ++k)
                before = ids[k];
            const auto added = before ? d.insertLineBefore(*before, r.line) : d.appendLine(r.line);
            if (!added)
                return false;
            ids[i] = *added;
        }
        if (activeRow < ids.size())
            active = ids[activeRow];
        return true;
    }});
    if (!ran)
        return std::unexpected(ran.error());
    // Legacy keeps the first selected row selected and edited.
    if (active)
        session.setSelection(Selection{active, {*active}, active, {}});
    return {};
}

} // namespace hikari::application
