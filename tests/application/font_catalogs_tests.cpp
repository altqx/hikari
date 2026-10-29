#include "hikari/application/font_catalogs.h"
#include "hikari/application/font_families.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace hikari;
using namespace hikari::application;
namespace fs = std::filesystem;

namespace {
char16_t lowerChar(char16_t c)
{
    if (c >= u'A' && c <= u'Z')
        return c + 32;
    static const std::map<char16_t, char16_t> latin{{u'Ł', u'ł'}, {u'Ó', u'ó'}, {u'Ź', u'ź'}, {u'Ą', u'ą'},
                                                    {u'Ę', u'ę'}, {u'Ś', u'ś'}, {u'Ć', u'ć'}};
    const auto it = latin.find(c);
    return it == latin.end() ? c : it->second;
}
const FontNameLower kLower = lowerChar;

std::string readBytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

fs::path fixture(const char *name)
{
    return fs::path(HIKARI_FONT_CATALOG_FIXTURES) / name;
}

std::vector<std::u16string> fontsOf(const FontCatalogs &c, std::u16string_view name)
{
    const auto *catalog = c.find(name);
    return catalog ? catalog->fonts : std::vector<std::u16string>{u"<none>"};
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return core::loadAss(b).document;
}

fs::path tempDir(const char *name)
{
    const fs::path dir = fs::temp_directory_path() / ("hikari-y6-" + std::string(name));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

struct ListedFace {
    std::string family;
    std::u32string glyphs;
};
class ListingFonts final : public FontServicePort {
public:
    std::vector<ListedFace> faces;
    int listings = 0;
    std::vector<std::u32string> checked;
    std::expected<FontReport, FontError> resolve(const FontEnvironment &, const std::vector<FontRequest> &) override
    {
        return std::unexpected(FontError::RendererUnavailable);
    }
    std::vector<SystemFace> systemFaces() override
    {
        std::vector<SystemFace> out;
        for (const auto &f : faces) {
            SystemFace s;
            s.families = {f.family, f.family + " Alias"};
            out.push_back(s);
        }
        return out;
    }
    std::vector<SystemFace> pickerFaces(const FontEnvironment &env) override
    {
        ++listings;
        auto out = systemFaces();
        for (const auto &font : env.externalFonts) {
            SystemFace s;
            s.families = {"External " + fs::path(font.name).filename().string()};
            s.externalFile = font.name;
            out.push_back(s);
        }
        return out;
    }
    std::vector<bool> facesCover(const std::vector<SystemFace> &list, const FontEnvironment &,
                                 const std::u32string &characters) override
    {
        checked.push_back(characters);
        std::vector<bool> out;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto &glyphs = i < faces.size() ? faces[i].glyphs : std::u32string();
            bool all = true;
            for (const char32_t c : characters)
                all = all && (glyphs.empty() || glyphs.find(c) != std::u32string::npos);
            out.push_back(all);
        }
        return out;
    }
    std::expected<FontCollection, FontError> collect(const std::vector<std::byte> &, const FontEnvironment &,
                                                     const std::atomic<bool> *) override
    {
        return std::unexpected(FontError::RendererUnavailable);
    }
    std::expected<ReimportCheck, FontError> verifyReimport(const std::vector<std::byte> &, const FontCollection &,
                                                           const std::string &) override
    {
        return std::unexpected(FontError::RendererUnavailable);
    }
};

}

TEST(FontCatalogs, ALegacyFileIsReadAndWrittenBackUnchanged)
{
    const std::string bytes = readBytes(fixture("FontCatalogs.txt"));
    const auto text = readFontCatalogFile(fixture("FontCatalogs.txt"));
    ASSERT_TRUE(text);
    FontCatalogs c(kLower);
    c.load(*text);
    EXPECT_EQ(c.names(), (std::vector<std::u16string>{u"Anime", u"Napisy", u"Ąęść"}));
    EXPECT_EQ(fontsOf(c, u"Anime"), (std::vector<std::u16string>{u"Arial", u"Gandhi Sans", u"Łódź Sans"}));
    EXPECT_EQ(c.serialize(), bytes);
    EXPECT_EQ(readBytes(fixture("FontCatalogs.txt")), bytes);
}

TEST(FontCatalogs, SaveWritesTheLegacyGrammarInMapOrder)
{
    FontCatalogs c(kLower);
    EXPECT_EQ(c.serialize(), "");
    c.addCatalog(u"b");
    c.addCatalog(u"B");
    c.addCatalog(u"a");
    c.addFont(u"b", u"Font Ż");
    c.addFont(u"b", u"Font Ż");
    c.addFont(u"missing", u"x");
    EXPECT_EQ(c.names(), (std::vector<std::u16string>{u"b", u"B", u"a"}));
    EXPECT_EQ(c.serialize(), "\xEF\xBB\xBF" "B={\r\n}\r\na={\r\n}\r\nb={\r\n\tFont \xC5\xBB\r\n}\r\n");
}

TEST(FontCatalogs, MapOrderFollowsThePlatformsWideCharacters)
{
    for (const auto order : {CatalogOrder::Utf16, CatalogOrder::Utf32}) {
        FontCatalogs c(kLower, order);
        c.addCatalog(u"�");
        c.addCatalog(u"\U0001F600");
        const bool emojiFirst = c.catalogs().front().name == u"\U0001F600";
        EXPECT_EQ(emojiFirst, order == CatalogOrder::Utf16);
    }
}

TEST(FontCatalogs, LoadReadsWhatLegacyRead)
{
    const auto text = readFontCatalogFile(fixture("quirks.txt"));
    ASSERT_TRUE(text);
    FontCatalogs c(kLower);
    c.load(*text);
    EXPECT_EQ(c.names(), (std::vector<std::u16string>{u"  Spaced ", u"", u"Second", u"Empty"}));
    EXPECT_EQ(fontsOf(c, u"  Spaced "), (std::vector<std::u16string>{u"Arial", u"ARIAL"}));
    EXPECT_EQ(fontsOf(c, u""), (std::vector<std::u16string>{u"Verdana"}));
    EXPECT_EQ(fontsOf(c, u"Second"), (std::vector<std::u16string>{u"Third={"}));
    EXPECT_EQ(fontsOf(c, u"Empty"), std::vector<std::u16string>{});
    EXPECT_EQ(c.serialize(), "\xEF\xBB\xBF={\r\n\tVerdana\r\n}\r\n  Spaced ={\r\n\tArial\r\n\tARIAL\r\n}\r\n"
                             "Empty={\r\n}\r\nSecond={\r\n\tThird={\r\n}\r\n");
    FontCatalogs cased(kLower);
    cased.load(u"A={\n\tArial\n}\na={\n\tTahoma\n}\n");
    EXPECT_EQ(cased.names(), (std::vector<std::u16string>{u"A", u"a"}));
    EXPECT_EQ(fontsOf(cased, u"A"), std::vector<std::u16string>{u"Arial"});
    EXPECT_EQ(fontsOf(cased, u"a"), std::vector<std::u16string>{u"Tahoma"});
}

TEST(FontCatalogs, FilesAreDecodedAsWxConvAutoDid)
{
    FontCatalogs c(kLower);
    c.load(*readFontCatalogFile(fixture("latin1.txt")));
    c.load(*readFontCatalogFile(fixture("utf16.txt")));
    EXPECT_EQ(fontsOf(c, u"Polski"), (std::vector<std::u16string>{u"Garamond ó"}));
    EXPECT_EQ(fontsOf(c, u"UTF16"), (std::vector<std::u16string>{u"MS Gothic"}));
    const fs::path dir = tempDir("decode");
    EXPECT_FALSE(readFontCatalogFile(dir / "missing.txt"));
    std::ofstream(dir / "empty.txt").close();
    EXPECT_FALSE(readFontCatalogFile(dir / "empty.txt"));
}

TEST(FontCatalogs, LoadingAnotherFileMerges)
{
    FontCatalogs c(kLower);
    c.load(*readFontCatalogFile(fixture("FontCatalogs.txt")));
    c.load(u"Anime={\n\tArial\n\tImpact\n}\nZeta={\n\tTahoma\n}\n");
    EXPECT_EQ(fontsOf(c, u"Anime"), (std::vector<std::u16string>{u"Arial", u"Gandhi Sans", u"Łódź Sans", u"Impact"}));
    EXPECT_EQ(c.names().back(), u"Zeta");
}

TEST(FontCatalogs, LoadingAFileCanReplaceRenameOrSkipEachCatalog)
{
    const auto file = FontCatalogs::parse(u"junk\nAnime={\n\tArial\n\tImpact\n}\nZeta={\n\tTahoma\n}\nAnime={\n\tArial\n\tVerdana\n}\n");
    ASSERT_EQ(file.size(), 2u);
    EXPECT_EQ(file[0].name, u"Anime");
    EXPECT_EQ(file[0].fonts, (std::vector<std::u16string>{u"Arial", u"Impact", u"Verdana"}));
    EXPECT_EQ(file[1].name, u"Zeta");

    using A = FontCatalogs::LoadAction;
    FontCatalogs c(kLower);
    c.load(*readFontCatalogFile(fixture("FontCatalogs.txt")));
    FontCatalogs merged = c, replaced = c, renamed = c, skipped = c;
    merged.apply(file, {});
    EXPECT_EQ(fontsOf(merged, u"Anime"),
              (std::vector<std::u16string>{u"Arial", u"Gandhi Sans", u"Łódź Sans", u"Impact", u"Verdana"}));
    replaced.apply(file, {{A::Replace, {}}});
    EXPECT_EQ(fontsOf(replaced, u"Anime"), (std::vector<std::u16string>{u"Arial", u"Impact", u"Verdana"}));
    renamed.apply(file, {{A::Rename, u"Anime (2)"}, {A::Skip, {}}});
    EXPECT_EQ(fontsOf(renamed, u"Anime"), fontsOf(c, u"Anime"));
    EXPECT_EQ(fontsOf(renamed, u"Anime (2)"), (std::vector<std::u16string>{u"Arial", u"Impact", u"Verdana"}));
    EXPECT_EQ(renamed.find(u"Zeta"), nullptr);
    EXPECT_EQ(renamed.names().back(), u"Anime (2)");
    skipped.apply(file, {{A::Skip, {}}, {A::Merge, {}}});
    EXPECT_EQ(fontsOf(skipped, u"Anime"), fontsOf(c, u"Anime"));
    EXPECT_EQ(fontsOf(skipped, u"Zeta"), (std::vector<std::u16string>{u"Tahoma"}));
    FontCatalogs onto = c;
    onto.apply({file[1]}, {{A::Rename, u"Anime"}});
    EXPECT_EQ(fontsOf(onto, u"Anime").back(), u"Tahoma");
    EXPECT_EQ(c.findNoCase(u"ANIME"), c.find(u"Anime"));
    EXPECT_EQ(c.findNoCase(u"Nothing"), nullptr);
    FontCatalogs cased = c;
    cased.apply(FontCatalogs::parse(u"anime={\n\tTahoma\n}\n"), {});
    EXPECT_EQ(cased.find(u"anime"), nullptr);
    EXPECT_EQ(fontsOf(cased, u"Anime").back(), u"Tahoma");
    FontCatalogs renamedCased = c;
    renamedCased.apply({file[1]}, {{A::Rename, u"ANIME"}});
    EXPECT_EQ(renamedCased.find(u"ANIME"), nullptr);
    EXPECT_EQ(fontsOf(renamedCased, u"Anime").back(), u"Tahoma");
    FontCatalogs replacedCased = c;
    replacedCased.apply(FontCatalogs::parse(u"ANIME={\n\tArial\n}\nanime={\n\tTahoma\n}\n"), {{A::Replace, {}}, {A::Replace, {}}});
    EXPECT_EQ(fontsOf(replacedCased, u"Anime"), (std::vector<std::u16string>{u"Arial", u"Tahoma"}));
}

TEST(FontCatalogs, FilesAreWrittenWhereLegacyWroteThem)
{
    const fs::path dir = tempDir("write") / "Config";
    FontCatalogs c(kLower);
    ASSERT_TRUE(writeFontCatalogFile(dir / kFontCatalogsFile, c.serialize()));
    EXPECT_EQ(readBytes(dir / kFontCatalogsFile), "");
    c.addCatalog(u"x");
    ASSERT_TRUE(writeFontCatalogFile(dir / kFontCatalogsFile, c.serialize()));
    EXPECT_EQ(readBytes(dir / kFontCatalogsFile), "\xEF\xBB\xBFx={\r\n}\r\n");
    EXPECT_EQ(fontCatalogsAutosaveFile(2), u"FontCatalogsAutosave2.txt");
}

TEST(FontCatalogs, RenameMovesTheFontsAndAsksWhenTheNameIsTaken)
{
    FontCatalogs c(kLower);
    c.load(u"A={\n\tArial\n\tImpact\n}\nB={\n\tImpact\n\tTahoma\n}\nC={\n\tVerdana\n}\n");
    int asked = 0;
    const auto answer = [&](FontCatalogs::Clash clash) {
        return [&asked, clash] {
            ++asked;
            return clash;
        };
    };
    EXPECT_TRUE(c.rename(u"C", u"D", answer(FontCatalogs::Clash::Cancel)));
    EXPECT_EQ(asked, 0);
    EXPECT_EQ(fontsOf(c, u"D"), (std::vector<std::u16string>{u"Verdana"}));
    EXPECT_FALSE(c.find(u"C"));
    EXPECT_EQ(c.names(), (std::vector<std::u16string>{u"A", u"B", u"D"}));
    FontCatalogs first = c;
    EXPECT_TRUE(first.rename(u"A", u"Z", answer(FontCatalogs::Clash::Cancel)));
    EXPECT_EQ(first.names(), (std::vector<std::u16string>{u"Z", u"B", u"D"}));
    EXPECT_FALSE(c.rename(u"A", u"B", answer(FontCatalogs::Clash::Cancel)));
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(fontsOf(c, u"A"), (std::vector<std::u16string>{u"Arial", u"Impact"}));
    FontCatalogs merged = c;
    EXPECT_TRUE(merged.rename(u"A", u"B", answer(FontCatalogs::Clash::Merge)));
    EXPECT_EQ(fontsOf(merged, u"B"), (std::vector<std::u16string>{u"Impact", u"Tahoma", u"Arial"}));
    EXPECT_FALSE(merged.find(u"A"));
    EXPECT_EQ(merged.names(), (std::vector<std::u16string>{u"B", u"D"}));
    EXPECT_TRUE(c.rename(u"A", u"B", answer(FontCatalogs::Clash::Delete)));
    EXPECT_EQ(fontsOf(c, u"B"), (std::vector<std::u16string>{u"Arial", u"Impact"}));
    EXPECT_EQ(c.serialize(), "\xEF\xBB\xBF" "B={\r\n\tArial\r\n\tImpact\r\n}\r\nD={\r\n\tVerdana\r\n}\r\n");
    EXPECT_TRUE(c.rename(u"nothing", u"E", answer(FontCatalogs::Clash::Cancel)));
    EXPECT_EQ(fontsOf(c, u"E"), std::vector<std::u16string>{});
    EXPECT_EQ(c.names(), (std::vector<std::u16string>{u"B", u"D", u"E"}));
}

TEST(FontCatalogs, RenamingOntoItsOwnNameKeepsTheFonts)
{
    FontCatalogs c(kLower);
    c.load(u"A={\n\tArial\n}\n");
    EXPECT_TRUE(c.rename(u"A", u"A", [] { return FontCatalogs::Clash::Merge; }));
    EXPECT_EQ(fontsOf(c, u"A"), (std::vector<std::u16string>{u"Arial"}));
    EXPECT_EQ(c.names(), std::vector<std::u16string>{u"A"});
}

TEST(FontCatalogs, LookupsAndTheSortedCatalog)
{
    FontCatalogs c(kLower);
    c.load(u"Z={\n\tbeta\n\tAlpha\n\tarial\n}\nM={\n\tAlpha\n}\n");
    EXPECT_EQ(c.catalogOf(u"Alpha"), u"M");
    EXPECT_EQ(c.catalogOf(u"alpha"), u"");
    EXPECT_TRUE(c.contains(u"Z", u"beta"));
    EXPECT_FALSE(c.contains(u"Z", u"Beta"));
    EXPECT_EQ(*c.sortedFonts(u"Z"), (std::vector<std::u16string>{u"Alpha", u"arial", u"beta"}));
    EXPECT_EQ(c.serialize(), "\xEF\xBB\xBFM={\r\n\tAlpha\r\n}\r\nZ={\r\n\tAlpha\r\n\tarial\r\n\tbeta\r\n}\r\n");
    EXPECT_EQ(c.sortedFonts(u"none"), nullptr);
    c.removeFont(u"Z", u"arial");
    c.remove(u"M");
    EXPECT_EQ(c.names(), (std::vector<std::u16string>{u"Z"}));
    EXPECT_EQ(c.catalogOf(u"Alpha"), u"Z");
}

TEST(FontCatalogs, CollectingFromSubtitlesAddsStylesAndFnTags)
{
    const auto doc = load("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                          "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
                          "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
                          "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                          "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,"
                          "2,2,2,10,10,10,1\n"
                          "Style: Sign,ARIAL,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,"
                          "2,2,2,10,10,10,1\n\n"
                          "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\fnImpact}a{\\fnGandhi Sans\\b1}b\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p0}{\\fnAfterP}x\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x{\\fnLater\\p1}m 0 0\n"
                          "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\fnCommented}x\n");
    FontCatalogs c(kLower);
    c.load(u"Mine={\n\tOld\n\timpact\n}\n");
    EXPECT_FALSE(c.collect(u"none", false, {&doc}));
    ASSERT_TRUE(c.collect(u"Mine", false, {&doc}));
    EXPECT_EQ(fontsOf(c, u"Mine"),
              (std::vector<std::u16string>{u"Old", u"impact", u"Arial", u"Gandhi Sans", u"Later"}));
    ASSERT_TRUE(c.collect(u"Mine", true, {&doc, &doc}));
    EXPECT_EQ(fontsOf(c, u"Mine"), (std::vector<std::u16string>{u"Arial", u"Impact", u"Gandhi Sans", u"Later"}));
}

TEST(FontPicker, TheCatalogChoiceInsertsItsTwoEntriesAsLegacyDid)
{
    EXPECT_EQ(catalogChoices({}, u"All fonts", u"Without catalog"),
              (std::vector<std::u16string>{u"Without catalog", u"All fonts"}));
    EXPECT_EQ(catalogChoices({u"A"}, u"All fonts", u"Without catalog"),
              (std::vector<std::u16string>{u"All fonts", u"Without catalog", u"A"}));
}

TEST(FontPicker, CatalogViewsOfTheFontList)
{
    FontCatalogs c(kLower);
    c.load(u"A={\n\tVerdana\n\tArial\n\tNot Installed\n}\nB={\n\tImpact\n}\n");
    const std::vector<std::u16string> fonts{u"Arial", u"Comic Sans MS", u"Impact", u"Tahoma", u"Verdana"};
    EXPECT_EQ(catalogView(0, u"All fonts", fonts, c), fonts);
    EXPECT_EQ(catalogView(1, u"", fonts, c), (std::vector<std::u16string>{u"Comic Sans MS", u"Tahoma"}));
    EXPECT_EQ(catalogView(2, u"A", fonts, c), (std::vector<std::u16string>{u"Arial", u"Verdana"}));
    EXPECT_EQ(catalogView(3, u"none", fonts, c), std::vector<std::u16string>{});
}

TEST(FontPicker, PartialNamesSelectAsLegacyDid)
{
    const std::vector<std::u16string> fonts{u"Arial", u"Arial Black", u"Calibri", u"Cambria", u"Impact", u"Tahoma"};
    EXPECT_EQ(fontListPartialIndex(fonts, u"", kLower), 0);
    EXPECT_EQ(fontListPartialIndex(fonts, u"cal", kLower), 2);
    EXPECT_EQ(fontListPartialIndex(fonts, u"CAM", kLower), 3);
    EXPECT_EQ(fontListPartialIndex(fonts, u"arial b", kLower), 1);
    EXPECT_EQ(fontListPartialIndex(fonts, u"b", kLower), 2);
    EXPECT_EQ(fontListPartialIndex(fonts, u"cx", kLower), 2);
    EXPECT_EQ(fontListPartialIndex(fonts, u"zz", kLower), 0);
    EXPECT_EQ(fontListPartialIndex({}, u"a", kLower), -1);
    EXPECT_EQ(fontListNameIndex(fonts, u"IMPACT", kLower), 4);
    EXPECT_EQ(fontListNameIndex(fonts, u"Tah", kLower), 5);
    EXPECT_EQ(catalogListPartialIndex(fonts, u"", kLower), 0);
    EXPECT_EQ(catalogListPartialIndex(fonts, u"ca", kLower), 2);
    EXPECT_EQ(catalogListPartialIndex({}, u"a", kLower), 0);
}

TEST(FontPicker, FamiliesAreListedFilteredAndKeptAsLegacyDid)
{
    ListingFonts service;
    service.faces = {{"Times", U"ab"}, {"arial", U""}, {"Arial", U"a"}, {"Comic", U"b"}, {"Times", U"a"}};
    FontFamilies families(service, kLower, FontListPlatform::Linux);
    EXPECT_EQ(families.fonts(), (std::vector<std::u16string>{u"arial", u"Comic", u"Times"}));
    EXPECT_EQ(service.listings, 1);
    EXPECT_EQ(families.fonts(), (std::vector<std::u16string>{u"arial", u"Comic", u"Times"}));
    EXPECT_EQ(service.listings, 1);
    EXPECT_EQ(families.filteredFonts(u"a"), (std::vector<std::u16string>{u"arial", u"Times"}));
    EXPECT_EQ(families.filteredFonts(u"b"), (std::vector<std::u16string>{u"arial", u"Times"}));
    families.enumerate();
    EXPECT_EQ(families.filteredFonts(u"b"), (std::vector<std::u16string>{u"arial", u"Comic", u"Times"}));
    EXPECT_EQ(service.checked.back(), U"b");
    families.refilter(u"a");
    EXPECT_EQ(families.filteredFonts(u"a"), (std::vector<std::u16string>{u"arial", u"Times"}));
    EXPECT_EQ(service.checked.back(), U"a");
}

TEST(FontPicker, FilterCharactersPerPlatform)
{
    EXPECT_EQ(filterCharacters(u"ą\U0001F600b", FontListPlatform::Windows), U"ąb");
    EXPECT_EQ(filterCharacters(u"ą\U0001F600b", FontListPlatform::Linux), U"ą\U0001F600b");
}

TEST(ExternalFonts, TheFolderIsReadAsLegacyLoadedIt)
{
    const fs::path dir = tempDir("external");
    const auto write = [&](const char *name, std::string_view bytes) { std::ofstream(dir / name) << bytes; };
    write("a.TTF", "font-a");
    write("b.otf", "font-b");
    write("c.txt", "text");
    write("d.ttc", "");
    write("ttf", "bare");
    write(".hidden.pfb", "hidden");
    fs::create_directories(dir / "sub.ttf");
    std::u16string path = dir.u16string() + u"/";
    const auto load = loadExternalFonts(path, kLower, FontListPlatform::Linux);
    ASSERT_TRUE(load.opened);
    std::vector<std::string> names;
    for (const auto &f : load.fonts)
        names.push_back(fs::path(f.name).filename().string());
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names, (std::vector<std::string>{".hidden.pfb", "a.TTF", "b.otf", "ttf"}));
    for (const auto &f : load.fonts)
        if (f.name.ends_with("a.TTF")) {
            EXPECT_EQ(f.name, dir.string() + "/a.TTF");
            EXPECT_EQ(f.bytes->size(), 6u);
        }
    EXPECT_FALSE(loadExternalFonts(path + u"missing/", kLower, FontListPlatform::Linux).opened);
    const fs::path empty = tempDir("external-empty");
    EXPECT_FALSE(loadExternalFonts(empty.u16string() + u"/", kLower, FontListPlatform::Linux).opened);
#ifdef _WIN32
    EXPECT_TRUE(loadExternalFonts(empty.u16string() + u"/", kLower, FontListPlatform::Windows).opened);
#endif
}

TEST(ExternalFonts, ExternalFacesAreListedAndUnreadableFilesNamed)
{
    ListingFonts service;
    service.faces = {{"Arial", U""}};
    FontFamilies families(service, kLower, FontListPlatform::Linux);
    FontEnvironment env;
    env.externalFonts = {{"/x/font.ttf", std::make_shared<std::vector<std::byte>>()}};
    families.setEnvironment(env);
    EXPECT_EQ(families.fonts(), (std::vector<std::u16string>{u"Arial", u"External font.ttf"}));
    EXPECT_TRUE(families.unreadableExternalFonts().empty());
}

TEST(FontPicker, TheRendererSelectionIsDescribed)
{
    FontReport report;
    EXPECT_EQ(pickerResolution(report).kind, PickerResolution::Kind::Missing);
    ResolvedFace face;
    face.stage = SelectionStage::Requested;
    face.familyNames = {"DejaVu Sans"};
    face.path = "/usr/share/fonts/DejaVuSans.ttf";
    face.emboldened = true;
    report.faces = {face};
    RequestReport request;
    request.faces = {0};
    request.substituted = true;
    report.requests = {request};
    auto r = pickerResolution(report);
    EXPECT_EQ(r.kind, PickerResolution::Kind::Substituted);
    EXPECT_EQ(r.family, "DejaVu Sans");
    EXPECT_EQ(r.file, "DejaVuSans.ttf");
    EXPECT_TRUE(r.emboldened);
    report.requests[0].substituted = false;
    report.requests[0].requestedFamilyFound = true;
    EXPECT_EQ(pickerResolution(report).kind, PickerResolution::Kind::Requested);
    report.requests[0].requestedFamilyFound = false;
    report.faces[0].stage = SelectionStage::DefaultFamily;
    EXPECT_EQ(pickerResolution(report).kind, PickerResolution::Kind::Fallback);
}
