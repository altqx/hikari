// W2 (Windows, HIKARI_WITH_VSFILTER): the xy-VSFilter this build's recipe
// made, through the CSRI adapter, against the legacy release's own
// xy-VSFilter (v0.0.1-rc.1, Csri/xy-VSFilter_hikarisub.dll, built by the
// legacy solution from the same fork commit). Both are driven the way legacy
// SubtitlesVSFilter drove its renderer, on the same fixtures: positions,
// styles, \clip, karaoke and the YCbCr matrix header. Fonts are the system's
// (VSFilter resolves them through GDI); the fixtures name Arial.
//
// HIKARI_LEGACY_CSRI: the legacy release's Csri folder (winix task
// legacy-win-setup extracts it to out/legacy-win/bundled/HikariSub_x64/Csri).
// Without it the comparison is skipped, never passed.

#include "hikari/backends/csri_renderer.h"

#include <windows.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::backends;

namespace {

constexpr int kWidth = 640, kHeight = 360;

std::string script(std::string_view header, std::string_view styles, std::string_view events)
{
    return std::string("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n") + std::string(header) +
           "\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
           "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
           "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
           "Style: Default,Arial,36,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1\n" +
           std::string(styles) +
           "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
           std::string(events);
}

struct Fixture {
    const char *name;
    std::string text;
    std::vector<int> times; // milliseconds
};

std::vector<Fixture> fixtures()
{
    return {
        {"positions",
         script("", "",
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an1}an1\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an5}centre\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an9}an9\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\pos(100,80)}pos\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\move(100,300,500,300,0,4000)}move\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,40,0,60,,margins\n"),
         {0, 1000, 2500, 4000}},
        {"styles",
         script("",
                "Style: Sign,Arial,48,&H0030A0E0,&H000000FF,&H00402010,&H00000000,-1,-1,-1,-1,120,80,4,10,1,3,4,8,"
                "10,10,10,1\nStyle: Box,Arial,30,&H4000FF00,&H000000FF,&H00FF0000,&H80000000,0,0,0,0,100,100,0,0,3,"
                "4,0,5,10,10,10,1\n",
                "Dialogue: 0,0:00:00.00,0:00:05.00,Sign,,0,0,0,,Bold italic underline\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Box,,0,0,0,,Opaque box\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\bord5\\shad0\\blur2\\c&H0000FF&\\3c&HFFFFFF&\\alpha&H40&}blur "
                "{\\be1\\fscx150\\frz15}be rotate\n"),
         {1000}},
        {"clip",
         script("", "",
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an5\\pos(320,120)\\fs80\\clip(250,90,390,140)}CLIPPED\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an5\\pos(320,200)\\fs80\\iclip(250,170,390,220)}INVERSE\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an5\\pos(320,290)\\fs80\\clip(m 220 250 l 420 250 320 330)}VECTOR\n"),
         {1000}},
        {"karaoke",
         script("", "",
                "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\an8\\k50}ka{\\k50}ra{\\k100}o{\\k100}ke\n"
                "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\an5\\kf100}fill{\\kf100}sweep\n"
                "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\an2\\ko100}out{\\ko100}line\n"),
         {0, 250, 750, 1500, 2600}},
        {"matrix-none", script("", "", "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\c&H3080E0&}matrix\n"), {1000}},
        {"matrix-tv601",
         script("YCbCr Matrix: TV.601\n", "", "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\c&H3080E0&}matrix\n"),
         {1000}},
        {"matrix-tv709",
         script("YCbCr Matrix: TV.709\n", "", "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\c&H3080E0&}matrix\n"),
         {1000}},
    };
}

RenderSnapshot snapshotOf(const std::string &text)
{
    RenderSnapshot s;
    s.script.resize(text.size());
    std::memcpy(s.script.data(), text.data(), text.size());
    return s;
}

QImage image(const OverlayFrame &f)
{
    QImage out(f.width, f.height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < f.height; ++y)
        std::memcpy(out.scanLine(y), f.pixels.data() + static_cast<std::size_t>(y) * f.stride, std::size_t(f.width) * 4);
    return out;
}

std::filesystem::path builtFolder()
{
    return std::filesystem::path(QDir(QStringLiteral(HIKARI_VSFILTER_CSRI_DIR)).absolutePath().toStdU16String());
}

// The legacy DLL under its own name in a scratch folder, so the two modules
// cannot be mistaken for each other.
std::filesystem::path legacyFolder()
{
    const char *dir = std::getenv("HIKARI_LEGACY_CSRI");
    if (!dir || !*dir)
        return {};
    const auto source = std::filesystem::path(dir) / "xy-VSFilter_hikarisub.dll";
    if (!std::filesystem::exists(source))
        return {};
    const auto folder = std::filesystem::path(QDir(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR)).absolutePath().toStdU16String()) /
                        "legacy-csri";
    std::filesystem::create_directories(folder);
    std::filesystem::copy_file(source, folder / "legacy-xy-VSFilter.dll",
                               std::filesystem::copy_options::overwrite_existing);
    return folder;
}

} // namespace

TEST(VsfilterRecipe, TheBuiltRendererLoadsThroughCsri)
{
    std::vector<std::string> log;
    const auto renderers = CsriRenderers::load(builtFolder(), [&](const std::string &t) { log.push_back(t); });
    EXPECT_EQ(renderers->names(), (std::vector<std::string>{"xy-vsfilter_textsub"}));
    for (const auto &line : log)
        ADD_FAILURE() << line;
    CsriRenderer r(renderers, "xy-vsfilter_textsub");
    ASSERT_TRUE(r.prepare(snapshotOf(fixtures().front().text)));
    const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->empty);
}

TEST(VsfilterRecipe, MatchesTheLegacyRendererOnEveryFixture)
{
    const auto legacy = legacyFolder();
    if (legacy.empty())
        GTEST_SKIP() << "HIKARI_LEGACY_CSRI does not name the legacy release's Csri folder";
    const auto built = CsriRenderers::load(builtFolder(), {});
    const auto old = CsriRenderers::load(legacy, {});
    ASSERT_EQ(built->names(), (std::vector<std::string>{"xy-vsfilter_textsub"}));
    ASSERT_EQ(old->names(), (std::vector<std::string>{"xy-vsfilter_textsub"}));
    const QString artifacts = QStringLiteral(HIKARI_TEST_ARTIFACT_DIR);
    QDir().mkpath(artifacts);
    for (const auto &f : fixtures()) {
        CsriRenderer a(built, ""), b(old, "");
        ASSERT_TRUE(a.prepare(snapshotOf(f.text))) << f.name;
        ASSERT_TRUE(b.prepare(snapshotOf(f.text))) << f.name;
        for (int ms : f.times) {
            const auto x = a.render(core::DocumentTime(std::int64_t(ms) * 1000), kWidth, kHeight);
            const auto y = b.render(core::DocumentTime(std::int64_t(ms) * 1000), kWidth, kHeight);
            ASSERT_TRUE(x && y) << f.name << " " << ms;
            const QString stem = artifacts + QStringLiteral("/%1-%2").arg(QLatin1String(f.name)).arg(ms);
            image(*x).save(stem + QStringLiteral("-built.png"));
            image(*y).save(stem + QStringLiteral("-legacy.png"));
            int differing = 0, maxDiff = 0;
            for (std::size_t i = 0; i < x->pixels.size(); ++i) {
                const int d = std::abs(int(x->pixels[i]) - int(y->pixels[i]));
                differing += d != 0;
                maxDiff = std::max(maxDiff, d);
            }
            std::fprintf(stderr, "W2 %s %d ms: drawn=%d differing bytes=%d max difference=%d\n", f.name, ms,
                         int(!x->empty), differing, maxDiff);
            EXPECT_EQ(x->empty, y->empty) << f.name << " " << ms;
            EXPECT_EQ(differing, 0) << f.name << " " << ms << ": max difference " << maxDiff;
        }
    }
}

// Font resolution for VSFilter's output (fonts.md: CSRI/VSFilter needs its
// own agreement evidence; a libass match does not certify it). xy-VSFilter
// asks GDI for each style's face (c4297ed0 src/subtitles/RTS.cpp:116-138,
// CMyFont: CreateFontIndirect with the LOGFONT STS.cpp:3719-3731 fills from
// the style, its name, Encoding as the charset and the size as lfHeight);
// the face GDI answers is what it draws with. Each requested family is written with the face GDI chose, and
// a family GDI does not have is reported as substituted, never as matched.
TEST(VsfilterRecipe, ReportsTheFacesGdiResolvesForItsOutput)
{
    struct Request {
        const wchar_t *family;
        BYTE charset;
    };
    // The fixtures' Encoding 1 is DEFAULT_CHARSET.
    const Request requests[] = {{L"Arial", DEFAULT_CHARSET}, {L"Times New Roman", DEFAULT_CHARSET},
                                {L"Hikari No Such Font", DEFAULT_CHARSET}};
    HDC dc = CreateCompatibleDC(nullptr);
    ASSERT_NE(dc, nullptr);
    const QString artifacts = QStringLiteral(HIKARI_TEST_ARTIFACT_DIR);
    QDir().mkpath(artifacts);
    QFile report(artifacts + QStringLiteral("/vsfilter-fonts.txt"));
    ASSERT_TRUE(report.open(QIODevice::WriteOnly | QIODevice::Text));
    for (const auto &r : requests) {
        LOGFONTW lf{};
        lf.lfHeight = 36; // CMyFont: (LONG)(fontSize + 0.5)
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = r.charset;
        lf.lfOutPrecision = OUT_TT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = ANTIALIASED_QUALITY;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
        wcsncpy_s(lf.lfFaceName, r.family, _TRUNCATE);
        HFONT font = CreateFontIndirectW(&lf);
        ASSERT_NE(font, nullptr);
        HGDIOBJ old = SelectObject(dc, font);
        wchar_t face[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, face);
        SelectObject(dc, old);
        DeleteObject(font);
        const QString requested = QString::fromWCharArray(r.family), resolved = QString::fromWCharArray(face);
        const bool matched = requested.compare(resolved, Qt::CaseInsensitive) == 0;
        report.write(QStringLiteral("%1 -> %2 (%3)\n")
                         .arg(requested, resolved, matched ? QStringLiteral("matched") : QStringLiteral("substituted"))
                         .toUtf8());
        std::fprintf(stderr, "W2 font %s -> %s\n", qPrintable(requested), qPrintable(resolved));
        if (requested.startsWith(QLatin1String("Hikari")))
            EXPECT_FALSE(matched) << "a family GDI does not have is substituted";
        else
            EXPECT_TRUE(matched) << qPrintable(requested) << " resolved to " << qPrintable(resolved);
    }
    DeleteDC(dc);
    // The substituted family still draws (GDI's substitute), which is why it
    // is reported rather than trusted.
    const auto renderers = CsriRenderers::load(builtFolder(), {});
    CsriRenderer r(renderers, "");
    ASSERT_TRUE(r.prepare(snapshotOf(script("", "Style: Missing,Hikari No Such Font,36,&H00FFFFFF,&H000000FF,"
                                                "&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1\n",
                                            "Dialogue: 0,0:00:00.00,0:00:05.00,Missing,,0,0,0,,substitute\n"))));
    const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->empty);
}
