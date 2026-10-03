#include "hikari/application/style_manager.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace hikari::application {

namespace {

using u8 = std::u8string;
namespace f = style_field;

std::optional<std::size_t> firstNamed(const StyleList &styles, std::u8string_view name)
{
    for (std::size_t i = 0; i < styles.size(); ++i)
        if (styles[i].name == name)
            return i;
    return std::nullopt;
}

std::optional<std::size_t> lastNamed(const StyleList &styles, std::u8string_view name)
{
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < styles.size(); ++i)
        if (styles[i].name == name)
            found = i;
    return found;
}

u8 fromString(const std::string &s)
{
    return u8(s.begin(), s.end());
}

} // namespace

int compareStyles(const core::StyleValues &a, const core::StyleValues &b)
{
    int r = 0;
    const auto bit = [&](bool differs, int flag) {
        if (differs)
            r |= flag;
    };
    bit(a.fontname != b.fontname, f::FontName);
    bit(a.fontsize != b.fontsize, f::FontSize);
    bit(a.bold != b.bold, f::Bold);
    bit(a.italic != b.italic, f::Italic);
    bit(a.underline != b.underline, f::Underline);
    bit(a.strikeOut != b.strikeOut, f::StrikeOut);
    bit(a.primary != b.primary, f::Primary);
    bit(a.secondary != b.secondary, f::Secondary);
    bit(a.outline != b.outline, f::Outline);
    bit(a.back != b.back, f::Shadow);
    bit(a.outlineWidth != b.outlineWidth, f::OutlineWidth);
    bit(a.shadow != b.shadow, f::ShadowWidth);
    bit(a.scaleX != b.scaleX, f::ScaleX);
    bit(a.scaleY != b.scaleY, f::ScaleY);
    bit(a.angle != b.angle, f::Angle);
    bit(a.spacing != b.spacing, f::Spacing);
    bit(a.borderStyle != b.borderStyle, f::BorderStyle);
    bit(a.alignment != b.alignment, f::Alignment);
    bit(a.marginLeft != b.marginLeft, f::MarginLeft);
    bit(a.marginRight != b.marginRight, f::MarginRight);
    bit(a.marginVertical != b.marginVertical, f::MarginVertical);
    bit(a.encoding != b.encoding, f::Encoding);
    return r;
}

core::StyleValues copyStyleChanges(core::StyleValues t, const core::StyleValues &c, int fields)
{
    if (fields & f::FontName) t.fontname = c.fontname;
    if (fields & f::FontSize) t.fontsize = c.fontsize;
    if (fields & f::Bold) t.bold = c.bold;
    if (fields & f::Italic) t.italic = c.italic;
    if (fields & f::Underline) t.underline = c.underline;
    if (fields & f::StrikeOut) t.strikeOut = c.strikeOut;
    if (fields & f::Primary) t.primary = c.primary;
    if (fields & f::Secondary) t.secondary = c.secondary;
    if (fields & f::Outline) t.outline = c.outline;
    if (fields & f::Shadow) t.back = c.back;
    if (fields & f::OutlineWidth) t.outlineWidth = c.outlineWidth;
    if (fields & f::ShadowWidth) t.shadow = c.shadow;
    if (fields & f::ScaleX) t.scaleX = c.scaleX;
    if (fields & f::ScaleY) t.scaleY = c.scaleY;
    if (fields & f::Angle) t.angle = c.angle;
    if (fields & f::Spacing) t.spacing = c.spacing;
    if (fields & f::BorderStyle) t.borderStyle = c.borderStyle;
    if (fields & f::Alignment) t.alignment = c.alignment;
    if (fields & f::MarginLeft) t.marginLeft = c.marginLeft;
    if (fields & f::MarginRight) t.marginRight = c.marginRight;
    if (fields & f::MarginVertical) t.marginVertical = c.marginVertical;
    if (fields & f::Encoding) t.encoding = c.encoding;
    return t;
}

core::StyleValues defaultStyle(std::u8string name)
{
    auto s = core::legacy::decodeStyle(
        u8"Style: Default,Garamond,40,&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1", false);
    s.name = std::move(name);
    return s;
}

std::u8string newStyleName(const StyleList &styles, std::u8string_view base)
{
    for (int count = 0;; ++count) {
        const u8 name = count == 0 ? u8(base) : u8(base) + fromString(std::to_string(count));
        if (!firstNamed(styles, name))
            return name;
    }
}

std::vector<std::size_t> transferStyles(StyleList &into, const StyleList &styles, const AskReplace &ask, bool cancelStops)
{
    std::vector<std::size_t> rows;
    std::optional<Replace> prompt;
    for (const auto &style : styles) {
        const auto found = firstNamed(into, style.name);
        if (!found) {
            into.push_back(style);
            rows.push_back(into.size() - 1);
            continue;
        }
        if (prompt != Replace::YesToAll && (cancelStops || prompt != Replace::Cancel)) {
            prompt = ask ? ask(style.name) : Replace::No;
            if (prompt == Replace::Cancel && cancelStops)
                break;
        }
        if (prompt == Replace::Yes || prompt == Replace::YesToAll) {
            into[*found] = style;
            rows.push_back(*found);
        }
    }
    return rows;
}

std::vector<std::size_t> moveStyles(StyleList &styles, std::vector<std::size_t> rows, StyleMove move)
{
    const long size = static_cast<long>(styles.size());
    if (rows.empty())
        return rows;
    const long step = move == StyleMove::ToStart ? -size : move == StyleMove::Up ? -1 : move == StyleMove::Down ? 1 : size;
    const bool up = move == StyleMove::ToStart || move == StyleMove::Up;
    long lastUp = 0, lastDown = size - 1;
    const long count = static_cast<long>(rows.size());
    for (long k = 0; k < count; ++k) {
        const long i = up ? k : count - 1 - k;
        const long from = static_cast<long>(rows[static_cast<std::size_t>(i)]);
        long to = from + step;
        if (up && to < lastUp)
            to = lastUp;
        else if (!up && to > lastDown)
            to = lastDown;
        const auto style = styles[static_cast<std::size_t>(from)];
        styles.erase(styles.begin() + from);
        styles.insert(styles.begin() + to, style);
        rows[static_cast<std::size_t>(i)] = static_cast<std::size_t>(to);
        if (up)
            ++lastUp;
        else
            --lastDown;
    }
    return rows;
}

void sortStyles(StyleList &styles, const NameCompare &compare)
{
    std::stable_sort(styles.begin(), styles.end(), [&](const core::StyleValues &a, const core::StyleValues &b) {
        return compare ? compare(a.name, b.name) < 0 : a.name < b.name;
    });
}

std::expected<std::size_t, std::u8string> commitStyle(StyleList &styles, const StyleEdit &edit)
{
    bool dummy = !edit.row || *edit.row >= styles.size();
    const auto found = lastNamed(styles, edit.style.name);
    if ((found && dummy) || (found && edit.oldName != edit.style.name && !dummy))
        return std::unexpected(edit.style.name);
    std::optional<std::size_t> row = found;
    if (!row && !dummy)
        row = firstNamed(styles, edit.oldName);
    if (!row)
        dummy = true;
    if (edit.fields)
        for (const auto r : edit.selected) {
            if (row && r == *row)
                continue;
            if (r >= styles.size())
                break;
            styles[r] = copyStyleChanges(styles[r], edit.style, edit.fields);
        }
    if (dummy) {
        styles.push_back(edit.style);
        return styles.size() - 1;
    }
    styles[*row] = edit.style;
    return *row;
}

std::optional<StyleList> documentStyles(const EditSession &session)
{
    const auto &d = session.document();
    if (d.format() != core::SubtitleFormat::Ass && d.format() != core::SubtitleFormat::PlainText)
        return std::nullopt;
    bool styles = false;
    for (const auto &section : d.sections()) {
        if (section.kind == core::SectionKind::SsaStyles)
            return std::nullopt;
        styles = styles || section.kind == core::SectionKind::Styles;
    }
    if (!styles)
        return std::nullopt;
    return core::decodeStyles(d);
}

std::expected<void, CommandRefusal>
setDocumentStyles(EditSession &session, const StyleList &styles, const std::vector<std::optional<std::size_t>> &origin,
                  std::optional<std::pair<std::u8string, std::u8string>> rename)
{
    const auto current = documentStyles(session);
    if (!current || origin.size() != styles.size())
        return std::unexpected(CommandRefusal::Invalid);
    std::vector<core::Document::StyleSlot> slots;
    bool changed = styles.size() != current->size();
    for (std::size_t i = 0; i < styles.size(); ++i) {
        core::Document::StyleSlot slot;
        slot.from = origin[i];
        if (!origin[i] || *origin[i] >= current->size() || compareStyles((*current)[*origin[i]], styles[i]) ||
            (*current)[*origin[i]].name != styles[i].name)
            slot.fields = core::legacy::styleRawFields(styles[i]);
        changed = changed || !origin[i] || *origin[i] != i || slot.fields;
        slots.push_back(std::move(slot));
    }
    std::set<core::LineId> touches;
    if (rename)
        for (const auto *line : session.document().lines())
            if (line->style == rename->first)
                touches.insert(line->id);
    if (!changed && touches.empty())
        return {};
    return session.run(Command{"Style editing", session.revision(), touches, [&](core::Document &d) {
        if (!d.rearrangeStyles(slots))
            return false;
        for (const auto id : touches)
            if (!d.editLine(id, [&](core::LineRecord &l) { l.style = rename->second; }))
                return false;
        return true;
    }});
}

std::expected<CleanResult, CommandRefusal> cleanDocumentStyles(EditSession &session)
{
    const auto styles = documentStyles(session);
    if (!styles)
        return std::unexpected(CommandRefusal::Invalid);
    std::set<u8> used;
    for (const auto *line : session.document().lines())
        used.insert(line->style);
    const u8 tlStyle = session.document().scriptInfo(u8"TLMode Style").value_or(u8());
    CleanResult result;
    StyleList kept;
    std::vector<std::optional<std::size_t>> origin;
    for (std::size_t i = 0; i < styles->size(); ++i) {
        const auto &name = (*styles)[i].name;
        if (used.contains(name) || name == tlStyle) {
            result.used.push_back(name);
            kept.push_back((*styles)[i]);
            origin.push_back(i);
        } else {
            result.deleted.push_back(name);
        }
    }
    if (!result.deleted.empty())
        if (auto ran = setDocumentStyles(session, kept, origin); !ran)
            return std::unexpected(ran.error());
    return result;
}

StyleList readCatalog(std::string_view bytes)
{
    if (bytes.starts_with("\xEF\xBB\xBF"))
        bytes.remove_prefix(3);
    StyleList out;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto j = std::min(bytes.find('\n', i), bytes.size());
        std::string_view line = bytes.substr(i, j - i);
        i = j + 1;
        if (!line.starts_with("Style: "))
            continue;
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        out.push_back(core::legacy::decodeStyle(u8(line.begin(), line.end()), false));
    }
    return out;
}

std::string writeCatalog(const StyleList &styles)
{
    std::string out = "\xEF\xBB\xBF";
    for (const auto &style : styles) {
        const auto fields = core::legacy::styleRawFields(style);
        out += "Style: ";
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i)
                out += ',';
            out.append(fields[i].begin(), fields[i].end());
        }
        out += "\r\n";
    }
    return out;
}

StyleCatalogs::StyleCatalogs(std::filesystem::path directory) : m_dir(std::move(directory))
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    for (const auto &entry : std::filesystem::directory_iterator(m_dir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".sty")
            m_names.push_back(entry.path().stem().u8string());
    std::sort(m_names.begin(), m_names.end());
    if (m_names.empty()) {
        // Legacy writes this Default catalog when there is none.
        std::ofstream(m_dir / "Default.sty", std::ios::binary)
            << "\xEF\xBB\xBFStyle: Default,Garamond,30,&H00FFFFFF,&H000000FF,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,0,2,2,2,10,10,10,1";
        m_names.push_back(u8"Default");
    }
    const auto it = std::find(m_names.begin(), m_names.end(), u8"Default");
    load(it != m_names.end() ? *it : m_names.front());
}

void StyleCatalogs::load(const std::u8string &name)
{
    m_current = name;
    std::ifstream in(m_dir / (name + u8".sty"), std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    m_styles = readCatalog(text.str());
    m_changed = false;
}

bool StyleCatalogs::choose(const std::u8string &name)
{
    if (std::find(m_names.begin(), m_names.end(), name) == m_names.end())
        return false;
    save();
    load(name);
    return true;
}

bool StyleCatalogs::create(const std::u8string &name)
{
    if (name.empty() || std::find(m_names.begin(), m_names.end(), name) != m_names.end())
        return false;
    save();
    m_names.push_back(name);
    m_current = name;
    m_styles.clear();
    m_changed = true;
    return true;
}

bool StyleCatalogs::remove(const std::u8string &name)
{
    const auto it = std::find(m_names.begin(), m_names.end(), name);
    if (it == m_names.end() || name == u8"Default")
        return false;
    const auto index = static_cast<std::size_t>(it - m_names.begin());
    m_names.erase(it);
    std::error_code ec;
    std::filesystem::remove(m_dir / (name + u8".sty"), ec);
    load(m_names[index == 0 ? 0 : index - 1]);
    return true;
}

bool StyleCatalogs::save()
{
    if (!m_changed)
        return true;
    std::ofstream out(m_dir / (m_current + u8".sty"), std::ios::binary | std::ios::trunc);
    out << writeCatalog(m_styles);
    m_changed = !out.good();
    return out.good();
}

} // namespace hikari::application
