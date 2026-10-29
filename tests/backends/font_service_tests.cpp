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
// Set before fontconfig initializes: ctest's ENVIRONMENT did not reach these tests on the Fedora runner.
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

}

TEST(FontIdentity, SameNamedAttachmentsAreToldApartByTheirBytes)
{
    const auto ab = resolve(attachments({"collision-a.ttf", "collision-b.ttf"}), {{"HikariProbeCollision", false, false, "AB"}});
    const auto ba = resolve(attachments({"collision-b.ttf", "collision-a.ttf"}), {{"HikariProbeCollision", false, false, "AB"}});
    EXPECT_EQ(ab.provider, "none");
    ASSERT_EQ(ab.requests[0].faces.size(), 1u);
    EXPECT_EQ(face(ab, 0).postscriptName, face(ba, 0).postscriptName) << "names cannot tell them apart";
    EXPECT_EQ(face(ab, 0).sha256, sha256Of(fixture("collision-a.ttf")));
    EXPECT_EQ(face(ab, 0).attachment, "collision-a.ttf");
    // Characterization of libass: the first attachment wins.
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
    // Characterization of libass: the attachment provider matches neither the typographic family nor the PostScript name.
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
                           {{"HikariProbeBase", false, false, "A\xe4\xb8\xad"},
                            {"HikariProbeBase", false, false, "A\xf0\x9f\x98\x80"},
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
    // Characterization of libass: the attachment provider ignores the typographic family ("Georama ExtraCondensed Thin" is ID 1).
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
    // Characterization of libass: a named instance's weight comes from the default instance's OS/2 class, so these are also emboldened.
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

// Windows can hold a face twice (a per-user arial.ttf beside the system one), so any installed file of the face is right.
TEST(DirectWriteIdentity, StreamBytesAreTheInstalledFile)
{
    LibassFontService service;
    const auto faces = service.systemFaces();
    ASSERT_FALSE(faces.empty());
    auto pathsOf = [&](const char *family, int weight) {
        std::vector<std::string> paths;
        for (const auto &f : faces)
            if (!f.italic && f.weight == weight && !f.path.empty() &&
                std::find(f.families.begin(), f.families.end(), family) != f.families.end() &&
                std::find(paths.begin(), paths.end(), f.path) == paths.end())
                paths.push_back(f.path);
        return paths;
    };
    auto installedFile = [](const std::string &sha, const std::vector<std::string> &paths) {
        for (const auto &p : paths)
            if (sha256Of(p) == sha)
                return p;
        return std::string();
    };
    auto listed = [](const std::vector<std::string> &paths) {
        std::string out;
        for (const auto &p : paths)
            out += "\n  " + p + " (" + sha256Of(p).substr(0, 12) + ")";
        return out;
    };
    const auto regulars = pathsOf("Arial", 400), bolds = pathsOf("Arial", 700);
    ASSERT_FALSE(regulars.empty()) << "Arial is part of every Windows install";
    const auto r = resolve(FontEnvironment{}, {{"Arial", false, false, "A"}, {"Arial", true, false, "A"}});
    std::fprintf(stderr, "provider: %s\n", r.provider.c_str());
    EXPECT_EQ(r.provider.rfind("directwrite", 0), 0u);
    EXPECT_EQ(face(r, 0).stage, SelectionStage::Requested);
    EXPECT_TRUE(face(r, 0).path.empty()) << "DirectWrite hands libass a stream";
    const std::string regular = installedFile(face(r, 0).sha256, regulars);
    EXPECT_FALSE(regular.empty()) << face(r, 0).sha256 << " is no installed Arial 400:" << listed(regulars);
    std::fprintf(stderr, "Arial 400: %zu installed, the stream is %s\n", regulars.size(), regular.c_str());
    if (!bolds.empty()) {
        const std::string bold = installedFile(face(r, 1).sha256, bolds);
        EXPECT_FALSE(bold.empty()) << face(r, 1).sha256 << " is no installed Arial 700:" << listed(bolds);
        EXPECT_FALSE(face(r, 1).emboldened);
        std::fprintf(stderr, "Arial 700: %zu installed, the stream is %s\n", bolds.size(), bold.c_str());
    }
}

TEST(DirectWriteIdentity, ScriptCoverageIsReportedNotAssumed)
{
    // Installed fonts' script coverage varies, so this only requires an honest report of what DirectWrite chose.
    const auto r = resolve(FontEnvironment{}, {{"Arial", false, false, "A\xe4\xb8\xad"},
                                               {"Arial", false, false, "A\xd8\xb9"},
                                               {"Arial", false, false, "A\xe0\xa4\x95"}});
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
