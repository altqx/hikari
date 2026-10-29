#pragma once

#include "hikari/core/document.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// Lowers one UTF-16 unit; the caller maps every letter.
using FontNameLower = std::function<char16_t(char16_t)>;

// <0, 0 or >0 by the lowered units.
int compareNoCase(std::u16string_view a, std::u16string_view b, const FontNameLower &lower);
bool sameNoCase(std::u16string_view a, std::u16string_view b, const FontNameLower &lower);

enum class CatalogOrder { Utf16, Utf32 }; // HikariSub legacy's catalog order on Windows / Linux
#ifdef _WIN32
inline constexpr CatalogOrder kCatalogOrder = CatalogOrder::Utf16;
#else
inline constexpr CatalogOrder kCatalogOrder = CatalogOrder::Utf32;
#endif

inline constexpr const char16_t *kFontCatalogsFile = u"FontCatalogs.txt";
std::u16string fontCatalogsAutosaveFile(int n);

class FontCatalogs {
public:
    struct Catalog {
        std::u16string name;
        std::vector<std::u16string> fonts;
    };

    explicit FontCatalogs(FontNameLower lower, CatalogOrder order = kCatalogOrder);

    // Merges in a file's catalogs: a line ending in '{' that does not start with a tab opens the
    // catalog named before its last '=', "}" closes it, the trimmed lines between are its fonts (each
    // once); other lines are ignored.
    void load(std::u16string_view text);
    // Merges these in, as load(text) does.
    void load(const std::vector<Catalog> &catalogs);
    // A file's catalogs in first-appearance order, leaving these alone; a name given twice gathers
    // both blocks' fonts.
    static std::vector<Catalog> parse(std::u16string_view text);
    // Merge adds the fonts the catalog lacks; Replace gives it the file's fonts; Rename adds them
    // under `newName` (merged when that exists); Skip leaves the catalog out.
    enum class LoadAction { Merge, Replace, Rename, Skip };
    struct LoadChoice {
        LoadAction action = LoadAction::Merge;
        std::u16string newName;
    };
    // `file` from parse(); choices[i] for file[i], Merge past their end.
    void apply(const std::vector<Catalog> &file, const std::vector<LoadChoice> &choices);
    // UTF-8 BOM, then per catalog "Name={", tab-indented fonts and "}", CRLF line ends; empty
    // without catalogs.
    std::string serialize() const;

    // In the order catalogs were added; a rename puts the new name in the old one's place.
    const std::vector<std::u16string> &names() const { return m_names; }
    // Sorted by the CatalogOrder.
    const std::vector<Catalog> &catalogs() const { return m_catalogs; }
    const Catalog *find(std::u16string_view name) const;
    // Ignoring case (sameNoCase); nullptr for none.
    const Catalog *findNoCase(std::u16string_view name) const;

    // The first catalog holding `font` exactly; empty for none.
    std::u16string catalogOf(std::u16string_view font) const;
    // Sorts the catalog's fonts in place by compareNoCase, so the next save writes them sorted;
    // nullptr for none.
    const std::vector<std::u16string> *sortedFonts(std::u16string_view catalog);
    // Exact match.
    bool contains(std::u16string_view catalog, std::u16string_view font) const;

    // A new empty catalog unless the name exists; the name joins names() if missing. True when created.
    bool addCatalog(std::u16string_view name);
    // When `newName` exists, `ask` decides: Merge adds the old catalog's fonts, Delete replaces its
    // fonts with them, Cancel changes nothing (false). The new name takes the old one's place in
    // names(), or stays where it is when already listed.
    enum class Clash { Merge, Delete, Cancel };
    bool rename(std::u16string_view oldName, std::u16string_view newName, const std::function<Clash()> &ask);
    void remove(std::u16string_view name);
    // Exact names, no duplicate.
    void addFont(std::u16string_view catalog, std::u16string_view font);
    void removeFont(std::u16string_view catalog, std::u16string_view font);

    // With `clear` the catalog is emptied first; each Style's font and each non-comment Line's \fn
    // (the translation when it has one), up to its first \p, joins unless held ignoring case.
    // False when there is no such catalog.
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

// nullopt when the file cannot be read or reads as nothing.
std::optional<std::u16string> readFontCatalogFile(const std::filesystem::path &path);
// Makes the folder when missing and replaces the file; false when it could not be written.
bool writeFontCatalogFile(const std::filesystem::path &path, const std::string &bytes);

} // namespace hikari::application
