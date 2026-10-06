// I5 (F47-corpus, F47-refresh): whole-document font collection from the
// renderer's own selections, reimport into a clean environment, refresh after
// font changes, concurrent documents and cancellation. Fixtures are the
// generated CC0 fonts; on Linux FONTCONFIG_FILE lists only them, the Qt OFL
// fonts and an initially empty refresh directory.
//
// Y6: the font picker's faces (the provider's and EXTERNAL_FONTS_DIRECTORY's),
// their character coverage for the font filter, and refresh() for the
// listing (F47-refresh).
#include "hikari/backends/libass_font_service.h"

#include <QCryptographicHash>
#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <thread>

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

std::string sha256Of(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
}

// A document: styles "<name>,<family>,<bold>,<italic>" and events "<style>,<text>".
std::vector<std::byte> document(std::initializer_list<std::string> styles, std::initializer_list<std::string> events)
{
    std::string s = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\nFormat: Name, "
                    "Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
                    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                    "MarginL, MarginR, MarginV, Encoding\n";
    for (const auto &style : styles) {
        std::string name, family, bold, italic;
        std::size_t a = style.find(','), b = style.find(',', a + 1), c = style.find(',', b + 1);
        name = style.substr(0, a);
        family = style.substr(a + 1, b - a - 1);
        bold = style.substr(b + 1, c - b - 1);
        italic = style.substr(c + 1);
        s += "Style: " + name + "," + family + ",40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000," + bold + "," +
             italic + ",0,0,100,100,0,0,1,0,0,7,10,10,10,1\n";
    }
    s += "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    int second = 0;
    for (const auto &event : events) {
        const std::size_t comma = event.find(',');
        char times[64];
        std::snprintf(times, sizeof times, "0:00:%02d.00,0:00:%02d.00", second, second + 1);
        s += "Dialogue: 0," + std::string(times) + "," + event.substr(0, comma) + ",,0,0,0,," + event.substr(comma + 1) +
             "\n";
        ++second;
    }
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
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

const CollectedFont *font(const FontCollection &c, const std::string &name)
{
    for (const auto &f : c.fonts)
        if (f.name == name)
            return &f;
    return nullptr;
}

} // namespace

// Y6: the external fonts (EXTERNAL_FONTS_DIRECTORY) are listed from their
// bytes, every face of a collection, and named by their file; legacy
// registered them for the process (FontEnumerator.cpp:637).
TEST(FontPickerFaces, ExternalFontsAreListedFromTheirBytes)
{
    const std::string refresh = std::string(HIKARI_FONT_REFRESH_SOURCE) + "/refresh.ttf";
    FontEnvironment env;
    env.systemFonts = false;
    env.externalFonts = {{refresh, load(refresh)}, {fixture("collection.ttc"), load(fixture("collection.ttc"))}};
    LibassFontService service;
    const auto faces = service.pickerFaces(env);
    ASSERT_EQ(faces.size(), 3u);
    EXPECT_EQ(faces[0].families.front(), "HikariProbeRefresh");
    EXPECT_EQ(faces[0].externalFile, refresh);
    EXPECT_EQ(faces[1].families.front(), "HikariProbeCollectionA");
    EXPECT_EQ(faces[2].families.front(), "HikariProbeCollectionB");
    EXPECT_EQ(faces[2].index, 1);
    EXPECT_EQ(faces[2].externalFile, fixture("collection.ttc"));
    // Bytes no face is read from list nothing.
    env.externalFonts = {{"/x/broken.ttf", std::make_shared<std::vector<std::byte>>(16, std::byte{1})}};
    EXPECT_TRUE(service.pickerFaces(env).empty());
}

// The font filter's check (FontEnumerator::CheckGlyphsExists): a face
// covers the filter when it maps every character.
TEST(FontPickerFaces, CoverageIsEveryCharacter)
{
    FontEnvironment env;
    env.systemFonts = false;
    env.externalFonts = {{fixture("base.ttf"), load(fixture("base.ttf"))},
                         {fixture("fallback.ttf"), load(fixture("fallback.ttf"))}};
    LibassFontService service;
    const auto faces = service.pickerFaces(env);
    ASSERT_EQ(faces.size(), 2u);
    EXPECT_EQ(service.facesCover(faces, env, U"AB"), (std::vector<bool>{true, false}));
    EXPECT_EQ(service.facesCover(faces, env, U"中"), (std::vector<bool>{false, true}));
    EXPECT_EQ(service.facesCover(faces, env, U"A中"), (std::vector<bool>{false, false}));
    EXPECT_EQ(service.facesCover(faces, env, std::u32string(1, U'\0')), (std::vector<bool>{false, false}));
}

// An external font renders as an installed one, and the renderer's
// selection names its file (the substituted face the font dialog reports).
TEST(FontPickerFaces, ExternalFontsRenderAndAreNamedByTheirFile)
{
    const std::string refresh = std::string(HIKARI_FONT_REFRESH_SOURCE) + "/refresh.ttf";
    FontEnvironment env;
    env.systemFonts = false;
    env.externalFonts = {{refresh, load(refresh)}};
    LibassFontService service;
    const auto r = service.resolve(env, {{"HikariProbeRefresh", false, false, "A"}});
    ASSERT_TRUE(r);
    ASSERT_EQ(r->requests.size(), 1u);
    ASSERT_FALSE(r->requests[0].faces.empty());
    const auto &face = r->faces[r->requests[0].faces[0]];
    EXPECT_TRUE(r->requests[0].requestedFamilyFound);
    EXPECT_EQ(face.path, refresh);
    EXPECT_TRUE(face.attachment.empty());
    EXPECT_EQ(face.sha256, sha256Of(refresh));
}

TEST(FontCollection, EveryStyleAndInlineChangeIsCollectedAndReimportsIdentically)
{
    const auto script = document({"A,HikariProbeBase,0,0", "B,HikariProbeCollectionB,0,0", "W,HikariProbeWeighted,0,0"},
                                 {"A,Plain", "B,From the collection", "A,{\\fnHikariProbeLegacy}Inline family",
                                  "W,{\\b1}Real bold", "A,{\\p1}m 0 0 l 100 0 100 100 0 100{\\p0}"});
    const auto env = attachments({"base.ttf", "collection.ttc", "legacy.ttf", "weighted.ttf", "base-bold.ttf",
                                  "collision-a.ttf"});
    LibassFontService service;
    const auto c = service.collect(script, env);
    ASSERT_TRUE(c);
    EXPECT_TRUE(c->complete());
    EXPECT_EQ(c->frameHashes.size(), 5u);
    std::vector<std::string> names;
    for (const auto &f : c->fonts)
        names.push_back(f.name);
    std::sort(names.begin(), names.end());
    // What libass selected: style W's own face is loaded before \b1 switches
    // to the real bold, so weighted.ttf is collected too; the unused collision
    // font is not.
    EXPECT_EQ(names, (std::vector<std::string>{"base-bold.ttf", "base.ttf", "collection.ttc", "legacy.ttf",
                                               "weighted.ttf"}));
    EXPECT_EQ(font(*c, "collection.ttc")->faces, std::vector<long>{1});
    EXPECT_EQ(font(*c, "base.ttf")->sha256, sha256Of(fixture("base.ttf")));
    EXPECT_EQ(font(*c, "legacy.ttf")->roles, std::vector<std::string>{"requested HikariProbeLegacy"});

    const auto reimport = service.verifyReimport(script, *c);
    ASSERT_TRUE(reimport);
    EXPECT_TRUE(reimport->identical) << reimport->differingFrames.size() << " frames differ";
}

TEST(FontCollection, MissingFamiliesAndGlyphsAreReportedAsIncomplete)
{
    const auto script = document({"A,HikariProbeBase,0,0", "N,NoSuchFamily,0,0"},
                                 {"A,Emoji \xf0\x9f\x98\x80 nowhere", "N,Missing family"});
    const auto c = LibassFontService().collect(script, attachments({"base.ttf", "fallback.ttf"}, "HikariProbeFallback"));
    ASSERT_TRUE(c);
    EXPECT_FALSE(c->complete());
    EXPECT_EQ(c->missingFamilies, std::vector<std::string>{"NoSuchFamily"});
    EXPECT_NE(std::find(c->missingGlyphs.begin(), c->missingGlyphs.end(), 0x1f600u), c->missingGlyphs.end());
    const auto *fallback = font(*c, "fallback.ttf");
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(fallback->roles, std::vector<std::string>{"default family"});
}

TEST(FontCollection, ConcurrentDocumentsKeepTheirOwnBytes)
{
    const auto script = document({"A,HikariProbeCollision,0,0"}, {"A,Same name, different bytes"});
    FontEnvironment first, second;
    first.systemFonts = second.systemFonts = false;
    first.attachments.push_back({"font.ttf", load(fixture("collision-a.ttf"))});
    second.attachments.push_back({"font.ttf", load(fixture("collision-b.ttf"))});
    std::vector<std::string> firstHashes, secondHashes;
    auto run = [&](const FontEnvironment &env, std::vector<std::string> &out) {
        LibassFontService service;
        for (int i = 0; i < 20; ++i) {
            const auto c = service.collect(script, env);
            out.push_back(c && c->fonts.size() == 1 ? c->fonts[0].sha256 : std::string("failed"));
        }
    };
    std::thread a(run, std::cref(first), std::ref(firstHashes));
    std::thread b(run, std::cref(second), std::ref(secondHashes));
    a.join();
    b.join();
    for (const auto &h : firstHashes)
        ASSERT_EQ(h, sha256Of(fixture("collision-a.ttf")));
    for (const auto &h : secondHashes)
        ASSERT_EQ(h, sha256Of(fixture("collision-b.ttf")));
}

TEST(FontCollection, AnAttachmentEditMakesANewGeneration)
{
    const auto script = document({"A,HikariProbeCollision,0,0"}, {"A,Edited"});
    auto env = attachments({"collision-a.ttf"});
    env.generation = 1;
    LibassFontService service;
    const auto before = service.collect(script, env);
    env.attachments = {{"collision-a.ttf", load(fixture("collision-b.ttf"))}}; // same name, new bytes
    env.generation = 2;
    const auto after = service.collect(script, env);
    ASSERT_TRUE(before && after);
    EXPECT_EQ(before->generation, 1u);
    EXPECT_EQ(after->generation, 2u);
    EXPECT_NE(before->fonts[0].sha256, after->fonts[0].sha256);
    EXPECT_NE(before->frameHashes, after->frameHashes);
}

TEST(FontCollection, CancellationEndsTheWork)
{
    std::vector<std::string> events;
    const auto script = document({"A,HikariProbeBase,0,0"}, {"A,One", "A,Two", "A,Three"});
    std::atomic<bool> cancel{true};
    const auto c = LibassFontService().collect(script, attachments({"base.ttf"}), &cancel);
    ASSERT_FALSE(c);
    EXPECT_EQ(c.error(), FontError::Cancelled);
}

// The multilingual corpus: Latin, Arabic, combining marks, CJK, a drawing and
// an emoji no fixture has. Characters outside the requested face are fallback
// dependent; the collection says so instead of claiming completeness.
TEST(FontCollection, MultilingualCorpusReportsWhatIsIncomplete)
{
    const auto script = document({"L,HikariProbeBase,0,0", "F,HikariProbeFallback,0,0"},
                                 {"L,Latin text", "F,\xd8\xa7\xd8\xb9 Arabic", "F,e\xcc\x81 combining",
                                  "F,\xe4\xb8\xad CJK", "L,{\\p1}m 0 0 l 50 0 50 50{\\p0}", "L,Mixed \xe4\xb8\xad",
                                  "L,\xf0\x9f\x98\x80"});
    const auto env = attachments({"base.ttf", "fallback.ttf"}, "HikariProbeFallback");
    LibassFontService service;
    const auto c = service.collect(script, env);
    ASSERT_TRUE(c);
    EXPECT_FALSE(c->complete());
    EXPECT_NE(std::find(c->missingGlyphs.begin(), c->missingGlyphs.end(), 0x1f600u), c->missingGlyphs.end());
    ASSERT_NE(font(*c, "fallback.ttf"), nullptr);
    EXPECT_NE(font(*c, "base.ttf"), nullptr);
    // The attachments-only corpus replays identically: no host fallback was used.
    const auto reimport = service.verifyReimport(script, *c, env.defaultFamily);
    ASSERT_TRUE(reimport);
    EXPECT_TRUE(reimport->identical);
}

#ifndef _WIN32

TEST(FontCollectionFontconfig, ProviderFallbackIsReportedAndDoesNotReplay)
{
    const auto script = document({"A,HikariProbeBase,0,0"}, {"A,Latin only", "A,With \xe4\xb8\xad"});
    LibassFontService service;
    const auto c = service.collect(script, FontEnvironment{});
    ASSERT_TRUE(c);
    EXPECT_EQ(c->provider, "fontconfig");
    EXPECT_EQ(c->fallbackGlyphs, std::vector<std::uint32_t>{0x4e2d});
    EXPECT_FALSE(c->complete());
    const auto *fallback = font(*c, "fallback.ttf");
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(fallback->path, fixture("fallback.ttf"));
    EXPECT_EQ(fallback->roles, std::vector<std::string>{"fallback U+4E2D"});
    // The accepted promise: report, do not promise replay. Without the host
    // resolver the fallback frame differs; the Latin-only frame does not.
    const auto reimport = service.verifyReimport(script, *c);
    ASSERT_TRUE(reimport);
    EXPECT_FALSE(reimport->identical);
    EXPECT_EQ(reimport->differingFrames, std::vector<std::size_t>{1});
}

TEST(FontCollectionFontconfig, InstallingAFontIsSeenByTheNextGeneration)
{
    namespace fs = std::filesystem;
    const fs::path installed = fs::path(HIKARI_FONT_REFRESH_DIR) / "refresh.ttf";
    fs::remove(installed);
    const auto script = document({"A,HikariProbeRefresh,0,0"}, {"A,Refresh"});
    FontEnvironment env;
    env.generation = 1;
    LibassFontService service;
    const auto before = service.collect(script, env);
    ASSERT_TRUE(before);
    // Absent: fontconfig answers with its best match under other names, at
    // the request (Arch) or as a substitution (Ubuntu's fontconfig rules).
    const auto listed = [](const std::vector<std::string> &l) {
        return std::find(l.begin(), l.end(), "HikariProbeRefresh") != l.end();
    };
    EXPECT_TRUE(listed(before->substitutedFamilies) || listed(before->missingFamilies));
    EXPECT_FALSE(before->complete());
    fs::copy_file(fs::path(HIKARI_FONT_REFRESH_SOURCE) / "refresh.ttf", installed);
    env.generation = 2;
    const auto after = service.collect(script, env);
    fs::remove(installed);
    ASSERT_TRUE(after);
    EXPECT_TRUE(after->complete());
    ASSERT_EQ(after->fonts.size(), 1u);
    EXPECT_EQ(after->fonts[0].path, installed.string());
    EXPECT_EQ(after->generation, 2u);
}

// Y6: installed faces' coverage is fontconfig's charset of the face (the
// Linux build's GetGlyphIndicesW, platform.h:1106).
TEST(FontPickerFacesFontconfig, InstalledFacesCoverWhatTheirCharsetHolds)
{
    LibassFontService service;
    const FontEnvironment env;
    const auto faces = service.pickerFaces(env);
    std::vector<SystemFace> chosen;
    for (const char *file : {"base.ttf", "fallback.ttf"})
        for (const auto &f : faces)
            if (f.path == fixture(file))
                chosen.push_back(f);
    ASSERT_EQ(chosen.size(), 2u);
    EXPECT_EQ(service.facesCover(chosen, env, U"A"), (std::vector<bool>{true, false}));
    EXPECT_EQ(service.facesCover(chosen, env, U"中"), (std::vector<bool>{false, true}));
    // The folders a font change re-lists (hikarisub_linux_font_directories).
    const auto dirs = service.fontDirectories();
    EXPECT_NE(std::find(dirs.begin(), dirs.end(), std::string(HIKARI_FONT_FIXTURES)), dirs.end());
    EXPECT_NE(std::find(dirs.begin(), dirs.end(), std::string(HIKARI_FONT_REFRESH_DIR)), dirs.end());
}

// F47-refresh for the picker: the listing keeps what it read until
// refresh(), which reads the installed fonts again.
TEST(FontPickerFacesFontconfig, RefreshReadsTheInstalledFontsAgain)
{
    namespace fs = std::filesystem;
    const fs::path installed = fs::path(HIKARI_FONT_REFRESH_DIR) / "picker-refresh.ttf";
    fs::remove(installed);
    LibassFontService service;
    service.refresh();
    const auto listed = [&] {
        const auto faces = service.pickerFaces(FontEnvironment{});
        return std::any_of(faces.begin(), faces.end(), [&](const SystemFace &f) { return f.path == installed.string(); });
    };
    // fontconfig trusts a folder's cache while the folder's modification
    // time (whole seconds) is unchanged; each change here moves it on.
    int seconds = 0;
    const auto touch = [&] {
        fs::last_write_time(installed.parent_path(), fs::file_time_type::clock::now() + std::chrono::seconds(++seconds * 2));
    };
    EXPECT_FALSE(listed());
    fs::copy_file(fs::path(HIKARI_FONT_REFRESH_SOURCE) / "refresh.ttf", installed);
    touch();
    EXPECT_FALSE(listed()) << "the listing keeps its generation";
    service.refresh();
    EXPECT_TRUE(listed());
    fs::remove(installed);
    touch();
    service.refresh();
    EXPECT_FALSE(listed());
}

#endif
