#pragma once

// Y6: Font catalogs (legacy FontCatalogManagement, FontCatalogList.cpp at
// 20d647c4). A Font catalog is a named list of font-family names; the
// catalogs live in <settings>/FontCatalogs.txt and the management dialog's
// safety copies FontCatalogsAutosave0..2.txt, in legacy's grammar:
//
//     Name={<CR><LF>
//     <TAB>Family<CR><LF>
//     }<CR><LF>
//
// one block per catalog in the order of legacy's std::map<wxString, ...>
// (wxString::operator<: UTF-16 code units on Windows, code points on Linux,
// R5-per-platform), the file starting with a UTF-8 BOM (OpenWrite's
// PartFileWrite through wxFile::Write's wxConvAuto, which writes UTF-8). No
// catalog writes an empty file. Reading is OpenWrite::FileOpen(path, text,
// false): wxFFile::ReadAll through wxConvAuto (decodeSessionBytes).
//
// Names are kept as written; family names that no font answers to are kept.

#include "hikari/core/document.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// towlower per UTF-16 unit, as wxString::CmpNoCase and IsSameAs(s, false)
// used it. U1-unicode-case: the caller maps every letter.
using FontNameLower = std::function<char16_t(char16_t)>;

// wxString::CmpNoCase: <0, 0 or >0 by the lowered units.
int compareNoCase(std::u16string_view a, std::u16string_view b, const FontNameLower &lower);
// wxString::IsSameAs(b, false): equal lengths and CmpNoCase == 0.
bool sameNoCase(std::u16string_view a, std::u16string_view b, const FontNameLower &lower);

enum class CatalogOrder { Utf16, Utf32 }; // std::map<wxString> on Windows / Linux
#ifdef _WIN32
inline constexpr CatalogOrder kCatalogOrder = CatalogOrder::Utf16;
#else
inline constexpr CatalogOrder kCatalogOrder = CatalogOrder::Utf32;
#endif

// "FontCatalogs.txt" and "FontCatalogsAutosave<n>.txt" (FontCatalogList.cpp:573, 203).
inline constexpr const char16_t *kFontCatalogsFile = u"FontCatalogs.txt";
std::u16string fontCatalogsAutosaveFile(int n);

class FontCatalogs {
public:
    struct Catalog {
        std::u16string name;
        std::vector<std::u16string> fonts;
    };

    explicit FontCatalogs(FontNameLower lower, CatalogOrder order = kCatalogOrder);

    // LoadCatalogs over a file's decoded text (FontCatalogList.cpp:567-613):
    // merged into the catalogs already held. A line ending in '{' that does
    // not start with a tab opens the catalog named before its last '=' (an
    // empty name without one), "}" closes it, and the lines between, trimmed,
    // are its fonts (each once, exactly); other lines are ignored.
    void load(std::u16string_view text);
    // SaveCatalogs (FontCatalogList.cpp:615-629): the file's bytes.
    std::string serialize() const;

    // fontCatalogsNames: the catalog choices, in the order catalogs were
    // added (a rename puts the new name in the old one's place).
    const std::vector<std::u16string> &names() const { return m_names; }
    // The catalogs in legacy's map order.
    const std::vector<Catalog> &catalogs() const { return m_catalogs; }
    const Catalog *find(std::u16string_view name) const;

    // FindCatalogByFont: the first catalog (map order) holding `font`
    // exactly; empty for none.
    std::u16string catalogOf(std::u16string_view font) const;
    // GetCatalogFonts: the catalog's fonts, first sorted in place by
    // CmpNoCase (so the next save writes them sorted). nullptr for none.
    const std::vector<std::u16string> *sortedFonts(std::u16string_view catalog);
    // IsFontInCatalog: exact match.
    bool contains(std::u16string_view catalog, std::u16string_view font) const;

    // AddCatalog: a new empty catalog unless one of that name exists; the
    // name joins the choices if it is not among them. True when created.
    bool addCatalog(std::u16string_view name);
    // ChangeCatalogName (FontCatalogList.cpp:739-776). When `newName`
    // exists, `ask` answers "Catalog named \"%s\" already exists. What to
    // do?": Merge adds the old catalog's fonts to it, Delete replaces its
    // fonts with the old catalog's, Cancel changes nothing (false). The old
    // catalog goes; the new name takes the old one's place in the choices,
    // or stays where it is when already listed (Y6-rename-listed: legacy
    // dropped the old name and listed the new one only after a restart).
    enum class Clash { Merge, Delete, Cancel };
    bool rename(std::u16string_view oldName, std::u16string_view newName, const std::function<Clash()> &ask);
    // RemoveCatalog.
    void remove(std::u16string_view name);
    // AddCatalogFont / RemoveCatalogFont: exact names, no duplicate.
    void addFont(std::u16string_view catalog, std::u16string_view font);
    void removeFont(std::u16string_view catalog, std::u16string_view font);

    // CollectFontsFromSubtitles (FontCatalogList.cpp:907-979) over the
    // Documents in order (the active tab's, or every tab's from the
    // first): with `clear` the catalog is emptied first; each Style's font
    // and each \fn of a Line that is not a comment (its translation when it
    // has one), up to its first \p tag, join the catalog unless it holds
    // the name ignoring case. False when there is no such catalog.
    bool collect(std::u16string_view catalog, bool clear, const std::vector<const core::Document *> &documents);

private:
    Catalog *findMutable(std::u16string_view name);
    Catalog &insert(std::u16string_view name);
    bool less(std::u16string_view a, std::u16string_view b) const;

    FontNameLower m_lower;
    CatalogOrder m_order;
    std::vector<Catalog> m_catalogs;
    std::vector<std::u16string> m_names;
};

// OpenWrite::FileOpen(path, &text, false) on a catalog file: nullopt when it
// cannot be read or reads as nothing.
std::optional<std::u16string> readFontCatalogFile(const std::filesystem::path &path);
// SaveCatalogs' OpenWrite(path): the folder is made when missing and the file
// replaced with `bytes`. False when it could not be written.
bool writeFontCatalogFile(const std::filesystem::path &path, const std::string &bytes);

} // namespace hikari::application
