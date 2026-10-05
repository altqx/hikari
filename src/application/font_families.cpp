#include "hikari/application/font_families.h"

#include "hikari/application/legacy_dir.h"
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

u16 lowered(u16v text, const FontNameLower &lower)
{
    u16 out(text);
    if (lower)
        for (auto &c : out)
            c = lower(c);
    return out;
}

bool hasNoCase(const std::vector<u16> &list, u16v name, const FontNameLower &lower)
{
    // wxArrayString::Index(name, false).
    return std::any_of(list.begin(), list.end(), [&](const u16 &f) { return sameNoCase(f, name, lower); });
}

void sortNoCase(std::vector<u16> &list, const FontNameLower &lower)
{
    // FontsTmp->Sort(CmpNoCase); the names differ ignoring case.
    std::stable_sort(list.begin(), list.end(), [&](const u16 &a, const u16 &b) { return compareNoCase(a, b, lower) < 0; });
}

// The search both partial-name selections share (FontDialog.cpp:276-300,
// FontCatalogList.cpp:273-301): `k` carries over from font to font.
// Returns the chosen index, or -1 to take `lastMatch`.
int partialSearch(const std::vector<u16> &fonts, const u16 &partial, const FontNameLower &lower, int &lastMatch)
{
    std::size_t k = 0;
    lastMatch = 0;
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        const u16 fontname = lowered(fonts[i], lower);
        if (fontname.empty() || fontname[0] < partial[0])
            continue;
        while (k < partial.size() && k < fontname.size()) {
            if (fontname[k] == partial[k]) {
                ++k;
                lastMatch = int(i);
                if (k >= partial.size())
                    return int(i);
            } else if (k > 0 && fontname.compare(0, k, partial, 0, k) != 0) {
                return -1;
            } else if (fontname[k] > partial[k]) {
                return int(i);
            } else {
                break;
            }
        }
    }
    return -1;
}

} // namespace

std::u32string filterCharacters(u16v filter, FontListPlatform platform)
{
    std::u32string out;
    for (std::size_t i = 0; i < filter.size(); ++i) {
        const char16_t c = filter[i];
        const bool high = c >= 0xD800 && c < 0xDC00, surrogate = c >= 0xD800 && c < 0xE000;
        if (platform == FontListPlatform::Windows) {
            if (!surrogate)
                out += char32_t(c); // U16_IS_SURROGATE units are skipped
            continue;
        }
        if (high && i + 1 < filter.size() && filter[i + 1] >= 0xDC00 && filter[i + 1] < 0xE000) {
            out += char32_t(0x10000 + ((c - 0xD800) << 10) + (filter[i + 1] - 0xDC00));
            ++i;
        } else {
            out += char32_t(c);
        }
    }
    return out;
}

std::u16string listedFamily(const SystemFace &face)
{
    if (!face.listedFamily.empty())
        return fromUtf8(face.listedFamily);
    return face.families.empty() ? u16() : fromUtf8(face.families.front());
}

FontFamilies::FontFamilies(FontServicePort &fonts, FontNameLower lower, FontListPlatform platform)
    : m_fonts(fonts), m_lower(std::move(lower)), m_platform(platform)
{
}

void FontFamilies::enumerate()
{
    // EnumerateFontsLocked: each face the provider lists, once per name
    // ignoring case; the filtered list checks a face only while its name is
    // not in it yet.
    const std::vector<SystemFace> faces = m_fonts.pickerFaces(m_environment);
    std::vector<bool> covers;
    if (m_filtered)
        covers = m_fonts.facesCover(faces, m_environment, filterCharacters(m_filter, m_platform));
    std::vector<u16> all, filtered;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const u16 name = listedFamily(faces[i]);
        if (name.empty())
            continue;
        if (!hasNoCase(all, name, m_lower))
            all.push_back(name);
        if (m_filtered && i < covers.size() && covers[i] && !hasNoCase(filtered, name, m_lower))
            filtered.push_back(name);
    }
    sortNoCase(all, m_lower);
    m_all = std::move(all);
    if (m_filtered) {
        sortNoCase(filtered, m_lower);
        m_filtered = std::move(filtered);
    }
}

const std::vector<u16> &FontFamilies::fonts()
{
    if (m_all.empty())
        enumerate();
    return m_all;
}

const std::vector<u16> &FontFamilies::filteredFonts(u16v filter)
{
    m_filter = u16(filter);
    if (!m_filtered) {
        m_filtered.emplace();
        enumerate();
    }
    return *m_filtered;
}

std::vector<std::string> FontFamilies::unreadableExternalFonts()
{
    std::vector<std::string> out;
    if (m_environment.externalFonts.empty())
        return out;
    FontEnvironment onlyExternal = m_environment;
    onlyExternal.systemFonts = false;
    const auto faces = m_fonts.pickerFaces(onlyExternal);
    for (const auto &font : m_environment.externalFonts)
        if (std::none_of(faces.begin(), faces.end(), [&](const SystemFace &f) { return f.externalFile == font.name; }))
            out.push_back(font.name);
    return out;
}

std::vector<u16> catalogChoices(const std::vector<u16> &names, const u16 &allFonts, const u16 &withoutCatalog)
{
    // HikariChoice::Insert: pos = MID(0, position, size - 1).
    std::vector<u16> list = names;
    const auto insert = [&](const u16 &what, int position) {
        const int pos = std::max(0, std::min(position, int(list.size()) - 1));
        list.insert(list.begin() + pos, what);
    };
    insert(allFonts, 0);
    insert(withoutCatalog, 1);
    return list;
}

std::vector<u16> catalogView(int selection, u16v value, const std::vector<u16> &fonts, FontCatalogs &catalogs,
                             WithoutCatalogLookup lookup)
{
    if (selection == 0)
        return fonts;
    if (selection == 1) {
        std::vector<u16> list = fonts;
        for (const auto &catalog : catalogs.catalogs()) {
            for (const auto &font : catalog.fonts) {
                if (lookup == WithoutCatalogLookup::FirstEntry) {
                    // FindString answers 0: the first font goes, while there is one
                    // (wxArrayString::RemoveAt on an empty list only asserts).
                    if (!list.empty())
                        list.erase(list.begin());
                    continue;
                }
                if (auto it = std::find(list.begin(), list.end(), font); it != list.end())
                    list.erase(it);
            }
        }
        return list;
    }
    std::vector<u16> list;
    if (const auto *cfonts = catalogs.sortedFonts(value))
        for (const auto &font : *cfonts)
            if (std::find(fonts.begin(), fonts.end(), font) != fonts.end()) // fonts->Index(font)
                list.push_back(font);
    return list;
}

int fontListPartialIndex(const std::vector<u16> &fonts, u16v partial, const FontNameLower &lower)
{
    // SetSelection(pos): below 0 is none (-1), past the end the last font.
    const auto select = [&](int pos) { return pos < 0 ? -1 : pos >= int(fonts.size()) ? int(fonts.size()) - 1 : pos; };
    if (partial.empty())
        return select(0);
    int lastMatch = 0;
    const int found = partialSearch(fonts, lowered(partial, lower), lower, lastMatch);
    return select(found == -1 ? lastMatch : found);
}

int fontListNameIndex(const std::vector<u16> &fonts, u16v name, const FontNameLower &lower)
{
    for (std::size_t i = 0; i < fonts.size(); ++i)
        if (sameNoCase(fonts[i], name, lower))
            return int(i);
    return fontListPartialIndex(fonts, name, lower);
}

int catalogListPartialIndex(const std::vector<u16> &fonts, u16v partial, const FontNameLower &lower)
{
    if (partial.empty())
        return 0; // goto done: lastMatch
    int lastMatch = 0;
    const int found = partialSearch(fonts, lowered(partial, lower), lower, lastMatch);
    return found < 0 ? lastMatch : found;
}

PickerResolution pickerResolution(const FontReport &report)
{
    PickerResolution r;
    if (report.requests.empty())
        return r;
    const RequestReport &request = report.requests.front();
    const ResolvedFace *base = nullptr;
    for (const std::size_t i : request.faces)
        if (i < report.faces.size() && report.faces[i].code == 0) {
            base = &report.faces[i];
            break;
        }
    if (!base)
        return r; // Missing
    r.kind = request.requestedFamilyFound ? PickerResolution::Kind::Requested
             : base->stage == SelectionStage::Requested ? PickerResolution::Kind::Substituted
                                                         : PickerResolution::Kind::Fallback;
    r.family = base->familyNames.empty() ? base->postscriptName : base->familyNames.front();
    const std::string &where = !base->attachment.empty() ? base->attachment : base->path;
    r.file = std::filesystem::path(std::u8string(where.begin(), where.end())).filename().string();
    r.faceIndex = base->faceIndex;
    r.emboldened = base->emboldened;
    r.italicized = base->italicized;
    return r;
}

ExternalFontsLoad loadExternalFonts(u16v path, const FontNameLower &lower, FontListPlatform platform)
{
    ExternalFontsLoad out;
    // FindFirstFileW(path + "*"): the folder before the last separator,
    // the name pattern after it.
    const u16 pattern = u16(path) + u"*";
    const std::size_t slash = platform == FontListPlatform::Windows ? pattern.find_last_of(u"\\/") : pattern.rfind(u'/');
    const u16 folder = slash == u16::npos ? u16(u".") : pattern.substr(0, slash == 0 ? 1 : slash);
    const u16 filespec = slash == u16::npos ? pattern : pattern.substr(slash + 1);
    using namespace legacy_dir;
    const auto entries = legacy_dir::entries(std::filesystem::path(folder), filespec, Files | Dirs | Hidden);
    // Windows lists "." and ".." in any folder; the Linux shim found nothing
    // in an empty one and failed (platform.h:523-538).
    if (!entries || (platform == FontListPlatform::Linux && entries->empty()))
        return out;
    out.opened = true;
    for (const auto &entry : *entries) {
        std::error_code ec;
        if (std::filesystem::is_directory(entry, ec))
            continue; // nFileSizeLow is 0
        const auto size = std::filesystem::file_size(entry, ec);
        if (ec || (size & 0xFFFFFFFFu) == 0)
            continue;
        const u16 file = entry.filename().u16string();
        const std::size_t dot = file.rfind(u'.');
        const u16 ext = lowered(dot == u16::npos ? u16v(file) : u16v(file).substr(dot + 1), lower);
        if (ext != u"ttf" && ext != u"otf" && ext != u"ttc" && ext != u"pfb")
            continue;
        std::ifstream in(entry, std::ios::binary);
        if (!in)
            continue;
        auto bytes = std::make_shared<std::vector<std::byte>>();
        std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bytes->resize(raw.size());
        std::transform(raw.begin(), raw.end(), bytes->begin(), [](char c) { return std::byte(c); });
        out.fonts.push_back({toUtf8(u16(path) + file), std::move(bytes)});
    }
    return out;
}

} // namespace hikari::application
