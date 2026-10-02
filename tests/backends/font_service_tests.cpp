// N4: font identity from the renderer itself (F47-abi through the libass
// diagnostics patch; F47-fontconfig on Linux; F47-variable with the locked Qt
// install's OFL Georama). Fixtures are original CC0 fonts generated at build
// time. On Linux, FONTCONFIG_FILE lists only them and the Qt OFL fonts.
// Characterizations are labelled as such: they pin what the pinned libass
// does, not what it should do.
#include "hikari/backends/libass_font_service.h"

#include <QCryptographicHash>
#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace hikari::application;
using hikari::backends::LibassFontService;

namespace {

#ifndef _WIN32
// The private configuration, set before fontconfig first initializes: the
// ctest ENVIRONMENT property did not reach these tests on the Fedora runner.
[[maybe_unused]] const bool kPrivateFontconfig = setenv("FONTCONFIG_FILE", HIKARI_FONTCONFIG_FILE, 1) == 0;
#endif

std::shared_ptr<const std::vector<std::byte>> load(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray d = f.readAll();
    auto b = std::make_shared<std::vector<std::byte>>(std::size_t(d.size()));
    std::memcpy(b->data(), d.constData(), b->size());
    return b;
}

std::string fixture(const char *name)
{
    return std::string(HIKARI_FONT_FIXTURES) + "/" + name;
}
std::string georama()
{
    return std::string(HIKARI_QT_FONTS) + "/quick/advancedtext/fonts/Georama-VariableFont_wdth,wght.ttf";
}
std::string titillium(const char *style)
{
    return std::string(HIKARI_QT_FONTS) + "/quickcontrols/wearable/WearableStyle/fonts/TitilliumWeb-" + style + ".ttf";
}

std::string sha256Of(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
}

FontEnvironment attachments(std::initializer_list<const char *> names, std::string defaultFamily = {})
{
    FontEnvironment env;
    env.systemFonts = false;
    env.defaultFamily = std::move(defaultFamily);
    for (const char *n : names)
        env.attachments.push_back({n, load(fixture(n))});
    return env;
}

const ResolvedFace &face(const FontReport &r, std::size_t request, std::size_t which = 0)
{
    return r.faces.at(r.requests.at(request).faces.at(which));
}

FontReport resolve(const FontEnvironment &env, std::vector<FontRequest> requests)
{
    LibassFontService service;
    auto r = service.resolve(env, requests);
    EXPECT_TRUE(r);
    return r.value_or(FontReport{});
}

} // namespace

TEST(FontIdentity, SameNamedAttachmentsAreToldApartByTheirBytes)
{
    const auto ab = resolve(attachments({"collision-a.ttf", "collision-b.ttf"}), {{"HikariProbeCollision", false, false, "AB"}});
    const auto ba = resolve(attachments({"collision-b.ttf", "collision-a.ttf"}), {{"HikariProbeCollision", false, false, "AB"}});
    EXPECT_EQ(ab.provider, "none");
    ASSERT_EQ(ab.requests[0].faces.size(), 1u);
    EXPECT_EQ(face(ab, 0).postscriptName, face(ba, 0).postscriptName) << "names cannot tell them apart";
    EXPECT_EQ(face(ab, 0).sha256, sha256Of(fixture("collision-a.ttf")));
    EXPECT_EQ(face(ab, 0).attachment, "collision-a.ttf");
    // Characterization: the first attachment wins, as in the Windows experiment.
    EXPECT_EQ(face(ba, 0).attachment, "collision-b.ttf");
    EXPECT_TRUE(face(ab, 0).embedded);
    EXPECT_TRUE(ab.complete());
}

TEST(FontIdentity, CollectionFacesKeepTheirIndex)
{
    const auto r = resolve(attachments({"collection.ttc"}), {{"HikariProbeCollectionA", false, false, "A"},
                                                             {"HikariProbeCollectionB", false, false, "A"}});
    EXPECT_EQ(face(r, 0).faceIndex, 0);
    EXPECT_EQ(face(r, 1).faceIndex, 1);
    EXPECT_EQ(face(r, 1).faceCount, 2);
    EXPECT_EQ(face(r, 0).sha256, face(r, 1).sha256) << "one file, two faces: identity needs both";
    EXPECT_EQ(face(r, 1).sha256, sha256Of(fixture("collection.ttc")));
}

TEST(FontIdentity, AuthoredNamesStayAsWrittenAndMatchesAreNamed)
{
    const auto r = resolve(attachments({"legacy.ttf", "fallback.ttf"}, "HikariProbeFallback"),
                           {{"HikariProbeLegacy", false, false, "A"},
                            {"HikariProbeLegacy Regular", false, false, "A"},
                            {"HikariProbeModern", false, false, "A"},
                            {"HikariProbeLegacyPS", false, false, "A"}});
    EXPECT_EQ(r.requests[1].request.family, "HikariProbeLegacy Regular");
    EXPECT_EQ(face(r, 0).nameMatch, NameMatch::Family);
    EXPECT_EQ(face(r, 1).nameMatch, NameMatch::FullName);
    EXPECT_EQ(face(r, 1).attachment, "legacy.ttf");
    // Characterization: the attachment provider matches neither the
    // typographic family nor the PostScript name; the default family stands in.
    for (std::size_t i : {2u, 3u}) {
        EXPECT_FALSE(r.requests[i].requestedFamilyFound) << r.requests[i].request.family;
        EXPECT_EQ(face(r, i).stage, SelectionStage::DefaultFamily);
        EXPECT_EQ(face(r, i).attachment, "fallback.ttf");
    }
    EXPECT_FALSE(r.complete());
    const auto &names = face(r, 0).familyNames;
    EXPECT_NE(std::find(names.begin(), names.end(), "HikariProbeModern"), names.end()) << "searchable metadata";
}

TEST(FontIdentity, RealStylesAreSelectedAndSyntheticOnesReported)
{
    const auto r = resolve(attachments({"weighted.ttf", "base-bold.ttf", "base.ttf"}),
                           {{"HikariProbeWeighted", true, false, "A"}, {"HikariProbeBase", true, true, "A"}});
    EXPECT_EQ(face(r, 0).attachment, "base-bold.ttf");
    EXPECT_FALSE(face(r, 0).emboldened) << "a real bold face";
    EXPECT_EQ(face(r, 1).attachment, "base.ttf");
    EXPECT_TRUE(face(r, 1).emboldened);
    EXPECT_TRUE(face(r, 1).italicized);
}

TEST(FontIdentity, FallbackMissingFontAndMissingGlyphAreDistinct)
{
    const auto r = resolve(attachments({"base.ttf", "fallback.ttf"}, "HikariProbeFallback"),
                           {{"HikariProbeBase", false, false, "A\xe4\xb8\xad"},     // A and a CJK character
                            {"HikariProbeBase", false, false, "A\xf0\x9f\x98\x80"}, // A and an emoji nowhere
                            {"NoSuchFamily", false, false, " "}});
    ASSERT_EQ(r.requests[0].faces.size(), 2u);
    EXPECT_TRUE(r.requests[0].requestedFamilyFound);
    EXPECT_TRUE(r.requests[0].usedFallback);
    EXPECT_EQ(face(r, 0, 1).code, 0x4e2du);
    EXPECT_EQ(face(r, 0, 1).stage, SelectionStage::DefaultFamily);
    EXPECT_EQ(face(r, 0, 1).attachment, "fallback.ttf");
    EXPECT_TRUE(r.requests[0].missingGlyphs.empty());
    EXPECT_EQ(r.requests[1].missingGlyphs, std::vector<std::uint32_t>{0x1f600});
    EXPECT_EQ(r.requests[1].faces.size(), 1u) << "no face carries it";
    EXPECT_FALSE(r.requests[2].requestedFamilyFound);
    EXPECT_FALSE(r.requests[0].captured());
    EXPECT_FALSE(r.requests[1].captured());
    EXPECT_FALSE(r.complete());
}

TEST(FontIdentity, AttachedVariableFontIsNotFoundByItsTypographicFamily)
{
    FontEnvironment env;
    env.systemFonts = false;
    env.attachments.push_back({"Georama.ttf", load(georama())});
    const auto r = resolve(env, {{"Georama", true, false, "A"}});
    // Characterization: its family name (ID 1) is "Georama ExtraCondensed
    // Thin"; the attachment provider ignores the typographic family.
    EXPECT_FALSE(r.requests[0].requestedFamilyFound);
}

TEST(FontIdentity, DocumentEnvironmentsDoNotShareAttachments)
{
    FontEnvironment first, second;
    first.systemFonts = second.systemFonts = false;
    first.attachments.push_back({"font.ttf", load(fixture("collision-a.ttf"))});
    second.attachments.push_back({"font.ttf", load(fixture("collision-b.ttf"))});
    const auto a = resolve(first, {{"HikariProbeCollision", false, false, "A"}});
    const auto b = resolve(second, {{"HikariProbeCollision", false, false, "A"}});
    EXPECT_EQ(face(a, 0).sha256, sha256Of(fixture("collision-a.ttf")));
    EXPECT_EQ(face(b, 0).sha256, sha256Of(fixture("collision-b.ttf")));
}

TEST(FontIdentity, InvalidFamiliesAreRefused)
{
    LibassFontService service;
    EXPECT_EQ(service.resolve({}, {{"Bad,Name", false, false, "A"}}).error(), FontError::InvalidInput);
}

#ifndef _WIN32

extern "C" int FcGetVersion(void);

// libass must use the pinned static fontconfig (vcpkg 2.17.1), configured
// by the test's private FONTCONFIG_FILE, not a system copy Qt loads.
TEST(FontconfigIdentity, ThePinnedFontconfigReadsThePrivateConfiguration)
{
    const char *file = std::getenv("FONTCONFIG_FILE");
    std::fprintf(stderr, "fontconfig %d, FONTCONFIG_FILE=%s\n", FcGetVersion(), file ? file : "(unset)");
    EXPECT_EQ(FcGetVersion(), 21701);
    ASSERT_NE(file, nullptr);
    EXPECT_NE(std::string(file).find("font-fixtures.conf"), std::string::npos);
}

TEST(FontconfigIdentity, TheProviderNamesTheFileItSelected)
{
    const auto r = resolve(FontEnvironment{}, {{"HikariProbeBase", false, false, "A"}});
    EXPECT_EQ(r.provider, "fontconfig");
    EXPECT_EQ(face(r, 0).path, fixture("base.ttf"));
    EXPECT_EQ(face(r, 0).sha256, sha256Of(fixture("base.ttf")));
    EXPECT_FALSE(face(r, 0).embedded);
    EXPECT_TRUE(r.complete());
}

TEST(FontconfigIdentity, GlyphFallbackComesFromTheProvider)
{
    const auto r = resolve(FontEnvironment{}, {{"HikariProbeBase", false, false, "A\xe4\xb8\xad"}});
    ASSERT_EQ(r.requests[0].faces.size(), 2u);
    EXPECT_EQ(face(r, 0, 1).stage, SelectionStage::Fallback);
    EXPECT_EQ(face(r, 0, 1).path, fixture("fallback.ttf"));
    EXPECT_FALSE(r.complete()) << "fallback-dependent glyphs keep the report incomplete";
}

TEST(FontconfigIdentity, TypographicFamiliesResolveThroughFontconfig)
{
    const auto r = resolve(FontEnvironment{}, {{"HikariProbeModern", false, false, "A"},
                                               {"HikariProbeCollectionB", false, false, "A"}});
    EXPECT_EQ(face(r, 0).path, fixture("legacy.ttf"));
    EXPECT_EQ(face(r, 0).nameMatch, NameMatch::TypographicFamily);
    EXPECT_EQ(face(r, 1).path, fixture("collection.ttc"));
    EXPECT_EQ(face(r, 1).faceIndex, 1);
}

TEST(FontconfigIdentity, NamedInstancesCarryTheirIndexAndCoordinates)
{
    const auto r = resolve(FontEnvironment{}, {{"Georama", true, false, "A"}, {"Georama", false, false, "A"}});
    const auto &bold = face(r, 0), &regular = face(r, 1);
    EXPECT_EQ(bold.path, georama());
    EXPECT_EQ(bold.sha256, regular.sha256) << "one file";
    EXPECT_EQ(bold.faceIndex >> 16, 7) << "named instance 7";
    EXPECT_EQ(regular.faceIndex >> 16, 4);
    EXPECT_EQ(bold.coords, (std::vector<double>{100, 700}));
    EXPECT_EQ(regular.coords, (std::vector<double>{100, 400}));
    EXPECT_EQ(bold.postscriptName, "GeoramaRoman-Bold");
    // Characterization (reported, not endorsed): libass judges a named
    // instance's weight from the default instance's OS/2 class (Thin), so it
    // also emboldens these instances synthetically.
    EXPECT_TRUE(bold.emboldened);
    EXPECT_TRUE(regular.emboldened);
}

TEST(FontconfigIdentity, RealBoldFilesNeedNoSimulation)
{
    const auto r = resolve(FontEnvironment{}, {{"Titillium Web", true, false, "A"}});
    EXPECT_EQ(face(r, 0).path, titillium("Bold"));
    EXPECT_FALSE(face(r, 0).emboldened);
}

TEST(FontconfigIdentity, SystemFacesAreTheProvidersView)
{
    LibassFontService service;
    const auto faces = service.systemFaces();
    auto find = [&](const char *family, int index) {
        return std::find_if(faces.begin(), faces.end(), [&](const SystemFace &f) {
            return std::find(f.families.begin(), f.families.end(), family) != f.families.end() && f.index == index;
        });
    };
    const auto b = find("HikariProbeCollectionB", 1);
    ASSERT_NE(b, faces.end());
    EXPECT_EQ(b->path, fixture("collection.ttc"));
    const auto instance = find("Georama", 7 << 16);
    ASSERT_NE(instance, faces.end());
    EXPECT_EQ(instance->weight, 700);
    EXPECT_NE(find("HikariProbeModern", 0), faces.end()) << "every family name a face answers to";
}

#else

TEST(DirectWriteIdentity, StreamBytesAreTheInstalledFile)
{
    LibassFontService service;
    const auto faces = service.systemFaces();
    ASSERT_FALSE(faces.empty());
    auto pathOf = [&](const char *family, int weight) -> std::string {
        for (const auto &f : faces)
            if (!f.italic && f.weight == weight &&
                std::find(f.families.begin(), f.families.end(), family) != f.families.end())
                return f.path;
        return {};
    };
    const std::string regular = pathOf("Arial", 400), bold = pathOf("Arial", 700);
    ASSERT_FALSE(regular.empty()) << "Arial is part of every Windows install";
    const auto r = resolve(FontEnvironment{}, {{"Arial", false, false, "A"}, {"Arial", true, false, "A"}});
    std::fprintf(stderr, "provider: %s\n", r.provider.c_str());
    EXPECT_EQ(r.provider.rfind("directwrite", 0), 0u);
    EXPECT_EQ(face(r, 0).stage, SelectionStage::Requested);
    EXPECT_TRUE(face(r, 0).path.empty()) << "DirectWrite hands libass a stream";
    EXPECT_EQ(face(r, 0).sha256, sha256Of(regular)) << regular;
    if (!bold.empty()) {
        EXPECT_EQ(face(r, 1).sha256, sha256Of(bold)) << bold;
        EXPECT_FALSE(face(r, 1).emboldened);
    }
}

TEST(DirectWriteIdentity, ScriptCoverageIsReportedNotAssumed)
{
    // Which installed fonts cover which scripts varies, so this records what
    // DirectWrite chose and requires only an honest report: the requested face
    // itself, a Fallback-stage face with its bytes, or a missing glyph.
    const auto r = resolve(FontEnvironment{}, {{"Arial", false, false, "A\xe4\xb8\xad"},     // CJK
                                               {"Arial", false, false, "A\xd8\xb9"},         // Arabic
                                               {"Arial", false, false, "A\xe0\xa4\x95"}});  // Devanagari
    for (std::size_t i = 0; i < r.requests.size(); ++i) {
        const auto &request = r.requests[i];
        if (request.faces.size() > 1) {
            const auto &fallback = face(r, i, 1);
            EXPECT_EQ(fallback.stage, SelectionStage::Fallback);
            EXPECT_FALSE(fallback.sha256.empty());
            EXPECT_FALSE(request.captured()) << "fallback-dependent text is never complete";
            std::fprintf(stderr, "request %zu: fallback for U+%04X is %s (%s, face %ld)\n", i, fallback.code,
                         fallback.postscriptName.c_str(), fallback.sha256.substr(0, 12).c_str(), fallback.faceIndex);
        } else if (!request.missingGlyphs.empty()) {
            EXPECT_FALSE(request.captured());
            std::fprintf(stderr, "request %zu: no installed font covers U+%04X\n", i, request.missingGlyphs[0]);
        } else {
            EXPECT_TRUE(request.captured());
            std::fprintf(stderr, "request %zu: covered by the requested face %s\n", i,
                         face(r, i).postscriptName.c_str());
        }
    }
}

#endif
