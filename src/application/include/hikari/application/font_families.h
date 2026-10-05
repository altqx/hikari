#pragma once

// Y6: the font picker's family lists over the FontService (legacy
// FontEnumerator, FontDialog's FontList and the catalog choice of FontDialog
// and StyleChange, at 20d647c4). Legacy asked GDI (and on Linux its
// fontconfig shim, platform.h:1133) for the installed families; here the
// FontService answers through the provider the renderer uses (fonts.md,
// ADR 0007), with the EXTERNAL_FONTS_DIRECTORY files added as the renderer
// gets them.

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

// The characters FontEnumerator::CheckGlyphsExists checks: on Windows each
// UTF-16 unit that is not a surrogate (GetGlyphIndicesW; a character outside
// the BMP is never checked); on Linux every character (wchar_t is UTF-32;
// FcCharSetHasChar), U+0000 included and never present.
std::u32string filterCharacters(std::u16string_view filter, FontListPlatform platform = kFontListPlatform);

// FontEnumerator: Fonts and FilteredFonts.
class FontFamilies {
public:
    FontFamilies(FontServicePort &fonts, FontNameLower lower, FontListPlatform platform = kFontListPlatform);

    // The fonts listed with the system's: the environment's external fonts.
    void setEnvironment(FontEnvironment environment) { m_environment = std::move(environment); }
    const FontEnvironment &environment() const { return m_environment; }

    // GetFonts: every family, enumerated when the list is empty.
    const std::vector<std::u16string> &fonts();
    // GetFilteredFonts(filter): the families with a glyph for every filter
    // character. The first call enumerates both lists with `filter`; later
    // calls only remember `filter` for the next enumeration (legacy keeps
    // the list it has).
    const std::vector<std::u16string> &filteredFonts(std::u16string_view filter);
    // EnumerateFonts(true) after a font change: both lists again (the
    // filtered one only once it exists), with the last filter given.
    void enumerate();
    // The external font files no face was read from (legacy
    // AddFontResourceExW failed: "Cannot add external font file %s.").
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

// The family a face is listed under (SystemFace::listedFamily, else its
// first family name).
std::u16string listedFamily(const SystemFace &face);

// The catalog choice of FontDialog and StyleChange: the catalog names with
// "All fonts" inserted at 0 and "Without catalog" at 1 through
// HikariChoice::Insert, which clamps the position to the last index: with
// no catalog "Without catalog" comes first (ListControls.cpp:685-688).
std::vector<std::u16string> catalogChoices(const std::vector<std::u16string> &names, const std::u16string &allFonts,
                                           const std::u16string &withoutCatalog);

// How "Without catalog" looks a catalog font up in the list it removes it
// from: FontDialog's FontList::FindString always answers 0, so each catalog
// entry removes the list's first font (FontDialog.cpp:351-354, 668-679);
// StyleChange's HikariChoice::FindString(font, true) finds it exactly.
enum class WithoutCatalogLookup { FirstEntry, Exact };

// ChangeCatalog's list for the choice's selection `selection` (the
// choice's value `value`): 0 every font of `fonts`; 1 those fonts without
// the catalogs' (as `lookup` finds them); otherwise the catalog named
// `value`, sorted (GetCatalogFonts) and kept where `fonts` has it exactly.
std::vector<std::u16string> catalogView(int selection, std::u16string_view value,
                                        const std::vector<std::u16string> &fonts, FontCatalogs &catalogs,
                                        WithoutCatalogLookup lookup);

// FontList::SetSelectionByPartialName (FontDialog.cpp:267-311): the index
// selected for `partial` (0 for an empty text; -1 for an empty list).
int fontListPartialIndex(const std::vector<std::u16string> &fonts, std::u16string_view partial,
                         const FontNameLower &lower);
// FontList::SetSelectionByName: the font equal ignoring case, else the
// partial-name selection.
int fontListNameIndex(const std::vector<std::u16string> &fonts, std::u16string_view name, const FontNameLower &lower);
// FontCatalogList::SetSelectionByPartialName (FontCatalogList.cpp:262-311):
// as the font dialog's, but an empty text selects the first row too and
// nothing is clamped.
int catalogListPartialIndex(const std::vector<std::u16string> &fonts, std::u16string_view partial,
                            const FontNameLower &lower);

// What the renderer selected for the font dialog's family (the
// "substituted face the font dialog reports"): the requested family's own
// face, a face of other names the provider substituted, a later stage
// (default family, fallback) or nothing.
struct PickerResolution {
    enum class Kind { Requested, Substituted, Fallback, Missing };
    Kind kind = Kind::Missing;
    std::string family;  // the selected face's first family name
    std::string file;    // its file name, or the attachment / external file
    long faceIndex = 0;
    bool emboldened = false, italicized = false;
};
PickerResolution pickerResolution(const FontReport &report);

// EXTERNAL_FONTS_DIRECTORY (FontEnumerator::LoadExternalFontsToProcess,
// FontEnumerator.cpp:621-665): FindFirstFileW(path + "*"), every entry in
// the platform's listing order (legacy_dir.h) whose size is not zero (its
// low 32 bits; folders have none) and whose extension (the text after the
// last '.', or the whole name without one) is ttf, otf, ttc or pfb ignoring
// case. Each file's full path is the setting's text followed by its name.
struct ExternalFontsLoad {
    bool opened = false; // false: "Cannot load external font folder"
    std::vector<FontAttachment> fonts; // name: the full path; bytes read
};
ExternalFontsLoad loadExternalFonts(std::u16string_view path, const FontNameLower &lower,
                                    FontListPlatform platform = kFontListPlatform);

} // namespace hikari::application
