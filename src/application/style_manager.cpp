#include "hikari/application/style_manager.h"

#include "hikari/core/srv3.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <absl/container/flat_hash_map.h>

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

}

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

std::vector<std::size_t> transferStyles(StyleList &into, const StyleList &styles, const ClashChoices &choices)
{
    std::vector<std::size_t> rows;
    absl::flat_hash_map<u8, std::size_t> first;
    for (std::size_t i = into.size(); i-- > 0;)
        first[into[i].name] = i;
    StyleNames taken = namesOf(into);
    for (const auto &style : styles)
        taken.insert(style.name);
    absl::flat_hash_map<u8, int> next;
    absl::flat_hash_map<u8, int> seen;
    const auto add = [&](core::StyleValues style) {
        first.try_emplace(style.name, into.size());
        taken.insert(style.name);
        into.push_back(std::move(style));
        rows.push_back(into.size() - 1);
    };
    for (const auto &style : styles) {
        const int occurrence = seen[style.name]++;
        const auto found = first.find(style.name);
        if (found == first.end()) {
            add(style);
            continue;
        }
        const auto choice = choices.find({style.name, occurrence});
        if (choice == choices.end() || choice->second.action == ClashAction::Skip)
            continue;
        if (choice->second.action == ClashAction::Replace) {
            into[found->second] = style;
            rows.push_back(found->second);
            continue;
        }
        auto renamed = style;
        const u8 &wanted = choice->second.newName;
        if (taken.contains(wanted)) {
            int &count = next.try_emplace(wanted, 2).first->second;
            for (;; ++count) {
                u8 candidate = wanted + u8" (" + fromString(std::to_string(count)) + u8")";
                if (!taken.contains(candidate)) {
                    renamed.name = std::move(candidate);
                    break;
                }
            }
        } else {
            renamed.name = wanted;
        }
        add(std::move(renamed));
    }
    return rows;
}

StyleNames namesOf(const StyleList &styles)
{
    StyleNames out;
    out.reserve(styles.size());
    for (const auto &s : styles)
        out.insert(s.name);
    return out;
}

std::u8string freeStyleName(const StyleNames &names, std::u8string_view name)
{
    for (int count = 2;; ++count) {
        u8 candidate = u8(name) + u8" (" + fromString(std::to_string(count)) + u8")";
        if (!names.contains(candidate))
            return candidate;
    }
}

std::u8string freeStyleName(const StyleList &styles, std::u8string_view name)
{
    return freeStyleName(namesOf(styles), name);
}

std::vector<std::size_t> moveStyles(StyleList &styles, std::vector<std::size_t> rows, StyleMove move)
{
    const long size = static_cast<long>(styles.size());
    if (rows.empty())
        return rows;
    const long step = move == StyleMove::ToStart ? -size : move == StyleMove::Up ? -1 : move == StyleMove::Down ? 1 : size;
    const bool up = move == StyleMove::ToStart || move == StyleMove::Up;
    const std::vector<std::size_t> from = rows;
    long lastUp = 0, lastDown = size - 1;
    const long count = static_cast<long>(rows.size());
    for (long k = 0; k < count; ++k) {
        const long i = up ? k : count - 1 - k;
        long to = static_cast<long>(from[static_cast<std::size_t>(i)]) + step;
        if (up && to < lastUp)
            to = lastUp;
        else if (!up && to > lastDown)
            to = lastDown;
        rows[static_cast<std::size_t>(i)] = static_cast<std::size_t>(to);
        if (up)
            ++lastUp;
        else
            --lastDown;
    }
    StyleList out(styles.size());
    std::vector<char> moved(styles.size(), 0), placed(styles.size(), 0);
    for (std::size_t k = 0; k < rows.size(); ++k) {
        out[rows[k]] = std::move(styles[from[k]]);
        moved[from[k]] = 1;
        placed[rows[k]] = 1;
    }
    std::size_t next = 0;
    for (std::size_t r = 0; r < styles.size(); ++r) {
        if (moved[r])
            continue;
        while (placed[next])
            ++next;
        out[next++] = std::move(styles[r]);
    }
    styles = std::move(out);
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
    std::optional<std::size_t> row;
    if (!dummy && styles[*edit.row].name == edit.oldName)
        row = edit.row;
    else if (found)
        row = found;
    else if (!dummy)
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

namespace {

std::vector<core::Srv3StyleOptions> followedSrv3Options(const std::vector<core::Srv3StyleOptions> &options,
                                                        const StyleList &before, const StyleList &after,
                                                        const std::vector<std::optional<std::size_t>> &origin,
                                                        const std::optional<std::pair<u8, u8>> &copy)
{
    const auto entryFor = [&](std::u8string_view style) -> const core::Srv3StyleOptions * {
        const std::u16string name = core::toUtf16(style);
        const auto it = std::ranges::find(options, name, &core::Srv3StyleOptions::style);
        return it == options.end() ? nullptr : &*it;
    };
    std::vector<core::Srv3StyleOptions> out;
    for (std::size_t i = 0; i < after.size(); ++i) {
        const core::Srv3StyleOptions *from = nullptr;
        if (origin[i] && *origin[i] < before.size() && firstNamed(before, before[*origin[i]].name) == origin[i])
            from = entryFor(before[*origin[i]].name);
        else if (!origin[i] && copy && copy->second == after[i].name)
            from = entryFor(copy->first);
        if (from) {
            out.push_back(*from);
            out.back().style = core::toUtf16(after[i].name);
        }
    }
    for (const auto &o : options)
        if (!firstNamed(before, core::toUtf8(o.style)))
            out.push_back(o);
    return out;
}

}

std::expected<void, CommandRefusal>
setDocumentStyles(EditSession &session, const StyleList &styles, const std::vector<std::optional<std::size_t>> &origin,
                  std::optional<std::pair<std::u8string, std::u8string>> rename,
                  std::optional<std::pair<std::u8string, std::u8string>> copy)
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
    const auto srv3Before = core::srv3StyleOptions(session.document());
    const auto srv3After = followedSrv3Options(srv3Before, *current, styles, origin, copy);
    return session.run(Command{"Style editing", session.revision(), touches, [&](core::Document &d) {
        if (!d.rearrangeStyles(slots))
            return false;
        if (srv3After != srv3Before && !core::setSrv3StyleOptions(d, srv3After))
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

namespace {

std::vector<std::u8string> scanCatalogs(const std::filesystem::path &dir)
{
    std::vector<std::u8string> names;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".sty")
            names.push_back(entry.path().stem().u8string());
    std::sort(names.begin(), names.end());
    return names;
}

StyleList readCatalogFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return readCatalog(text.str());
}

bool listed(const std::vector<std::u8string> &names, const std::u8string &name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

Tier otherTier(Tier tier)
{
    return tier == Tier::Project ? Tier::Mine : Tier::Project;
}

}

StyleCatalogs::StyleCatalogs(std::filesystem::path directory) : m_dir(std::move(directory))
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    m_names = scanCatalogs(m_dir);
    if (m_names.empty()) {
        std::ofstream(m_dir / "Default.sty", std::ios::binary)
            << "\xEF\xBB\xBFStyle: Default,Garamond,30,&H00FFFFFF,&H000000FF,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,0,2,2,2,10,10,10,1";
        m_names.push_back(u8"Default");
    }
    loadDefault();
}

void StyleCatalogs::loadDefault()
{
    if (listed(m_projectNames, u8"Default"))
        load(u8"Default", Tier::Project);
    else
        load(listed(m_names, u8"Default") ? std::u8string(u8"Default") : m_names.front(), Tier::Mine);
}

void StyleCatalogs::setProjectDirectory(std::filesystem::path directory)
{
    save();
    m_projectDir = std::move(directory);
    m_projectNames = m_projectDir.empty() ? std::vector<std::u8string>() : scanCatalogs(m_projectDir);
    const std::u8string name = m_current;
    if (listed(m_projectNames, name) || listed(m_names, name))
        load(name, resolve(name));
    else
        loadDefault();
}

std::vector<std::u8string> StyleCatalogs::names() const
{
    std::vector<std::u8string> out = m_names;
    for (const auto &name : m_projectNames)
        if (!listed(out, name))
            out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

bool StyleCatalogs::readOnly() const
{
    return m_currentTier == Tier::Mine && listed(m_projectNames, m_current);
}

std::filesystem::path StyleCatalogs::file(const std::u8string &name, Tier tier) const
{
    return directory(tier) / (name + u8".sty");
}

Tier StyleCatalogs::resolve(const std::u8string &name) const
{
    return listed(m_projectNames, name) ? Tier::Project : Tier::Mine;
}

void StyleCatalogs::load(const std::u8string &name, Tier tier)
{
    m_current = name;
    m_currentTier = tier;
    m_styles = readCatalogFile(file(name, tier));
    m_changed = false;
}

bool StyleCatalogs::choose(const std::u8string &name)
{
    return choose(name, resolve(name));
}

bool StyleCatalogs::choose(const std::u8string &name, Tier tier)
{
    if (!listed(names(tier), name))
        return false;
    save();
    load(name, tier);
    return true;
}

bool StyleCatalogs::create(const std::u8string &name, Tier tier)
{
    if (name.empty() || listed(names(tier), name) || (tier == Tier::Project && m_projectDir.empty()) ||
        (tier == Tier::Mine && listed(m_projectNames, name)))
        return false;
    save();
    auto &into = namesIn(tier);
    into.insert(std::upper_bound(into.begin(), into.end(), name), name);
    m_current = name;
    m_currentTier = tier;
    m_styles.clear();
    m_changed = true;
    return true;
}

bool StyleCatalogs::remove(const std::u8string &name)
{
    return remove(name, resolve(name));
}

bool StyleCatalogs::remove(const std::u8string &name, Tier tier)
{
    auto &from = namesIn(tier);
    const auto it = std::find(from.begin(), from.end(), name);
    if (it == from.end() || (tier == Tier::Mine && name == u8"Default"))
        return false;
    const bool shown = name == m_current && tier == m_currentTier;
    if (!shown)
        save();
    const auto index = static_cast<std::size_t>(it - from.begin());
    from.erase(it);
    std::error_code ec;
    std::filesystem::remove(file(name, tier), ec);
    if (!shown) {
        if (tier == Tier::Project && m_currentTier == Tier::Mine && m_current == name)
            load(name, Tier::Mine);
        return true;
    }
    if (from.empty())
        loadDefault();
    else
        load(from[index == 0 ? 0 : index - 1], tier);
    return true;
}

std::optional<std::u8string> StyleCatalogs::copy(const std::u8string &name, Tier from, const ClashChoice &clash)
{
    const Tier to = otherTier(from);
    if ((to == Tier::Project && m_projectDir.empty()) || !listed(names(from), name))
        return std::nullopt;
    if (name == m_current && from == m_currentTier)
        save();
    const auto landed = landingName(names(to), name, clash);
    if (!landed)
        return std::nullopt;
    std::error_code ec;
    std::filesystem::create_directories(directory(to), ec);
    {
        std::ofstream out(file(*landed, to), std::ios::binary | std::ios::trunc);
        out << writeCatalog(readCatalogFile(file(name, from)));
        if (!out.good())
            return std::nullopt;
    }
    auto &into = namesIn(to);
    if (!listed(into, *landed))
        into.insert(std::upper_bound(into.begin(), into.end(), *landed), *landed);
    if (*landed == m_current && to == m_currentTier)
        load(*landed, to);
    return landed;
}

std::optional<std::u8string> StyleCatalogs::move(const std::u8string &name, Tier from, const ClashChoice &clash)
{
    const bool wasShown = name == m_current && from == m_currentTier;
    const auto landed = copy(name, from, clash);
    if (!landed)
        return std::nullopt;
    if (from == Tier::Mine && name == u8"Default")
        return landed;
    remove(name, from);
    if (wasShown)
        load(*landed, otherTier(from));
    return landed;
}

std::optional<StyleList> StyleCatalogs::list(const std::u8string &catalog) const
{
    return list(catalog, resolve(catalog));
}

std::optional<StyleList> StyleCatalogs::list(const std::u8string &catalog, Tier tier) const
{
    if (catalog == m_current && tier == m_currentTier)
        return m_styles;
    if (!listed(names(tier), catalog))
        return std::nullopt;
    return readCatalogFile(file(catalog, tier));
}

std::optional<core::StyleValues> StyleCatalogs::find(const std::u8string &catalog, const std::u8string &style) const
{
    const auto styles = list(catalog);
    if (!styles)
        return std::nullopt;
    if (const auto row = firstNamed(*styles, style))
        return (*styles)[*row];
    return std::nullopt;
}

bool StyleCatalogs::save()
{
    if (!m_changed || readOnly())
        return true;
    std::error_code ec;
    std::filesystem::create_directories(directory(m_currentTier), ec);
    std::ofstream out(file(m_current, m_currentTier), std::ios::binary | std::ios::trunc);
    out << writeCatalog(m_styles);
    m_changed = !out.good();
    return out.good();
}

}
