#include "hikari/application/font_catalogs.h"

#include "hikari/application/font_collector.h"
#include "hikari/application/session_file.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace hikari::application {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

u16 fromUtf8(std::string_view text)
{
    return core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(text.data()), text.size()));
}

std::string toUtf8(u16v text)
{
    const std::u8string out = core::toUtf8(text);
    return std::string(out.begin(), out.end());
}

std::u32string toU32(u16v text)
{
    std::u32string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
            out += char32_t(0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00));
            ++i;
        } else {
            out += char32_t(c);
        }
    }
    return out;
}

u16 fromU32(const std::u32string &text)
{
    u16 out;
    for (const char32_t c : text) {
        if (c >= 0x10000) {
            out += char16_t(0xD800 + ((c - 0x10000) >> 10));
            out += char16_t(0xDC00 + ((c - 0x10000) & 0x3FF));
        } else {
            out += char16_t(c);
        }
    }
    return out;
}

// wxString::Trim(true) / Trim(false): wxSafeIsspace, ASCII white space only
// (' ', \t, \n, \v, \f, \r).
bool space(char16_t c)
{
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}

u16v trimRight(u16v s)
{
    while (!s.empty() && space(s.back()))
        s.remove_suffix(1);
    return s;
}

u16v trimLeft(u16v s)
{
    while (!s.empty() && space(s.front()))
        s.remove_prefix(1);
    return s;
}

bool containsExact(const std::vector<u16> &list, u16v value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

} // namespace

int compareNoCase(u16v a, u16v b, const FontNameLower &lower)
{
    // wxStricmp: _wcsicmp / wcscasecmp, the towlower values in turn.
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char16_t x = lower ? lower(a[i]) : a[i];
        const char16_t y = lower ? lower(b[i]) : b[i];
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}

bool sameNoCase(u16v a, u16v b, const FontNameLower &lower)
{
    return a.size() == b.size() && compareNoCase(a, b, lower) == 0;
}

std::u16string fontCatalogsAutosaveFile(int n)
{
    const std::string number = std::to_string(n);
    return u"FontCatalogsAutosave" + u16(number.begin(), number.end()) + u".txt";
}

FontCatalogs::FontCatalogs(FontNameLower lower, CatalogOrder order) : m_lower(std::move(lower)), m_order(order) {}

bool FontCatalogs::less(u16v a, u16v b) const
{
    if (m_order == CatalogOrder::Utf16)
        return a < b;
    const std::u32string x = toU32(a), y = toU32(b);
    return x < y;
}

FontCatalogs::Catalog *FontCatalogs::findMutable(u16v name)
{
    for (auto &c : m_catalogs)
        if (c.name == name)
            return &c;
    return nullptr;
}

const FontCatalogs::Catalog *FontCatalogs::find(u16v name) const
{
    for (const auto &c : m_catalogs)
        if (c.name == name)
            return &c;
    return nullptr;
}

FontCatalogs::Catalog &FontCatalogs::insert(u16v name)
{
    // fontCatalogs[name] = new wxArrayString, kept in map order.
    auto at = std::find_if(m_catalogs.begin(), m_catalogs.end(), [&](const Catalog &c) { return !less(c.name, name); });
    return *m_catalogs.insert(at, Catalog{u16(name), {}});
}

void FontCatalogs::load(u16v text)
{
    // wxStringTokenizer(text, "\n", wxTOKEN_STRTOK): no empty tokens.
    bool block = false;
    Catalog *current = nullptr;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(u'\n', start), text.size());
        u16v token = text.substr(start, end - start);
        start = end + 1;
        if (token.empty())
            continue;
        token = trimRight(token); // token.Trim()
        if (token.ends_with(u'{') && !token.starts_with(u'\t')) {
            block = true;
            // token.BeforeLast('='): empty when there is no '='.
            const std::size_t eq = token.rfind(u'=');
            const u16 name(eq == u16v::npos ? u16v() : token.substr(0, eq));
            addCatalog(name);
            current = findMutable(name);
            continue;
        }
        if (!block)
            continue;
        if (token == u"}") {
            block = false;
            continue;
        }
        token = trimLeft(token); // token.Trim(false)
        if (current && !token.empty() && !containsExact(current->fonts, token))
            current->fonts.emplace_back(token);
    }
}

std::string FontCatalogs::serialize() const
{
    std::string out;
    for (const auto &c : m_catalogs) {
        if (out.empty())
            out = "\xEF\xBB\xBF"; // PartFileWrite's first part carries U+FEFF
        out += toUtf8(c.name) + "={\r\n";
        for (const auto &font : c.fonts)
            out += "\t" + toUtf8(font) + "\r\n";
        out += "}\r\n";
    }
    return out;
}

u16 FontCatalogs::catalogOf(u16v font) const
{
    for (const auto &c : m_catalogs)
        if (containsExact(c.fonts, font))
            return c.name;
    return {};
}

const std::vector<u16> *FontCatalogs::sortedFonts(u16v catalog)
{
    Catalog *c = findMutable(catalog);
    if (!c)
        return nullptr;
    // std::sort by CmpNoCase; names equal but for case keep their order here.
    std::stable_sort(c->fonts.begin(), c->fonts.end(),
                     [this](const u16 &a, const u16 &b) { return compareNoCase(a, b, m_lower) < 0; });
    return &c->fonts;
}

bool FontCatalogs::contains(u16v catalog, u16v font) const
{
    const Catalog *c = find(catalog);
    return c && containsExact(c->fonts, font);
}

bool FontCatalogs::addCatalog(u16v name)
{
    if (find(name))
        return false;
    insert(name);
    if (!containsExact(m_names, name))
        m_names.emplace_back(name);
    return true;
}

bool FontCatalogs::rename(u16v oldName, u16v newName, const std::function<Clash()> &ask)
{
    std::optional<std::vector<u16>> table; // fontTable
    bool merge = false;
    if (find(newName)) {
        const Clash answer = ask ? ask() : Clash::Cancel;
        if (answer == Clash::Cancel)
            return false;
        merge = answer == Clash::Merge;
        if (merge)
            table = find(newName)->fonts;
        // Delete: the existing list is dropped; the old catalog's replaces it.
    }
    const u16 oldKey(oldName), newKey(newName);
    if (Catalog *old = findMutable(oldKey)) {
        std::vector<u16> fonts = std::move(old->fonts);
        if (table) {
            for (auto &font : fonts)
                if (!containsExact(*table, font))
                    table->push_back(std::move(font));
        } else {
            table = std::move(fonts);
        }
        m_catalogs.erase(m_catalogs.begin() + (old - m_catalogs.data()));
        if (auto it = std::find(m_names.begin(), m_names.end(), oldKey); it != m_names.end())
            m_names.erase(it);
    } else if (!merge) {
        table.reset(); // a new, empty list (legacy also dropped the existing one on Delete)
    }
    // fontCatalogs[newCatalog] = fontTable or a new list. A rename onto the
    // same name keeps its fonts (Y6-rename-self: legacy freed the list it
    // then stored).
    Catalog *target = findMutable(newKey);
    if (!target)
        target = &insert(newKey);
    target->fonts = table ? std::move(*table) : std::vector<u16>();
    return true;
}

void FontCatalogs::remove(u16v name)
{
    Catalog *c = findMutable(name);
    if (!c)
        return;
    if (auto it = std::find(m_names.begin(), m_names.end(), name); it != m_names.end())
        m_names.erase(it);
    m_catalogs.erase(m_catalogs.begin() + (c - m_catalogs.data()));
}

void FontCatalogs::addFont(u16v catalog, u16v font)
{
    if (Catalog *c = findMutable(catalog); c && !containsExact(c->fonts, font))
        c->fonts.emplace_back(font);
}

void FontCatalogs::removeFont(u16v catalog, u16v font)
{
    if (Catalog *c = findMutable(catalog))
        if (auto it = std::find(c->fonts.begin(), c->fonts.end(), font); it != c->fonts.end())
            c->fonts.erase(it);
}

bool FontCatalogs::collect(u16v catalog, bool clear, const std::vector<const core::Document *> &documents)
{
    Catalog *c = findMutable(catalog);
    if (!c)
        return false;
    if (clear)
        c->fonts.clear();
    // it->second->Index(fn, false): not added when present ignoring case.
    const auto add = [&](const u16 &fn) {
        for (const auto &font : c->fonts)
            if (sameNoCase(font, fn, m_lower))
                return;
        c->fonts.push_back(fn);
    };
    static const std::vector<std::u32string> names{U"p", U"fn"};
    for (const core::Document *doc : documents) {
        if (!doc)
            continue;
        for (const auto &style : core::decodeStyles(*doc))
            add(core::toUtf16(style.fontname));
        for (const auto &line : doc->lines()) {
            if (line->comment)
                continue;
            // ParseTags parses GetTextNoCopy: the translation when there is one.
            const std::u8string &text = line->translation.empty() ? line->text : line->translation;
            for (const auto &tag : collector::parseTags(toU32(core::toUtf16(text)), names)) {
                if (tag.name == U"p" || tag.name == U"pvector")
                    break;
                if (tag.name == U"fn")
                    add(fromU32(tag.value));
            }
        }
    }
    return true;
}

std::optional<std::u16string> readFontCatalogFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt; // !IsFileReadable
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto text = decodeSessionBytes(bytes); // wxConvAuto; nullopt when empty
    if (!text)
        return std::nullopt;
    return fromUtf8(*text);
}

bool writeFontCatalogFile(const std::filesystem::path &path, const std::string &bytes)
{
    std::error_code ec;
    if (path.has_parent_path() && !std::filesystem::exists(path.parent_path(), ec))
        std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(bytes.data(), std::streamsize(bytes.size()));
    return bool(out);
}

} // namespace hikari::application
