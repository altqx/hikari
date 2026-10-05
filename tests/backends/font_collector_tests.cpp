// Y8: the font collector through the libass diagnostic hook (F47 obligations
// as under I5): the collected hashes and collection faces, the archive's
// contents, both meanings of the font count, partial output written only
// after acknowledgment and labelled, cancellation leaving nothing
// unlabelled, and the written fonts reimported into a clean environment.
// On Linux the system provider is fontconfig over the generated CC0 fixtures
// only (FONTCONFIG_FILE), so every selection is deterministic. On Windows it
// is libass's DirectWrite provider over the installed fonts, with the
// fixtures the Documents name installed for the current user for the length
// of the test process (Y8W, windows_user_fonts.h); fallback comes from
// DirectWrite's own resolver and is reported as whichever file it chose.
#include "hikari/backends/font_collector_output.h"
#include "hikari/backends/libass_font_service.h"

#include "hikari/core/ass_load.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

#ifdef _WIN32
#include "fonts/windows_user_fonts.h"
#endif

using namespace hikari;
using namespace hikari::application;
using hikari::backends::FolderCollectorOutput;
using hikari::backends::LibassFontService;
using hikari::backends::ZipCollectorOutput;

namespace {

#ifndef _WIN32
// Set before fontconfig first initializes (as the I5 tests do).
[[maybe_unused]] const bool kPrivateFontconfig = setenv("FONTCONFIG_FILE", HIKARI_FONTCONFIG_FILE, 1) == 0;
#else
// The fixtures the Documents below name, installed for this process's
// tests; fallback.ttf is not, so DirectWrite's resolver picks a system font.
// DirectWrite's collection lists legacy.ttf under its typographic family
// (HikariProbeModern); GDI, where libass looks, under HikariProbeLegacy.
[[maybe_unused]] ::testing::Environment *const kUserFonts =
    ::testing::AddGlobalTestEnvironment(new hikari::testing::WindowsUserFonts(
        HIKARI_FONT_FIXTURES, {L"base.ttf", L"base-bold.ttf", L"weighted.ttf", L"legacy.ttf", L"collection.ttc"},
        {L"HikariProbeBase", L"HikariProbeWeighted", L"HikariProbeModern", L"HikariProbeCollectionB"}));
#endif

std::string fixture(const char *name)
{
    return std::string(HIKARI_FONT_FIXTURES) + "/" + name;
}

// A fixture's path as the provider names it: fontconfig the configured
// directory's, DirectWrite the installed copy's in the user's font folder.
std::u16string installed(const char *name)
{
#ifdef _WIN32
    return (hikari::testing::WindowsUserFonts::userFontFolder() / name).make_preferred().u16string();
#else
    return QString::fromStdString(fixture(name)).toStdU16String();
#endif
}

// The system provider the collector renders with: fontconfig, or libass's
// DirectWrite provider ("directwrite (with GDI)" on the desktop).
bool systemProvider(const std::string &provider)
{
#ifdef _WIN32
    return provider.rfind("directwrite", 0) == 0;
#else
    return provider == "fontconfig";
#endif
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

std::string sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

std::string fixtureSha(const char *name)
{
    return sha256(readAll(QString::fromStdString(fixture(name))));
}

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// styles "<name>,<family>,<bold>,<italic>", events "<style>,<text>", a second apart.
CollectorDocument document(int tab, std::initializer_list<std::string> styles, std::initializer_list<std::string> events)
{
    std::string s = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\nFormat: Name, "
                    "Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
                    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                    "MarginL, MarginR, MarginV, Encoding\n";
    for (const auto &style : styles) {
        const std::size_t a = style.find(','), b = style.find(',', a + 1), c = style.find(',', b + 1);
        s += "Style: " + style.substr(0, a) + "," + style.substr(a + 1, b - a - 1) +
             ",40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000," + style.substr(b + 1, c - b - 1) + "," +
             style.substr(c + 1) + ",0,0,100,100,0,0,1,0,0,7,10,10,10,1\n";
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
    CollectorDocument d;
    d.tab = tab;
    d.script = bytesOf(s);
    d.document = std::make_shared<core::Document>(core::loadAss(d.script).document);
    return d;
}

// The archive's entries in order, inflated; flags and method checked.
std::vector<std::pair<std::string, QByteArray>> unzip(const QByteArray &zip)
{
    std::vector<std::pair<std::string, QByteArray>> out;
    auto u16 = [&](qsizetype at) { return quint32(quint8(zip[at])) | quint32(quint8(zip[at + 1])) << 8; };
    auto u32 = [&](qsizetype at) { return u16(at) | u16(at + 2) << 16; };
    qsizetype at = 0;
    while (at + 30 <= zip.size() && u32(at) == 0x04034b50) {
        EXPECT_EQ(u16(at + 6) & 0x800, 0x800u) << "UTF-8 name flag (Utf8ZipEntry)";
        EXPECT_EQ(u16(at + 8), 8u) << "deflate";
        const quint32 crc = u32(at + 14), compressed = u32(at + 18), size = u32(at + 22);
        const quint32 nameLen = u16(at + 26), extraLen = u16(at + 28);
        const std::string name = zip.mid(at + 30, nameLen).toStdString();
        const QByteArray data = zip.mid(at + 30 + nameLen + extraLen, compressed);
        QByteArray inflated(qsizetype(size), '\0');
        z_stream z{};
        inflateInit2(&z, -15);
        z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
        z.avail_in = uInt(data.size());
        z.next_out = reinterpret_cast<Bytef *>(inflated.data());
        z.avail_out = uInt(inflated.size());
        EXPECT_EQ(inflate(&z, Z_FINISH), Z_STREAM_END) << name;
        inflateEnd(&z);
        EXPECT_EQ(crc32(0, reinterpret_cast<const Bytef *>(inflated.data()), uInt(inflated.size())), crc) << name;
        out.emplace_back(name, inflated);
        at += 30 + nameLen + extraLen + compressed;
    }
    EXPECT_TRUE(at + 4 <= zip.size() && u32(at) == 0x02014b50u) << "central directory after the entries";
    return out;
}

std::shared_ptr<const std::vector<std::byte>> shared(const QByteArray &b)
{
    auto v = std::make_shared<std::vector<std::byte>>(std::size_t(b.size()));
    std::memcpy(v->data(), b.data(), v->size());
    return v;
}

// Reimport in a clean environment: the Documents rendered from the written
// fonts alone (no system provider) against the system rendering.
bool reimportsIdentically(LibassFontService &service, const CollectorDocument &doc,
                          const std::vector<std::pair<std::string, QByteArray>> &fonts)
{
    const auto system = service.collect(doc.script, FontEnvironment{});
    if (!system)
        return false;
    FontCollection written = *system;
    written.fonts.clear();
    for (const auto &[name, bytes] : fonts) {
        CollectedFont f;
        f.name = name;
        f.bytes = shared(bytes);
        written.fonts.push_back(f);
    }
    const auto check = service.verifyReimport(doc.script, written);
    return check && check->identical;
}

const CollectedFile *file(const CollectorReview &r, const std::u16string &name)
{
    for (const auto &f : r.files)
        if (f.name == name)
            return &f;
    return nullptr;
}

} // namespace

// Zip: every variant's file as the renderer selected it, named from its
// provider path, the TTC's face recorded, the archive complete and
// unlabelled, and its contents alone reproduce every frame.
TEST(FontCollectorRenderer, ZipHoldsTheSelectedBytesAndReimportsIdentically)
{
    const auto doc = document(0, {"A,HikariProbeBase,0,0", "B,HikariProbeCollectionB,0,0", "W,HikariProbeWeighted,1,0"},
                              {"A,Plain", "B,From the collection", "A,{\\fnHikariProbeLegacy}Inline family", "W,Bold"});
    LibassFontService service;
    FontCollector collector(service);
    const auto review = collector.prepare({doc}, CollectorAction::Zip);
    ASSERT_TRUE(review);
    std::fprintf(stderr, "provider: %s\n", review->provider.c_str());
    EXPECT_TRUE(systemProvider(review->provider)) << review->provider;
    ASSERT_TRUE(review->retrievedFonts);
    EXPECT_TRUE(review->complete()) << FontCollector::labelText(*review, nullptr);
    std::vector<std::u16string> names;
    for (const auto &f : review->files)
        names.push_back(f.name);
    ASSERT_EQ(names, (std::vector<std::u16string>{u"base.ttf", u"collection.ttc", u"legacy.ttf", u"base-bold.ttf"}));
    EXPECT_EQ(file(*review, u"base.ttf")->sha256, fixtureSha("base.ttf"));
    EXPECT_EQ(file(*review, u"collection.ttc")->sha256, fixtureSha("collection.ttc"));
    EXPECT_EQ(file(*review, u"collection.ttc")->faces, std::vector<long>{1});
    EXPECT_EQ(file(*review, u"base-bold.ttf")->sha256, fixtureSha("base-bold.ttf"));
    EXPECT_EQ(file(*review, u"legacy.ttf")->shown, installed("legacy.ttf"));
    ASSERT_EQ(review->reimports.size(), 1u);
    EXPECT_TRUE(review->reimports[0].identical);
    EXPECT_EQ(review->reimports[0].frames, 4u);

    QTemporaryDir dir;
    const QString archive = dir.filePath(QStringLiteral("nested/fonts.zip"));
    ZipCollectorOutput out(archive);
    const auto result = collector.apply(*review, out, false);
    EXPECT_TRUE(result.complete);
    EXPECT_TRUE(result.written);
    EXPECT_FALSE(result.labelled);
    EXPECT_EQ(result.foundCount, 4); // files added
    const auto entries = unzip(readAll(archive));
    ASSERT_EQ(entries.size(), 4u);
    std::vector<std::string> entryNames;
    for (const auto &[name, bytes] : entries) {
        entryNames.push_back(name);
        EXPECT_EQ(sha256(bytes), fixtureSha(name.c_str())) << name;
    }
    EXPECT_EQ(entryNames, (std::vector<std::string>{"base.ttf", "collection.ttc", "legacy.ttf", "base-bold.ttf"}));
    EXPECT_TRUE(reimportsIdentically(service, doc, entries));
}

// A \fn value with a comma is one family (legacy ParseTags keeps the whole
// value, SubsDialogue.cpp:1086-1158), so the renderer is asked for it whole,
// as the Document asks: no font is named "HikariProbeBase,Bold", so it is
// not found, though HikariProbeBase is and a Style line would have read
// only that.
TEST(FontCollectorRenderer, AFamilyWithACommaIsAskedForWhole)
{
    const auto doc = document(0, {"A,HikariProbeBase,0,0"}, {"A,Plain", "A,{\\fnHikariProbeBase,Bold}Comma"});
    LibassFontService service;
    FontCollector collector(service);
    const auto review = collector.prepare({doc}, CollectorAction::Check);
    ASSERT_TRUE(review);
    EXPECT_TRUE(review->found.contains(u"HikariProbeBase"));
    EXPECT_TRUE(review->notFound.contains(u"HikariProbeBase,Bold"));
    EXPECT_FALSE(review->found.contains(u"HikariProbeBase,Bold"));
    EXPECT_EQ(review->notFoundCount, 1);
    EXPECT_FALSE(review->complete());
}

// Check counts families; Copy counts the files it wrote. One family used
// regular and bold is one font found and two fonts copied.
TEST(FontCollectorRenderer, BothMeaningsOfTheFontCount)
{
    const auto doc = document(0, {"W,HikariProbeWeighted,0,0", "A,HikariProbeBase,0,0"},
                              {"W,Regular", "W,{\\b1}Bold", "A,Base"});
    LibassFontService service;
    FontCollector collector(service);
    const auto check = collector.prepare({doc}, CollectorAction::Check);
    ASSERT_TRUE(check);
    EXPECT_EQ(collector.result(*check).foundCount, 2);
    EXPECT_TRUE(check->complete());

    const auto copy = collector.prepare({doc}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(copy);
    QTemporaryDir dir;
    FolderCollectorOutput out(dir.filePath(QStringLiteral("Czcionki")));
    const auto result = collector.apply(*copy, out, false);
    EXPECT_EQ(result.foundCount, 3);
    EXPECT_TRUE(result.complete);
    const QStringList written = QDir(dir.filePath(QStringLiteral("Czcionki"))).entryList(QDir::Files, QDir::Name);
    EXPECT_EQ(written, (QStringList{QStringLiteral("base-bold.ttf"), QStringLiteral("base.ttf"), QStringLiteral("weighted.ttf")}));
    for (const QString &name : written)
        EXPECT_EQ(sha256(readAll(dir.filePath(QStringLiteral("Czcionki/") + name))), fixtureSha(name.toUtf8().constData()));
}

// A fallback-dependent character and a missing family: the review is
// incomplete, the fallback file is reported with its role and not
// collected, nothing is written without acknowledgment, and with it the
// folder carries the label.
TEST(FontCollectorRenderer, PartialOutputOnlyAfterAcknowledgmentAndLabelled)
{
    const auto doc = document(0, {"A,HikariProbeBase,0,0", "N,NoSuchFamily,0,0"},
                              {"A,Latin only", "A,With \xe4\xb8\xad", "N,Missing family"});
    LibassFontService service;
    FontCollector collector(service);
    const auto review = collector.prepare({doc}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(review);
    EXPECT_FALSE(review->complete());
    EXPECT_FALSE(review->allGlyphs);
    EXPECT_TRUE(review->notFound.contains(u"NoSuchFamily"));
    EXPECT_EQ(review->notFoundCount, 1);
    EXPECT_EQ(review->fallbackGlyphs, std::vector<std::uint32_t>{0x4e2d});
    ASSERT_EQ(review->files.size(), 1u);
    EXPECT_EQ(review->files[0].name, u"base.ttf");
    const RendererFile *fallback = nullptr;
    for (const auto &f : review->rendererFiles)
        if (std::find(f.roles.begin(), f.roles.end(), "fallback U+4E2D") != f.roles.end())
            fallback = &f;
    ASSERT_NE(fallback, nullptr) << "the fallback file is reported with its role";
#ifdef _WIN32
    // DirectWrite's resolver chose an installed system font; whichever it is,
    // it is a file with bytes, reported and not collected.
    std::fprintf(stderr, "fallback for U+4E2D: %s (%s, faces %zu)\n",
                 QString::fromStdU16String(fallback->shown).toUtf8().constData(), fallback->sha256.substr(0, 12).c_str(),
                 fallback->faces.size());
    EXPECT_FALSE(fallback->sha256.empty());
    EXPECT_NE(fallback->shown.find(u'\\'), std::u16string::npos) << "named from the installed file's path";
#else
    EXPECT_EQ(fallback->shown, installed("fallback.ttf"));
#endif
    for (const auto &f : review->files)
        EXPECT_NE(f.sha256, fallback->sha256) << "the fallback file is not collected";
    // Without the fallback the clean reimport differs on the frame that used it.
    ASSERT_EQ(review->reimports.size(), 1u);
    EXPECT_FALSE(review->reimports[0].identical);
    const auto &warnings = review->found.at(u"HikariProbeBase").warnings;
    ASSERT_FALSE(warnings.empty());
    EXPECT_EQ(warnings[0].kind, collector::Note::Kind::MissingCharacters);
    EXPECT_EQ(warnings[0].a, u"中");

    QTemporaryDir dir;
    const QString folder = dir.filePath(QStringLiteral("out"));
    FolderCollectorOutput refused(folder);
    const auto no = collector.apply(*review, refused, false);
    EXPECT_TRUE(no.refused);
    EXPECT_FALSE(QDir(folder).exists());

    FolderCollectorOutput out(folder);
    const auto yes = collector.apply(*review, out, true);
    EXPECT_FALSE(yes.complete);
    EXPECT_TRUE(yes.labelled);
    EXPECT_EQ(QDir(folder).entryList(QDir::Files, QDir::Name),
              (QStringList{QString::fromUtf8(backends::kIncompleteLabel), QStringLiteral("base.ttf")}));
    const QString label = QString::fromUtf8(readAll(QDir(folder).filePath(QString::fromUtf8(backends::kIncompleteLabel))));
    EXPECT_TRUE(label.contains(QStringLiteral("INCOMPLETE")));
    EXPECT_TRUE(label.contains(QStringLiteral("NoSuchFamily")));
    EXPECT_TRUE(label.contains(QStringLiteral("U+4E2D")));
    EXPECT_TRUE(label.contains(QString::fromStdU16String(fallback->shown) + QStringLiteral(" (fallback U+4E2D), not collected")))
        << label.toStdString();

    // The same review as an archive: the label is an entry of it.
    const QString archive = dir.filePath(QStringLiteral("partial.zip"));
    const auto zipReview = collector.prepare({doc}, CollectorAction::Zip);
    ASSERT_TRUE(zipReview);
    ZipCollectorOutput zip(archive);
    const auto zipped = collector.apply(*zipReview, zip, true);
    EXPECT_TRUE(zipped.labelled);
    const auto entries = unzip(readAll(archive));
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].first, "base.ttf");
    EXPECT_EQ(entries[1].first, backends::kIncompleteLabel);
    EXPECT_TRUE(entries[1].second.contains("U+4E2D"));
}

// Cancelled while writing: an archive is never published (a file of that
// name stays as it was), a folder keeps its label.
namespace {
// Passes each call to the real output; the user cancels once the first
// font is written.
class CancelAfterFirstPut final : public CollectorOutput {
public:
    CancelAfterFirstPut(CollectorOutput &inner, std::atomic<bool> &cancel) : m_inner(inner), m_cancel(cancel) {}
    bool open() override { return m_inner.open(); }
    bool folderFailed() const override { return m_inner.folderFailed(); }
    bool put(const std::u16string &name, const std::vector<std::byte> &bytes) override
    {
        const bool ok = m_inner.put(name, bytes);
        m_cancel = true;
        return ok;
    }
    bool label(const std::string &utf8Text) override { return m_inner.label(utf8Text); }
    bool unlabel() override { return m_inner.unlabel(); }
    bool commit() override { return m_inner.commit(); }
    void discard() override { m_inner.discard(); }

private:
    CollectorOutput &m_inner;
    std::atomic<bool> &m_cancel;
};
} // namespace

// Cancelled after the first font is written: the folder keeps that font and
// its label; the archive, one entry already added, is discarded and the
// previous file of that name stays, with no temporary file left beside it.
TEST(FontCollectorRenderer, CancellationMidWriteLeavesNothingUnlabelled)
{
    const auto doc = document(0, {"W,HikariProbeWeighted,0,0", "A,HikariProbeBase,0,0"},
                              {"W,Regular", "W,{\\b1}Bold", "A,Base"});
    LibassFontService service;
    FontCollector collector(service);
    std::atomic<bool> cancel{false};

    QTemporaryDir dir;
    const auto folderReview = collector.prepare({doc}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(folderReview);
    ASSERT_TRUE(folderReview->complete());
    ASSERT_EQ(folderReview->files.size(), 3u);
    const QString folder = dir.filePath(QStringLiteral("copy"));
    {
        FolderCollectorOutput real(folder);
        CancelAfterFirstPut out(real, cancel);
        const auto r = collector.apply(*folderReview, out, false, &cancel);
        EXPECT_TRUE(r.cancelled);
        EXPECT_FALSE(r.complete);
        EXPECT_TRUE(r.labelled);
        EXPECT_EQ(r.foundCount, 1);
    }
    const QString first = QString::fromStdU16String(folderReview->files.front().name);
    EXPECT_EQ(QDir(folder).entryList(QDir::Files, QDir::Name),
              (QStringList{QString::fromUtf8(backends::kIncompleteLabel), first}));
    EXPECT_TRUE(readAll(QDir(folder).filePath(QString::fromUtf8(backends::kIncompleteLabel))).contains("Writing was cancelled."));
    EXPECT_EQ(sha256(readAll(QDir(folder).filePath(first))), fixtureSha(first.toUtf8().constData()));

    cancel = false;
    const auto zipReview = collector.prepare({doc}, CollectorAction::Zip);
    ASSERT_TRUE(zipReview);
    const QString archive = dir.filePath(QStringLiteral("fonts.zip"));
    {
        QFile previous(archive);
        ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
        previous.write("previous");
    }
    {
        ZipCollectorOutput real(archive);
        CancelAfterFirstPut out(real, cancel);
        const auto r = collector.apply(*zipReview, out, false, &cancel);
        EXPECT_TRUE(r.cancelled);
        EXPECT_FALSE(r.written);
        EXPECT_EQ(r.foundCount, 1);
    }
    EXPECT_EQ(readAll(archive), QByteArray("previous"));
    EXPECT_EQ(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("fonts.zip")});
}

TEST(FontCollectorRenderer, CancellationLeavesNothingUnlabelled)
{
    const auto doc = document(0, {"A,HikariProbeBase,0,0"}, {"A,Plain"});
    LibassFontService service;
    FontCollector collector(service);
    const auto zipReview = collector.prepare({doc}, CollectorAction::Zip);
    ASSERT_TRUE(zipReview);
    ASSERT_TRUE(zipReview->complete());
    std::atomic<bool> cancel{true};

    QTemporaryDir dir;
    const QString archive = dir.filePath(QStringLiteral("fonts.zip"));
    {
        QFile previous(archive);
        ASSERT_TRUE(previous.open(QIODevice::WriteOnly));
        previous.write("previous");
    }
    {
        ZipCollectorOutput out(archive);
        const auto r = collector.apply(*zipReview, out, false, &cancel);
        EXPECT_TRUE(r.cancelled);
        EXPECT_FALSE(r.written);
    }
    EXPECT_EQ(readAll(archive), QByteArray("previous"));
    EXPECT_EQ(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("fonts.zip")});

    const auto folderReview = collector.prepare({doc}, CollectorAction::CopyToFolder);
    ASSERT_TRUE(folderReview);
    const QString folder = dir.filePath(QStringLiteral("copy"));
    FolderCollectorOutput out(folder);
    const auto r = collector.apply(*folderReview, out, false, &cancel);
    EXPECT_TRUE(r.cancelled);
    EXPECT_FALSE(r.complete);
    EXPECT_EQ(QDir(folder).entryList(QDir::Files), QStringList{QString::fromUtf8(backends::kIncompleteLabel)});
    EXPECT_TRUE(readAll(QDir(folder).filePath(QString::fromUtf8(backends::kIncompleteLabel))).contains("Writing was cancelled."));

    // Cancelled while the review is prepared.
    const auto none = collector.prepare({doc}, CollectorAction::Zip, &cancel);
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error(), FontError::Cancelled);
}
