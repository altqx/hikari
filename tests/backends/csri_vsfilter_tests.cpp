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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
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

// The faces xy-VSFilter itself selected while it drew: its module's import
// of gdi32!SelectObject is redirected, and each font it selects into a DC
// (CMyFont's SelectFont(g_hDC, ...) for the metrics, CText's for the glyph
// outlines, RTS.cpp:131 and :650) is read back on that DC right after the
// selection: the LOGFONT it asked for (GetObject) and the face GDI's mapper
// chose for it (GetTextFace, and the outline metrics' full name, which names
// the font file's face, "Arial Bold Italic" where a style is a real face).
namespace drawn {

struct Face {
    std::wstring requested;
    LONG weight = 0;
    bool italic = false;
    std::wstring selected;
    std::wstring full;
    bool operator<(const Face &o) const
    {
        return std::tie(requested, weight, italic, selected, full) <
               std::tie(o.requested, o.weight, o.italic, o.selected, o.full);
    }
    bool operator==(const Face &o) const = default;
};

using SelectObjectFn = HGDIOBJ(WINAPI *)(HDC, HGDIOBJ);
std::mutex lock;
std::set<Face> faces;
SelectObjectFn realSelectObject = &SelectObject;

HGDIOBJ WINAPI selectObject(HDC dc, HGDIOBJ object)
{
    HGDIOBJ previous = realSelectObject(dc, object);
    if (previous && object && GetObjectType(object) == OBJ_FONT) {
        LOGFONTW lf{};
        wchar_t face[LF_FACESIZE] = {};
        if (GetObjectW(object, sizeof lf, &lf) && GetTextFaceW(dc, LF_FACESIZE, face)) {
            Face f{lf.lfFaceName, lf.lfWeight, lf.lfItalic != 0, face, {}};
            if (const UINT size = GetOutlineTextMetricsW(dc, 0, nullptr)) {
                std::vector<char> buffer(size);
                auto *otm = reinterpret_cast<OUTLINETEXTMETRICW *>(buffer.data());
                if (GetOutlineTextMetricsW(dc, size, otm))
                    f.full = reinterpret_cast<const wchar_t *>(buffer.data() + reinterpret_cast<std::size_t>(otm->otmpFullName));
            }
            std::lock_guard guard(lock);
            faces.insert(std::move(f));
        }
    }
    return previous;
}

// Points `module`'s import of dll!function at `replacement`; the import's
// previous target, or null when the module does not import it by name.
void *patchImport(HMODULE module, const char *dll, const char *function, void *replacement)
{
    auto *base = reinterpret_cast<BYTE *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return nullptr;
    for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), dll) != 0 || !d->OriginalFirstThunk)
            continue;
        auto *name = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->OriginalFirstThunk);
        auto *slot = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->FirstThunk);
        for (; name->u1.AddressOfData; ++name, ++slot) {
            if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal))
                continue;
            const auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + name->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), function) != 0)
                continue;
            DWORD protection = 0;
            if (!VirtualProtect(&slot->u1.Function, sizeof slot->u1.Function, PAGE_READWRITE, &protection))
                return nullptr;
            void *previous = reinterpret_cast<void *>(slot->u1.Function);
            slot->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&slot->u1.Function, sizeof slot->u1.Function, protection, &protection);
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
            return previous;
        }
    }
    return nullptr;
}

// The module that holds a renderer's entry points.
HMODULE moduleOf(const std::shared_ptr<CsriRenderers> &renderers)
{
    const auto *r = renderers->select("");
    HMODULE module = nullptr;
    if (!r || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(r->render), &module))
        return nullptr;
    return module;
}

// A script whose styles ask for these families, in words no other test
// draws: xy-VSFilter keeps its measured words and outlines in module-wide
// caches, and a cached word selects no font.
std::string probeScript()
{
    return script("",
                  "Style: Times,Times New Roman,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,"
                  "2,8,20,20,20,1\n"
                  "Style: Missing,Hikari No Such Font,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,"
                  "1,2,2,5,20,20,20,1\n"
                  "Style: Styled,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,-1,-1,0,0,100,100,0,0,1,2,2,2,20,"
                  "20,60,1\n",
                  "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an7}Readback arial face\n"
                  "Dialogue: 0,0:00:00.00,0:00:05.00,Times,,0,0,0,,Readback times face\n"
                  "Dialogue: 0,0:00:00.00,0:00:05.00,Missing,,0,0,0,,Readback missing face\n"
                  "Dialogue: 0,0:00:00.00,0:00:05.00,Styled,,0,0,0,,Readback styled face\n");
}

// Draws the probe through the renderers' default renderer with the import
// redirected (and put back before the module could go), and returns the
// faces it selected.
std::set<Face> readBack(const std::shared_ptr<CsriRenderers> &renderers, std::string &problem)
{
    HMODULE module = moduleOf(renderers);
    if (!module) {
        problem = "the renderer's module was not found";
        return {};
    }
    void *previous = patchImport(module, "gdi32.dll", "SelectObject", reinterpret_cast<void *>(&selectObject));
    if (!previous) {
        problem = "the renderer does not import gdi32!SelectObject by name";
        return {};
    }
    realSelectObject = reinterpret_cast<SelectObjectFn>(previous);
    {
        std::lock_guard guard(lock);
        faces.clear();
    }
    {
        CsriRenderer r(renderers, "");
        if (!r.prepare(snapshotOf(probeScript())))
            problem = "the probe script was refused";
        else if (const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight); !frame || frame->empty)
            problem = "the probe drew nothing";
    }
    patchImport(module, "gdi32.dll", "SelectObject", previous);
    std::lock_guard guard(lock);
    return faces;
}

QString line(const Face &f)
{
    const QString requested = QString::fromStdWString(f.requested), selected = QString::fromStdWString(f.selected);
    const bool matched = requested.compare(selected, Qt::CaseInsensitive) == 0;
    return QStringLiteral("%1 (weight %2%3) -> %4 [%5] (%6)")
        .arg(requested)
        .arg(f.weight)
        .arg(f.italic ? QStringLiteral(", italic") : QString())
        .arg(selected, QString::fromStdWString(f.full), matched ? QStringLiteral("matched") : QStringLiteral("substituted"));
}

} // namespace drawn

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

// The faces read back from xy-VSFilter's own font selections while it drew
// (drawn::selectObject): each requested family GDI has is drawn with that
// family, a family it lacks with a substitute, reported as substituted; with
// the legacy release's renderer (HIKARI_LEGACY_CSRI) the two select the same
// faces. Written to vsfilter-drawn-faces.txt.
TEST(VsfilterRecipe, ReadsBackTheFacesItDrewWith)
{
    std::string problem;
    const auto renderers = CsriRenderers::load(builtFolder(), {});
    const auto built = drawn::readBack(renderers, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    const QString artifacts = QStringLiteral(HIKARI_TEST_ARTIFACT_DIR);
    QDir().mkpath(artifacts);
    QFile report(artifacts + QStringLiteral("/vsfilter-drawn-faces.txt"));
    ASSERT_TRUE(report.open(QIODevice::WriteOnly | QIODevice::Text));
    report.write("# built: the faces xy-VSFilter selected while drawing (requested -> GDI's face [full name])\n");
    for (const auto &f : built) {
        report.write((drawn::line(f) + QLatin1Char('\n')).toUtf8());
        std::fprintf(stderr, "W2 drawn %s\n", qPrintable(drawn::line(f)));
    }
    const auto selectedFor = [&](const std::set<drawn::Face> &faces, const wchar_t *family, LONG weight, bool italic) {
        std::set<std::wstring> out;
        for (const auto &f : faces)
            if (_wcsicmp(f.requested.c_str(), family) == 0 && f.weight == weight && f.italic == italic)
                out.insert(f.selected);
        return out;
    };
    const auto same = [](const std::set<std::wstring> &selected, const wchar_t *family) {
        return selected.size() == 1 && _wcsicmp(selected.begin()->c_str(), family) == 0;
    };
    EXPECT_TRUE(same(selectedFor(built, L"Arial", FW_NORMAL, false), L"Arial")) << "Arial";
    EXPECT_TRUE(same(selectedFor(built, L"Arial", FW_BOLD, true), L"Arial")) << "Arial bold italic";
    EXPECT_TRUE(same(selectedFor(built, L"Times New Roman", FW_NORMAL, false), L"Times New Roman")) << "Times New Roman";
    const auto missing = selectedFor(built, L"Hikari No Such Font", FW_NORMAL, false);
    EXPECT_FALSE(missing.empty()) << "the missing family was never selected";
    for (const auto &face : missing)
        EXPECT_NE(_wcsicmp(face.c_str(), L"Hikari No Such Font"), 0) << "a family GDI lacks is drawn with a substitute";

    const auto legacy = legacyFolder();
    if (legacy.empty())
        return;
    const auto old = drawn::readBack(CsriRenderers::load(legacy, {}), problem);
    ASSERT_TRUE(problem.empty()) << "legacy: " << problem;
    report.write("# legacy release (v0.0.1-rc.1)\n");
    for (const auto &f : old)
        report.write((drawn::line(f) + QLatin1Char('\n')).toUtf8());
    EXPECT_EQ(built, old) << "the recipe's renderer and the legacy release's selected different faces";
}
