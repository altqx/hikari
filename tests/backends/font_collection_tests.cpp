#include "hikari/application/font_families.h"
#include "hikari/backends/libass_font_service.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <thread>

#ifdef _WIN32
#include "fonts/windows_user_fonts.h"
#endif

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

std::string sha256Of(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
}

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

std::optional<SystemFace> listedFace(const std::vector<SystemFace> &faces, const std::filesystem::path &file,
                                     int index = 0)
{
    for (const auto &f : faces)
        if (f.externalFile.empty() && f.index == index && samePath(f.path, file))
            return f;
    return std::nullopt;
}

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

// DirectWrite's font cache takes a moment to let go of a removed font.
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
    // fontconfig trusts a folder's cache while its mtime (whole seconds) is one it has cached.
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

}

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
    env.externalFonts = {{"/x/broken.ttf", std::make_shared<std::vector<std::byte>>(16, std::byte{1})}};
    EXPECT_TRUE(service.pickerFaces(env).empty());
}

TEST(FontPickerFaces, ALeasesNamesAreReadFromItsBytes)
{
    const auto names = hikari::backends::fontNames(*load(fixture("collection.ttc")));
    for (const char *want : {"HikariProbeCollectionA", "HikariProbeCollectionB"})
        EXPECT_NE(std::ranges::find(names, std::string(want)), names.end()) << want;
    EXPECT_TRUE(hikari::backends::fontNames(std::vector<std::byte>(16, std::byte{1})).empty());
}

namespace {

std::vector<std::byte> nameTable(unsigned records, const std::function<std::string(unsigned)> &text, bool shared)
{
    std::vector<std::byte> out, strings;
    const auto put16 = [&out](unsigned v) {
        out.push_back(std::byte(v >> 8 & 0xFF));
        out.push_back(std::byte(v & 0xFF));
    };
    put16(0);
    put16(records);
    put16(6 + 12 * records);
    for (unsigned i = 0; i < records; ++i) {
        const unsigned offset = shared ? 0 : unsigned(strings.size());
        if (!shared || i == 0)
            for (const char c : text(i)) {
                strings.push_back(std::byte{0});
                strings.push_back(std::byte(c));
            }
        put16(3);
        put16(1);
        put16(0x409);
        put16(1);
        put16(shared ? unsigned(strings.size()) : unsigned(strings.size()) - offset);
        put16(offset);
    }
    out.insert(out.end(), strings.begin(), strings.end());
    return out;
}

std::vector<std::byte> collectionOf(unsigned faces, const std::vector<std::byte> &names)
{
    const auto base = load(fixture("base.ttf"));
    if (!base)
        return {};
    const auto at = [&base](std::size_t i) { return std::to_integer<unsigned>((*base)[i]); };
    const auto be16 = [&at](std::size_t i) { return at(i) << 8 | at(i + 1); };
    const auto be32 = [&be16](std::size_t i) { return be16(i) << 16 | be16(i + 2); };
    std::vector<std::byte> out;
    const auto put16 = [&out](unsigned v) {
        out.push_back(std::byte(v >> 8 & 0xFF));
        out.push_back(std::byte(v & 0xFF));
    };
    const auto put32 = [&put16](unsigned v) {
        put16(v >> 16);
        put16(v & 0xFFFF);
    };
    const unsigned tables = be16(4);
    const unsigned header = 12 + 4 * faces;
    put32(0x74746366); // ttcf
    put32(0x00010000);
    put32(faces);
    for (unsigned i = 0; i < faces; ++i)
        put32(header);
    for (std::size_t i = 0; i < 12; ++i)
        out.push_back((*base)[i]);
    unsigned offset = header + 12 + 16 * tables;
    std::vector<std::byte> data;
    for (unsigned t = 0; t < tables; ++t) {
        const std::size_t record = 12 + 16 * std::size_t(t);
        const unsigned tag = be32(record);
        const bool isName = tag == 0x6E616D65; // name
        const unsigned from = be32(record + 8), length = isName ? unsigned(names.size()) : be32(record + 12);
        put32(tag);
        put32(be32(record + 4));
        put32(offset + unsigned(data.size()));
        put32(length);
        if (isName)
            data.insert(data.end(), names.begin(), names.end());
        else
            data.insert(data.end(), base->begin() + from, base->begin() + from + length);
        while (data.size() % 4)
            data.push_back(std::byte{0});
    }
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

}

TEST(FontPickerFaces, AHostileCollectionsNamesAreBounded)
{
    const auto out = collectionOf(32, nameTable(
                                          5000,
                                          [](unsigned i) {
                                              char text[16];
                                              std::snprintf(text, sizeof text, "Name%05u", i);
                                              return std::string(text);
                                          },
                                          false));
    ASSERT_FALSE(out.empty());

    const auto started = std::chrono::steady_clock::now();
    const auto read = hikari::backends::fontNames(out);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    EXPECT_NE(std::ranges::find(read, std::string("Name00000")), read.end());
    EXPECT_LE(read.size(), 4096u);
    EXPECT_LT(ms, 3000);
}

TEST(FontPickerFaces, AHostileCollectionsRepeatedLongNamesAreBounded)
{
    const auto out =
        collectionOf(256, nameTable(2730, [](unsigned) { return std::string(16380, 'A'); }, true));
    ASSERT_FALSE(out.empty());

    const auto started = std::chrono::steady_clock::now();
    const auto read = hikari::backends::fontNames(out);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    EXPECT_NE(std::ranges::find(read, std::string(16380, 'A')), read.end());
    EXPECT_LT(ms, 3000);
}

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
    // libass loads style W's own face before \b1 switches to the real bold, so weighted.ttf is collected too.
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
    env.attachments = {{"collision-a.ttf", load(fixture("collision-b.ttf"))}};
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
    const auto reimport = service.verifyReimport(script, *c, env.defaultFamily);
    ASSERT_TRUE(reimport);
    EXPECT_TRUE(reimport->identical);
}

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
    const auto reimport = service.verifyReimport(script, *c);
    ASSERT_TRUE(reimport);
    EXPECT_FALSE(reimport->identical);
    EXPECT_EQ(reimport->differingFrames, std::vector<std::size_t>{1});
}

TEST(FontCollectionFontconfig, InstallingAFontIsSeenByTheNextGeneration)
{
    InstalledFont font(HIKARI_FONT_REFRESH_SOURCE, "refresh.ttf", "HikariProbeRefresh");
    const auto script = document({"A,HikariProbeRefresh,0,0"}, {"A,Refresh"});
    FontEnvironment env;
    env.generation = 1;
    LibassFontService service;
    const auto before = service.collect(script, env);
    ASSERT_TRUE(before);
    // fontconfig answers an absent family with its best match, at the request (Arch) or as a substitution (Ubuntu).
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
    const auto dirs = service.fontDirectories();
    EXPECT_NE(std::find(dirs.begin(), dirs.end(), std::string(HIKARI_FONT_FIXTURES)), dirs.end());
    EXPECT_NE(std::find(dirs.begin(), dirs.end(), std::string(HIKARI_FONT_REFRESH_DIR)), dirs.end());
}

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
    // fontconfig trusts a folder's cache while its mtime (whole seconds) is unchanged.
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
