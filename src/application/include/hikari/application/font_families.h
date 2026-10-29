#pragma once

#include "hikari/application/font_catalogs.h"
#include "hikari/application/font_service.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

enum class FontListPlatform { Windows, Linux };
#ifdef _WIN32
inline constexpr FontListPlatform kFontListPlatform = FontListPlatform::Windows;
#else
inline constexpr FontListPlatform kFontListPlatform = FontListPlatform::Linux;
#endif

// Windows checks UTF-16 units, skipping surrogates (GetGlyphIndicesW); Linux
// every code point, U+0000 never present.
std::u32string filterCharacters(std::u16string_view filter, FontListPlatform platform = kFontListPlatform);

class FontFamilies {
public:
    FontFamilies(FontServicePort &fonts, FontNameLower lower, FontListPlatform platform = kFontListPlatform);

    void setEnvironment(FontEnvironment environment) { m_environment = std::move(environment); }
    const FontEnvironment &environment() const { return m_environment; }

    // Enumerated when the list is empty.
    const std::vector<std::u16string> &fonts();
    // Families with a glyph for every filter character. Only the first call
    // enumerates; later ones keep `filter` for the next enumeration.
    const std::vector<std::u16string> &filteredFonts(std::u16string_view filter);
    // After a font change: both lists again, with the last filter.
    void enumerate();
    // Unlike filteredFonts, `filter` applies now.
    void refilter(std::u16string_view filter);
    std::vector<std::string> unreadableExternalFonts();

private:
    FontServicePort &m_fonts;
    FontNameLower m_lower;
    FontListPlatform m_platform;
    FontEnvironment m_environment;
    std::vector<std::u16string> m_all;
    std::optional<std::vector<std::u16string>> m_filtered;
    std::u16string m_filter;
};

// SystemFace::listedFamily, else its first family name.
std::u16string listedFamily(const SystemFace &face);

// MUST KILL: with no catalog, "Without catalog" comes before "All fonts"
std::vector<std::u16string> catalogChoices(const std::vector<std::u16string> &names, const std::u16string &allFonts,
                                           const std::u16string &withoutCatalog);

// 0: every font; 1: fonts in no catalog; else the catalog named `value`,
// sorted. Catalog fonts match `fonts` exactly.
std::vector<std::u16string> catalogView(int selection, std::u16string_view value,
                                        const std::vector<std::u16string> &fonts, FontCatalogs &catalogs);

// 0 for an empty text; -1 for an empty list.
int fontListPartialIndex(const std::vector<std::u16string> &fonts, std::u16string_view partial,
                         const FontNameLower &lower);
// The font equal ignoring case, else the partial-name index.
int fontListNameIndex(const std::vector<std::u16string> &fonts, std::u16string_view name, const FontNameLower &lower);
// As fontListPartialIndex, but nothing is clamped.
int catalogListPartialIndex(const std::vector<std::u16string> &fonts, std::u16string_view partial,
                            const FontNameLower &lower);

// What the renderer selected for the font dialog's family.
struct PickerResolution {
    enum class Kind { Requested, Substituted, Fallback, Missing };
    Kind kind = Kind::Missing;
    std::string family;  // the selected face's first family name
    std::string file;    // file name, attachment or external file
    long faceIndex = 0;
    bool emboldened = false, italicized = false;
};
PickerResolution pickerResolution(const FontReport &report);

// Non-empty ttf, otf, ttc or pfb files in listing order; each full path is
// `path` followed by the name, no separator added.
struct ExternalFontsLoad {
    bool opened = false;
    std::vector<FontAttachment> fonts; // name: the full path
};
ExternalFontsLoad loadExternalFonts(std::u16string_view path, const FontNameLower &lower,
                                    FontListPlatform platform = kFontListPlatform);

} // namespace hikari::application
