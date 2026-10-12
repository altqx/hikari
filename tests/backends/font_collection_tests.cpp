// I5 (F47-corpus, F47-refresh): whole-document font collection from the
// renderer's own selections, reimport into a clean environment, refresh after
// font changes, concurrent documents and cancellation. Fixtures are the
// generated CC0 fonts; on Linux FONTCONFIG_FILE lists only them, the Qt OFL
// fonts and an initially empty refresh directory.
//
// Y6: the font picker's faces (the provider's and EXTERNAL_FONTS_DIRECTORY's),
// their character coverage for the font filter, and refresh() for the
// listing (F47-refresh). The installed-font listing runs on both providers:
// a fixture installed into the private fontconfig configuration on Linux,
// and on Windows installed for the current user while the test runs (Y8W,
// windows_user_fonts.h), where DirectWrite lists it beside the system's.
#include "hikari/application/font_families.h"
#include "hikari/backends/libass_font_service.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>

#ifdef _WIN32
#include "fonts/windows_user_fonts.h"
#endif

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

// A path as listed, for comparison: native separators, no trailing one, and
// on Windows without regard to case.
QString comparable(const QString &path)
{
    QString out = QDir::toNativeSeparators(path);
    while (out.size() > 1 && out.endsWith(QDir::separator()))
        out.chop(1);
#ifdef _WIN32
    out = out.toCaseFolded();
#endif
    return out;
}

bool samePath(const std::string &listed, const std::filesystem::path &file)
{
    return comparable(QString::fromStdString(listed)) == comparable(QString::fromStdU16String(file.u16string()));
}

// The installed face the provider lists from `file`.
std::optional<SystemFace> listedFace(const std::vector<SystemFace> &faces, const std::filesystem::path &file,
                                     int index = 0)
{
    for (const auto &f : faces)
        if (f.externalFile.empty() && f.index == index && samePath(f.path, file))
            return f;
    return std::nullopt;
}

// The families the font picker lists over `service` (FontEnumerator).
std::vector<std::u16string> pickerFamilies(FontServicePort &service)
{
    FontFamilies families(service, [](char16_t c) { return c >= u'A' && c <= u'Z' ? char16_t(c + 32) : c; });
    return families.fonts();
}

bool lists(const std::vector<std::u16string> &families, std::u16string_view name)
{
    return std::find(families.begin(), families.end(), name) != families.end();
}

bool hasFamily(const SystemFace &face, const std::string &name)
{
    return std::find(face.families.begin(), face.families.end(), name) != face.families.end();
}

// Asks until `condition` holds, for up to 30 s: DirectWrite's font cache
// takes a moment to let go of a removed font, fontconfig answers at once.
template <typename Condition> bool eventually(Condition condition)
{
    const auto start = std::chrono::steady_clock::now();
    while (!condition()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(30))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

// One fixture installed as a system font while a test runs: on Linux copied
// into the private configuration's refresh folder (as installed-<file>, apart
// from the other refresh tests' files), on Windows installed for the current
// user. remove() takes it away again, as the destructor does when a failed
// assertion ended the test first.
class InstalledFont {
public:
    InstalledFont(const char *directory, const char *file, [[maybe_unused]] const char *family)
#ifdef _WIN32
        : m_fonts(directory, {std::filesystem::path(file).wstring()}, {std::filesystem::path(family).wstring()}),
          m_path((hikari::testing::WindowsUserFonts::userFontFolder() / file).make_preferred())
#else
        : m_source(std::filesystem::path(directory) / file),
          m_path(std::filesystem::path(HIKARI_FONT_REFRESH_DIR) / (std::string("installed-") + file))
#endif
    {
    }
    InstalledFont(const InstalledFont &) = delete;
    InstalledFont &operator=(const InstalledFont &) = delete;
    ~InstalledFont()
    {
        if (m_installed)
            remove();
    }

    const std::filesystem::path &path() const { return m_path; }

    void install()
    {
        m_installed = true;
#ifdef _WIN32
        m_fonts.SetUp();
#else
        const auto before = std::filesystem::last_write_time(m_path.parent_path());
        std::filesystem::copy_file(m_source, m_path, std::filesystem::copy_options::overwrite_existing);
        changed(before);
#endif
    }

    void remove()
    {
        m_installed = false;
#ifdef _WIN32
        m_fonts.TearDown();
#else
        const auto before = std::filesystem::last_write_time(m_path.parent_path());
        std::filesystem::remove(m_path);
        changed(before);
#endif
    }

private:
#ifdef _WIN32
    hikari::testing::WindowsUserFonts m_fonts;
#else
    // fontconfig trusts a folder's cache while the folder's modification
    // time (whole seconds) is one it has cached; move it past every earlier one.
    void changed(std::filesystem::file_time_type before)
    {
        const auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(m_path.parent_path(), std::max(before, now) + std::chrono::seconds(2));
    }
    std::filesystem::path m_source;
#endif
    std::filesystem::path m_path;
    bool m_installed = false;
};

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

// Y6 (F47-refresh) on both providers: an installed font is listed from its
// file under its family name, with its coverage, in the picker's families
// and from a folder a font change re-lists; once it is removed, the listing
// after refresh() drops it. On Windows it is installed for the current user
// (HKCU Fonts and %LOCALAPPDATA%\Microsoft\Windows\Fonts), the second of
// FontEnumerator's folders (FontEnumerator.cpp:362-374); on Linux into a
// folder of the fontconfig configuration. The service lists before the
// install, so the listing that names the font is one read after it.
TEST(FontPickerFacesInstalled, AnInstalledFontIsListedUntilItIsRemoved)
{
    LibassFontService service;
    InstalledFont font(HIKARI_FONT_REFRESH_SOURCE, "refresh.ttf", "HikariProbeRefresh");
    EXPECT_FALSE(listedFace(service.pickerFaces(FontEnvironment{}), font.path()))
        << font.path().string() << " is listed before it was installed";
    EXPECT_FALSE(lists(pickerFamilies(service), u"HikariProbeRefresh"));
    font.install();
    service.refresh();
    const auto faces = service.pickerFaces(FontEnvironment{});
    const auto face = listedFace(faces, font.path());
    ASSERT_TRUE(face) << font.path().string() << " is not among the " << faces.size() << " faces listed";
    EXPECT_EQ(listedFamily(*face), u"HikariProbeRefresh");
    EXPECT_TRUE(hasFamily(*face, "HikariProbeRefresh"));
    EXPECT_EQ(face->postscriptName, "HikariProbeRefresh-Regular");
    EXPECT_EQ(service.facesCover({*face}, FontEnvironment{}, U"A"), std::vector<bool>{true});
    EXPECT_EQ(service.facesCover({*face}, FontEnvironment{}, U"中"), std::vector<bool>{false});
    EXPECT_TRUE(lists(pickerFamilies(service), u"HikariProbeRefresh"));
    const auto dirs = service.fontDirectories();
    EXPECT_TRUE(std::any_of(dirs.begin(), dirs.end(),
                            [&](const std::string &d) { return samePath(d, font.path().parent_path()); }));

    font.remove();
    EXPECT_TRUE(eventually([&] {
        service.refresh();
        return !listedFace(service.pickerFaces(FontEnvironment{}), font.path());
    })) << font.path().string() << " is still listed 30 s after it was removed";
    EXPECT_FALSE(lists(pickerFamilies(service), u"HikariProbeRefresh"));
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
    // Installed and removed with the folder's modification time moved on, so
    // the cache fontconfig keeps for the folder does not outlive the font.
    InstalledFont font(HIKARI_FONT_REFRESH_SOURCE, "refresh.ttf", "HikariProbeRefresh");
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
    font.install();
    env.generation = 2;
    const auto after = service.collect(script, env);
    font.remove();
    ASSERT_TRUE(after);
    EXPECT_TRUE(after->complete());
    ASSERT_EQ(after->fonts.size(), 1u);
    EXPECT_EQ(after->fonts[0].path, font.path().string());
    EXPECT_EQ(after->generation, 2u);
}

// Y6: installed faces are listed by fontconfig's first family name (the
// Linux build's EnumFontFamiliesEx, platform.h:1133) and their coverage is
// fontconfig's charset of the face (its GetGlyphIndicesW, platform.h:1106).
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
    EXPECT_EQ(listedFamily(chosen[0]), u"HikariProbeBase");
    EXPECT_EQ(listedFamily(chosen[1]), u"HikariProbeFallback");
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

#ifdef _WIN32

// Y6 on Windows: DirectWrite lists the fonts installed for every user and
// for the current one, under the GDI family name legacy's EnumFontFamiliesEx
// listed (FontEnumerator.cpp:155), and coverage reads each installed file's
// character map as GetGlyphIndicesW did (FontEnumerator::CheckGlyphsExists).
// The fixtures are installed for the current user while each test runs and
// removed again afterwards.
class FontPickerFacesDirectWrite : public ::testing::Test {
protected:
    void SetUp() override { m_fonts.SetUp(); }
    void TearDown() override { m_fonts.TearDown(); }

    static std::filesystem::path userFont(const char *name)
    {
        return (hikari::testing::WindowsUserFonts::userFontFolder() / name).make_preferred();
    }

private:
    // DirectWrite's collection lists legacy.ttf under its typographic family.
    hikari::testing::WindowsUserFonts m_fonts{
        HIKARI_FONT_FIXTURES,
        {L"base.ttf", L"fallback.ttf", L"legacy.ttf", L"collection.ttc"},
        {L"HikariProbeBase", L"HikariProbeFallback", L"HikariProbeModern", L"HikariProbeCollectionB"}};
};

TEST_F(FontPickerFacesDirectWrite, SystemAndUserFontsAreListedByTheirGdiFamilyNames)
{
    LibassFontService service;
    const auto faces = service.pickerFaces(FontEnvironment{});
    const std::filesystem::path windows = qEnvironmentVariable("WINDIR").toStdWString();
    ASSERT_FALSE(windows.empty()) << "WINDIR";
    // Arial where the image has it, else any face listed from the system folder.
    const std::filesystem::path systemFolder = windows / L"Fonts";
    auto systemFont = listedFace(faces, systemFolder / L"arial.ttf");
    for (auto f = faces.begin(); !systemFont && f != faces.end(); ++f) {
        const QString folder = QFileInfo(QString::fromStdString(f->path)).path();
        if (f->externalFile.empty() && samePath(folder.toStdString(), systemFolder))
            systemFont = *f;
    }
    ASSERT_TRUE(systemFont) << "none of the " << faces.size() << " faces listed is from " << systemFolder.string();
    if (samePath(systemFont->path, systemFolder / L"arial.ttf"))
        EXPECT_EQ(systemFont->listedFamily, "Arial");
    ASSERT_FALSE(systemFont->listedFamily.empty()) << systemFont->path;
    EXPECT_TRUE(hasFamily(*systemFont, systemFont->listedFamily)) << systemFont->path;

    const auto base = listedFace(faces, userFont("base.ttf"));
    const auto fallback = listedFace(faces, userFont("fallback.ttf"));
    const auto legacy = listedFace(faces, userFont("legacy.ttf"));
    const auto first = listedFace(faces, userFont("collection.ttc"), 0);
    const auto second = listedFace(faces, userFont("collection.ttc"), 1);
    ASSERT_TRUE(base && fallback && legacy && first && second)
        << "the current user's fonts are not all among the " << faces.size() << " faces listed";
    EXPECT_EQ(base->listedFamily, "HikariProbeBase");
    EXPECT_EQ(fallback->listedFamily, "HikariProbeFallback");
    EXPECT_EQ(first->listedFamily, "HikariProbeCollectionA");
    EXPECT_EQ(second->listedFamily, "HikariProbeCollectionB");
    // Listed under its GDI name; the typographic family is answered to too.
    EXPECT_EQ(legacy->listedFamily, "HikariProbeLegacy");
    EXPECT_TRUE(hasFamily(*legacy, "HikariProbeLegacy"));
    EXPECT_TRUE(hasFamily(*legacy, "HikariProbeModern"));

    const auto families = pickerFamilies(service);
    EXPECT_TRUE(lists(families, QString::fromStdString(systemFont->listedFamily).toStdU16String()))
        << systemFont->listedFamily;
    for (const std::u16string name :
         {u"HikariProbeBase", u"HikariProbeLegacy", u"HikariProbeCollectionA", u"HikariProbeCollectionB"})
        EXPECT_TRUE(lists(families, name)) << QString::fromStdU16String(name).toStdString();
    EXPECT_FALSE(lists(families, u"HikariProbeModern"));

    EXPECT_EQ(service.facesCover({*base, *fallback}, FontEnvironment{}, U"A"), (std::vector<bool>{true, false}));
    EXPECT_EQ(service.facesCover({*base, *fallback}, FontEnvironment{}, U"中"), (std::vector<bool>{false, true}));

    const auto dirs = service.fontDirectories();
    for (const auto &folder : {windows / L"Fonts", hikari::testing::WindowsUserFonts::userFontFolder()})
        EXPECT_TRUE(std::any_of(dirs.begin(), dirs.end(), [&](const std::string &d) { return samePath(d, folder); }))
            << folder.string();
}

#endif
