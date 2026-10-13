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
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
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
                // Whole OUTLINETEXTMETRICW elements, so the structure is aligned;
                // its strings follow it, at offsets from its start.
                std::vector<OUTLINETEXTMETRICW> buffer((size + sizeof(OUTLINETEXTMETRICW) - 1) / sizeof(OUTLINETEXTMETRICW));
                OUTLINETEXTMETRICW *otm = buffer.data();
                if (GetOutlineTextMetricsW(dc, size, otm))
                    f.full = reinterpret_cast<const wchar_t *>(reinterpret_cast<const char *>(otm)
                                                               + reinterpret_cast<std::size_t>(otm->otmpFullName));
            }
            std::lock_guard guard(lock);
            faces.insert(std::move(f));
        }
    }
    return previous;
}

// Points `module`'s import of dll!function at `replacement`; the import's
// previous target, or null when the module does not import it by name.
// Only the module's own import table: xy-VSFilter links MFC statically and
// delay-loads only oleacc.dll (ports/hikari-xy-vsfilter/CMakeLists.txt, as
// VSFilter.props has it), so its SelectFont calls go through that table; a
// renderer reaching gdi32 another way fails the read-back, it never passes.
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

// The built renderer's read-back, taken once per process: its caches would
// answer a second probe without selecting a font.
const std::set<Face> &builtFaces(std::string &problem)
{
    static std::string first;
    static const std::set<Face> faces = readBack(CsriRenderers::load(builtFolder(), {}), first);
    problem = first;
    return faces;
}

// The probe's own selections, by what was asked and what GDI chose (the
// full name is reported, not compared).
std::set<Face> probed(const std::set<Face> &faces)
{
    std::set<Face> out;
    for (const auto &f : faces)
        for (const wchar_t *family : {L"Arial", L"Times New Roman", L"Hikari No Such Font"})
            if (_wcsicmp(f.requested.c_str(), family) == 0)
                out.insert(Face{f.requested, f.weight, f.italic, f.selected, {}});
    return out;
}

// A report file in the test artifacts, open for writing (closed on failure).
std::unique_ptr<QFile> report(const char *name)
{
    const QString artifacts = QStringLiteral(HIKARI_TEST_ARTIFACT_DIR);
    QDir().mkpath(artifacts);
    auto file = std::make_unique<QFile>(artifacts + QLatin1Char('/') + QLatin1String(name));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    return file;
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
// family, a family it lacks with a substitute, reported as substituted.
// Written to vsfilter-drawn-faces.txt.
TEST(VsfilterRecipe, ReadsBackTheFacesItDrewWith)
{
    std::string problem;
    const auto &built = drawn::builtFaces(problem);
    ASSERT_TRUE(problem.empty()) << problem;
    const auto report = drawn::report("vsfilter-drawn-faces.txt");
    ASSERT_TRUE(report);
    report->write("# built: the faces xy-VSFilter selected while drawing (requested -> GDI's face [full name])\n");
    for (const auto &f : built) {
        report->write((drawn::line(f) + QLatin1Char('\n')).toUtf8());
        std::fprintf(stderr, "W2 drawn %s\n", qPrintable(drawn::line(f)));
    }
    const auto selectedFor = [&](const wchar_t *family, LONG weight, bool italic) {
        std::set<std::wstring> out;
        for (const auto &f : built)
            if (_wcsicmp(f.requested.c_str(), family) == 0 && f.weight == weight && f.italic == italic)
                out.insert(f.selected);
        return out;
    };
    const auto same = [](const std::set<std::wstring> &selected, const wchar_t *family) {
        return selected.size() == 1 && _wcsicmp(selected.begin()->c_str(), family) == 0;
    };
    EXPECT_TRUE(same(selectedFor(L"Arial", FW_NORMAL, false), L"Arial")) << "Arial";
    EXPECT_TRUE(same(selectedFor(L"Arial", FW_BOLD, true), L"Arial")) << "Arial bold italic";
    EXPECT_TRUE(same(selectedFor(L"Times New Roman", FW_NORMAL, false), L"Times New Roman")) << "Times New Roman";
    const auto missing = selectedFor(L"Hikari No Such Font", FW_NORMAL, false);
    EXPECT_FALSE(missing.empty()) << "the missing family was never selected";
    for (const auto &face : missing)
        EXPECT_NE(_wcsicmp(face.c_str(), L"Hikari No Such Font"), 0) << "a family GDI lacks is drawn with a substitute";
}

// The same probe through the legacy release's renderer (HIKARI_LEGACY_CSRI):
// for the probe's families (other selections, such as a DC's previous font
// put back, are not compared) both renderers ask for the same weights and
// slants and GDI gives them the same faces. Written to
// vsfilter-drawn-faces-legacy.txt. Skipped without the legacy release.
TEST(VsfilterRecipe, SelectsTheFacesTheLegacyReleaseSelects)
{
    const auto legacy = legacyFolder();
    if (legacy.empty())
        GTEST_SKIP() << "HIKARI_LEGACY_CSRI does not name the legacy release's Csri folder";
    std::string problem;
    const auto built = drawn::probed(drawn::builtFaces(problem));
    ASSERT_TRUE(problem.empty()) << problem;
    const auto old = drawn::probed(drawn::readBack(CsriRenderers::load(legacy, {}), problem));
    ASSERT_TRUE(problem.empty()) << "legacy: " << problem;
    const auto report = drawn::report("vsfilter-drawn-faces-legacy.txt");
    ASSERT_TRUE(report);
    for (const auto &[title, faces] : {std::pair{"# built", &built}, std::pair{"# legacy release (v0.0.1-rc.1)", &old}}) {
        report->write(title);
        report->write("\n");
        for (const auto &f : *faces)
            report->write((drawn::line(f) + QLatin1Char('\n')).toUtf8());
    }
    EXPECT_FALSE(built.empty());
    EXPECT_EQ(built, old) << "the recipe's renderer and the legacy release's selected different faces";
}

namespace {

// The face GDI gives this process for `family` now.
std::wstring gdiFace(const wchar_t *family)
{
    HDC dc = CreateCompatibleDC(nullptr);
    LOGFONTW lf{};
    lf.lfHeight = 36;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, family, _TRUNCATE);
    HFONT font = CreateFontIndirectW(&lf);
    HGDIOBJ old = SelectObject(dc, font);
    wchar_t face[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, face);
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteDC(dc);
    return face;
}

std::shared_ptr<const std::vector<std::byte>> leasedFontBytes()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

// HIKARI_VSFILTERMOD_CSRI (a folder holding the package's VSFiltermod.dll):
// VSFilterMod loaded alone, from a copy in its own folder; null without it.
std::shared_ptr<CsriRenderers> vsfilterModRenderers()
{
    const char *mod = std::getenv("HIKARI_VSFILTERMOD_CSRI");
    if (!mod || !*mod)
        return nullptr;
    const auto folder =
        std::filesystem::path(QDir(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR)).absolutePath().toStdU16String()) /
        "vsfiltermod-csri";
    std::filesystem::create_directories(folder);
    std::filesystem::copy_file(std::filesystem::path(mod) / "VSFiltermod.dll", folder / "VSFiltermod.dll",
                               std::filesystem::copy_options::overwrite_existing);
    return CsriRenderers::load(folder, {});
}

// A Line in `family` (the lease's family or Arial) through `renderers`'
// default renderer, with the Titillium Web lease or none.
std::optional<OverlayFrame> drawLeaseProbe(const std::shared_ptr<CsriRenderers> &renderers, const char *family, bool lease,
                                           std::string &problem)
{
    CsriRenderer r(renderers, "");
    RenderSnapshot s = snapshotOf(script("",
                                         std::string("Style: Probe,") + family +
                                             ",40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,5,"
                                             "20,20,20,1\n",
                                         "Dialogue: 0,0:00:00.00,0:00:05.00,Probe,,0,0,0,,Leased font probe\n"));
    if (lease)
        s.fonts = {FontLease{"Titillium Web", leasedFontBytes()}};
    if (!r.prepare(std::move(s))) {
        problem = "the script was refused";
        return std::nullopt;
    }
    if (lease && gdiFace(L"Titillium Web") != L"Titillium Web") {
        problem = "GDI does not have the leased family while the context lives";
        return std::nullopt;
    }
    auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight);
    if (!frame || frame->empty) {
        problem = "nothing was drawn";
        return std::nullopt;
    }
    return std::move(*frame);
}

// xy-VSFilter's own font selections while it draws the lease's family
// (drawn::selectObject): the faces it selected for it.
std::set<std::wstring> facesDrawnWithALease(const std::shared_ptr<CsriRenderers> &renderers, std::string &problem)
{
    HMODULE module = drawn::moduleOf(renderers);
    if (!module) {
        problem = "the renderer's module was not found";
        return {};
    }
    void *previous = drawn::patchImport(module, "gdi32.dll", "SelectObject", reinterpret_cast<void *>(&drawn::selectObject));
    if (!previous) {
        problem = "the renderer does not import gdi32!SelectObject by name";
        return {};
    }
    drawn::realSelectObject = reinterpret_cast<drawn::SelectObjectFn>(previous);
    {
        std::lock_guard guard(drawn::lock);
        drawn::faces.clear();
    }
    {
        CsriRenderer r(renderers, "");
        RenderSnapshot s = snapshotOf(script("",
                                             "Style: Leased,Titillium Web,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,"
                                             "0,0,0,0,100,100,0,0,1,2,2,5,20,20,20,1\n",
                                             "Dialogue: 0,0:00:00.00,0:00:05.00,Leased,,0,0,0,,Leased read-back words\n"));
        s.fonts = {FontLease{"Titillium Web", leasedFontBytes()}};
        if (!r.prepare(std::move(s)))
            problem = "the script was refused";
        else if (const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight); !frame || frame->empty)
            problem = "nothing was drawn";
    }
    drawn::patchImport(module, "gdi32.dll", "SelectObject", previous);
    std::set<std::wstring> out;
    std::lock_guard guard(drawn::lock);
    for (const auto &f : drawn::faces)
        if (_wcsicmp(f.requested.c_str(), L"Titillium Web") == 0)
            out.insert(f.selected);
    return out;
}

} // namespace

// W2: the snapshot's font leases (the external fonts, as legacy's
// FontEnumerator added its folder with AddFontResourceExW(FR_PRIVATE)) reach
// the CSRI renderer. A Line in Titillium Web, a family only the lease has,
// is drawn while GDI has the family and differs from the same Line in Arial,
// GDI's substitute for a missing family (without the lease the two are drawn
// alike); the process no longer has the family once the context is gone.
// The face each renderer selected is also read back from its own
// selections. HIKARI_VSFILTERMOD_CSRI (a folder holding the package's
// VSFiltermod.dll) adds VSFilterMod.
TEST(VsfilterRecipe, DrawsWithTheSnapshotsLeasedFonts)
{
    ASSERT_TRUE(leasedFontBytes()) << HIKARI_TEST_FONT;
    if (gdiFace(L"Titillium Web") == L"Titillium Web")
        GTEST_SKIP() << "Titillium Web is installed on this system, so a lease proves nothing";
    std::vector<std::pair<std::string, std::shared_ptr<CsriRenderers>>> renderers{
        {"xy-VSFilter", CsriRenderers::load(builtFolder(), {})}};
    if (const auto loaded = vsfilterModRenderers()) {
        EXPECT_EQ(loaded->names(), (std::vector<std::string>{"vsfiltermod_textsub"}));
        renderers.emplace_back("VSFilterMod", loaded);
    }
    for (const auto &[name, list] : renderers) {
        std::string problem;
        const auto arial = drawLeaseProbe(list, "Arial", false, problem);
        ASSERT_TRUE(arial) << name << ": " << problem;
        const auto leased = drawLeaseProbe(list, "Titillium Web", true, problem);
        ASSERT_TRUE(leased) << name << ": " << problem;
        std::size_t differing = 0;
        for (std::size_t i = 0; i < leased->pixels.size(); ++i)
            differing += leased->pixels[i] != arial->pixels[i];
        std::fprintf(stderr, "W2 %s: the leased family's Line differs from Arial's in %zu bytes\n", name.c_str(), differing);
        EXPECT_GT(differing, 0u) << name << ": the leased family was drawn as GDI's substitute";
        image(*leased).save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/lease-%1-titillium.png").arg(QString::fromStdString(name)));
        image(*arial).save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/lease-%1-arial.png").arg(QString::fromStdString(name)));
        EXPECT_NE(gdiFace(L"Titillium Web"), L"Titillium Web") << name << ": the lease outlived its context";
        {
            const auto faces = facesDrawnWithALease(list, problem);
            EXPECT_TRUE(problem.empty()) << name << ": " << problem;
            EXPECT_EQ(faces, (std::set<std::wstring>{L"Titillium Web"})) << name;
            EXPECT_NE(gdiFace(L"Titillium Web"), L"Titillium Web") << name << ": the lease outlived its context";
        }
    }
}

// W2: VSFilterMod takes only BGR_, which blends into the frame and leaves no
// alpha; legacy drew it onto the RGB32 video frame. The adapter's overlay
// (the instant drawn onto black and onto white) composited over a frame must
// give what VSFilterMod draws onto that frame itself, on every fixture.
TEST(VsfilterRecipe, VsfilterModOverlayMatchesDrawingOnTheFrame)
{
    const auto renderers = vsfilterModRenderers();
    if (!renderers)
        GTEST_SKIP() << "HIKARI_VSFILTERMOD_CSRI is not set";
    ASSERT_EQ(renderers->names(), (std::vector<std::string>{"vsfiltermod_textsub"}));
    const csri::Renderer *mod = renderers->select("");
    ASSERT_TRUE(mod);
    // A frame with every level in every channel, as a video frame has.
    std::vector<std::uint8_t> background(std::size_t(kWidth) * kHeight * 4);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            std::uint8_t *p = &background[(std::size_t(y) * kWidth + x) * 4];
            p[0] = static_cast<std::uint8_t>(x * 255 / (kWidth - 1));
            p[1] = static_cast<std::uint8_t>(y * 255 / (kHeight - 1));
            p[2] = static_cast<std::uint8_t>((x + y) % 256);
            p[3] = 0;
        }
    auto out = drawn::report("vsfiltermod-overlay.txt");
    std::size_t frames = 0;
    for (const auto &f : fixtures()) {
        CsriRenderer overlay(renderers, "");
        ASSERT_TRUE(overlay.prepare(snapshotOf(f.text))) << f.name;
        const std::string marked = "\xEF\xBB\xBF" + f.text; // as GetVisible's text
        void *direct = mod->openMem(mod->rend, marked.data(), marked.size(), nullptr);
        ASSERT_TRUE(direct) << f.name;
        const csri::Fmt fmt{csri::BGR_, kWidth, kHeight};
        ASSERT_EQ(mod->requestFmt(direct, &fmt), 0) << f.name;
        for (int ms : f.times) {
            const auto frame = overlay.render(core::DocumentTime(std::int64_t(ms) * 1000), kWidth, kHeight);
            ASSERT_TRUE(frame) << f.name << " " << ms;
            std::vector<std::uint8_t> drawn = background;
            csri::Frame target{};
            target.pixfmt = csri::BGR_;
            target.planes[0] = drawn.data();
            target.strides[0] = kWidth * 4;
            mod->render(direct, &target, ms / 1000.0);
            int worst = 0;
            std::size_t covered = 0, off = 0;
            for (std::size_t i = 0; i < drawn.size(); i += 4) {
                const std::uint8_t *o = &frame->pixels[i];
                covered += o[3] != 0;
                for (int c = 0; c < 3; ++c) {
                    const int composite = o[c] + (background[i + c] * (255 - o[3]) + 127) / 255;
                    const int d = std::abs(composite - int(drawn[i + c]));
                    worst = (std::max)(worst, d);
                    off += d > 1;
                }
            }
            if (out)
                out->write(QStringLiteral("%1 %2 ms: %3 covered pixels, largest channel difference %4, %5 channels off by more than 1\n")
                               .arg(QString::fromLatin1(f.name))
                               .arg(ms)
                               .arg(covered)
                               .arg(worst)
                               .arg(off)
                               .toUtf8());
            std::fprintf(stderr, "W2 VSFilterMod %s %d ms: %zu covered, largest difference %d, %zu off by more than 1\n",
                         f.name, ms, covered, worst, off);
            EXPECT_GT(covered, 0u) << f.name << " " << ms;
            EXPECT_LE(worst, 2) << f.name << " " << ms;
            ++frames;
        }
        mod->close(direct);
    }
    EXPECT_GT(frames, 0u);
}
