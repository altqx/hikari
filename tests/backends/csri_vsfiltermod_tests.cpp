#include "hikari/application/font_families.h"
#include "hikari/core/ass_attachments.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/backends/csri_renderer.h"

#include <windows.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
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
    std::vector<int> times;
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
                // Whole OUTLINETEXTMETRICW elements keep it aligned; its strings follow at offsets from its start.
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

HMODULE moduleOf(const std::shared_ptr<CsriRenderers> &renderers)
{
    const auto *r = renderers->select("");
    HMODULE module = nullptr;
    if (!r || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(r->render), &module))
        return nullptr;
    return module;
}

std::unique_ptr<QFile> report(const char *name)
{
    const QString artifacts = QStringLiteral(HIKARI_TEST_ARTIFACT_DIR);
    QDir().mkpath(artifacts);
    auto file = std::make_unique<QFile>(artifacts + QLatin1Char('/') + QLatin1String(name));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    return file;
}

}

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

std::shared_ptr<CsriRenderers> vsfilterModRenderers()
{
    const char *mod = std::getenv("HIKARI_VSFILTERMOD_CSRI");
#ifdef HIKARI_VSFILTERMOD_CSRI_DIR
    if (!mod || !*mod)
        mod = HIKARI_VSFILTERMOD_CSRI_DIR;
#endif
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

std::set<drawn::Face> readBack(const std::shared_ptr<CsriRenderers> &renderers, RenderSnapshot snapshot, std::string &problem)
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
        if (!r.prepare(std::move(snapshot)))
            problem = "the script was refused";
        else if (const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight); !frame || frame->empty)
            problem = "nothing was drawn";
    }
    drawn::patchImport(module, "gdi32.dll", "SelectObject", previous);
    std::lock_guard guard(drawn::lock);
    return drawn::faces;
}

std::set<std::wstring> facesDrawnWithALease(const std::shared_ptr<CsriRenderers> &renderers, std::string &problem)
{
    RenderSnapshot s = snapshotOf(script("",
                                         "Style: Leased,Titillium Web,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,"
                                         "0,0,0,0,100,100,0,0,1,2,2,5,20,20,20,1\n",
                                         "Dialogue: 0,0:00:00.00,0:00:05.00,Leased,,0,0,0,,Leased read-back words\n"));
    s.fonts = {FontLease{"Titillium Web", leasedFontBytes()}};
    std::set<std::wstring> out;
    for (const auto &f : readBack(renderers, std::move(s), problem))
        if (_wcsicmp(f.requested.c_str(), L"Titillium Web") == 0)
            out.insert(f.selected);
    return out;
}

std::wstring gdiFaceFor(const drawn::Face &f)
{
    HDC dc = CreateCompatibleDC(nullptr);
    LOGFONTW lf{};
    lf.lfHeight = 40;
    lf.lfWeight = f.weight;
    lf.lfItalic = f.italic;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, f.requested.c_str(), _TRUNCATE);
    HFONT font = CreateFontIndirectW(&lf);
    HGDIOBJ old = SelectObject(dc, font);
    wchar_t face[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, face);
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteDC(dc);
    return face;
}

QString faceLine(const drawn::Face &f)
{
    const QString requested = QString::fromStdWString(f.requested), selected = QString::fromStdWString(f.selected);
    const bool matched = requested.compare(selected, Qt::CaseInsensitive) == 0;
    return QStringLiteral("%1 (weight %2%3) -> %4 [%5] (%6; GDI alone gives %7)")
        .arg(requested)
        .arg(f.weight)
        .arg(f.italic ? QStringLiteral(", italic") : QString())
        .arg(selected, QString::fromStdWString(f.full), matched ? QStringLiteral("matched") : QStringLiteral("substituted"),
             QString::fromStdWString(gdiFaceFor(f)));
}

struct Capture {
    QString fixture;
    int ms = 0;
};

std::vector<Capture> legacyCaptures(const QString &dir)
{
    QFile f(dir + QStringLiteral("/captures.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    std::vector<Capture> out;
    for (const auto v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        out.push_back({o.value(QStringLiteral("fixture")).toString(), o.value(QStringLiteral("ms")).toInt()});
    }
    return out;
}

std::optional<RenderSnapshot> appSnapshot(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = f.readAll();
    const auto loaded = core::loadAss(std::span(reinterpret_cast<const std::byte *>(bytes.constData()), std::size_t(bytes.size())));
    RenderSnapshot s;
    s.script = core::encodeAss(loaded.document, core::AssSaveOptions{.renderer = true});
    return s;
}

}

TEST(VsfilterMod, LoadsThroughCsri)
{
    const auto renderers = vsfilterModRenderers();
    if (!renderers)
        GTEST_SKIP() << "no VSFilterMod: HIKARI_VSFILTERMOD_CSRI is not set and the build did not fetch it";
    EXPECT_EQ(renderers->names(), (std::vector<std::string>{"vsfiltermod_textsub"}));
    CsriRenderer r(renderers, "vsfiltermod_textsub");
    ASSERT_TRUE(r.prepare(snapshotOf(fixtures().front().text)));
    const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->empty);
    EXPECT_EQ(r.selectedName(), "vsfiltermod_textsub");
}

TEST(VsfilterMod, DrawsWithTheSnapshotsLeasedFonts)
{
    const auto list = vsfilterModRenderers();
    if (!list)
        GTEST_SKIP() << "no VSFilterMod: HIKARI_VSFILTERMOD_CSRI is not set and the build did not fetch it";
    ASSERT_TRUE(leasedFontBytes()) << HIKARI_TEST_FONT;
    if (gdiFace(L"Titillium Web") == L"Titillium Web")
        GTEST_SKIP() << "Titillium Web is installed on this system, so a lease proves nothing";
    ASSERT_EQ(list->names(), (std::vector<std::string>{"vsfiltermod_textsub"}));
    std::string problem;
    const auto arial = drawLeaseProbe(list, "Arial", false, problem);
    ASSERT_TRUE(arial) << problem;
    const auto leased = drawLeaseProbe(list, "Titillium Web", true, problem);
    ASSERT_TRUE(leased) << problem;
    std::size_t differing = 0;
    for (std::size_t i = 0; i < leased->pixels.size(); ++i)
        differing += leased->pixels[i] != arial->pixels[i];
    std::fprintf(stderr, "W2 VSFilterMod: the leased family's Line differs from Arial's in %zu bytes\n", differing);
    EXPECT_GT(differing, 0u) << "the leased family was drawn as GDI's substitute";
    image(*leased).save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/lease-VSFilterMod-titillium.png"));
    image(*arial).save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/lease-VSFilterMod-arial.png"));
    EXPECT_NE(gdiFace(L"Titillium Web"), L"Titillium Web") << "the lease outlived its context";
    const auto faces = facesDrawnWithALease(list, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_EQ(faces, (std::set<std::wstring>{L"Titillium Web"}));
    EXPECT_NE(gdiFace(L"Titillium Web"), L"Titillium Web") << "the lease outlived its context";
}

TEST(VsfilterMod, AFontsReloadReachesTheKeptRenderer)
{
    const auto loaded = vsfilterModRenderers();
    if (!loaded)
        GTEST_SKIP() << "no VSFilterMod: HIKARI_VSFILTERMOD_CSRI is not set and the build did not fetch it";
    ASSERT_TRUE(leasedFontBytes()) << HIKARI_TEST_FONT;
    if (gdiFace(L"Titillium Web") == L"Titillium Web")
        GTEST_SKIP() << "Titillium Web is installed on this system, so a reload proves nothing";
    const std::vector<std::pair<std::string, std::shared_ptr<CsriRenderers>>> renderers{{"VSFilterMod", loaded}};
    const auto artifacts = std::filesystem::path(QDir(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR)).absolutePath().toStdU16String());
    const application::FontNameLower lower = [](char16_t c) { return static_cast<char16_t>(std::towlower(c)); };
    for (const auto &entry : renderers) {
        const std::string &name = entry.first;
        const auto folder = artifacts / ("reload-" + name);
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        std::vector<std::string> events;
        const CsriRenderers::FontLoader addToProcess = entry.second->fontLoader();
        ASSERT_TRUE(addToProcess) << name;
        entry.second->setFontLoader([&events, addToProcess](const FontLease &font) -> std::shared_ptr<void> {
            auto handle = addToProcess(font);
            if (!handle)
                return nullptr;
            events.push_back("add " + font.family);
            return std::shared_ptr<void>(handle.get(), [&events, handle, family = font.family](void *) mutable {
                events.push_back("remove " + family);
                handle.reset();
            });
        });
        std::vector<FontLease> leases;
        const auto readFolder = [&](const char *step) {
            const auto load = application::loadExternalFonts(folder.u16string() + u"\\", lower);
            EXPECT_TRUE(load.opened) << name << ", " << step;
            leases.clear();
            for (const auto &font : load.fonts)
                leases.push_back(FontLease{font.name, font.bytes});
        };
        {
            CsriRenderer r(entry.second, "");
            // Words no other test draws: VSFilter keeps module-wide caches.
            const std::string text = script("",
                                            "Style: Reload,Titillium Web,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,"
                                            "0,100,100,0,0,1,2,2,5,20,20,20,1\n",
                                            "Dialogue: 0,0:00:00.00,0:00:05.00,Reload,,0,0,0,,Reloaded external words\n");
            const auto draw = [&](const char *step) -> std::optional<OverlayFrame> {
                RenderSnapshot s = snapshotOf(text);
                s.fonts = leases;
                if (!r.prepare(std::move(s))) {
                    ADD_FAILURE() << name << ", " << step << ": the script was refused";
                    return std::nullopt;
                }
                auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight);
                if (!frame || frame->empty) {
                    ADD_FAILURE() << name << ", " << step << ": nothing was drawn";
                    return std::nullopt;
                }
                image(*frame).save(QString::fromStdU16String((artifacts / ("reload-" + name + "-" + step + ".png")).u16string()));
                return std::move(*frame);
            };
            readFolder("before");
            const auto before = draw("before");
            ASSERT_TRUE(before);
            EXPECT_NE(gdiFace(L"Titillium Web"), L"Titillium Web") << name;

            std::filesystem::copy_file(std::filesystem::path(HIKARI_TEST_FONT), folder / "TitilliumWeb-Regular.ttf");
            reloadSubtitleFonts();
            readFolder("added");
            ASSERT_EQ(leases.size(), 1u) << name;
            const std::string family = leases.front().family;
            const auto added = draw("added");
            ASSERT_TRUE(added);
            EXPECT_EQ(gdiFace(L"Titillium Web"), L"Titillium Web") << name << ": the added font is not in the process";
            std::size_t differing = 0;
            for (std::size_t i = 0; i < added->pixels.size(); ++i)
                differing += added->pixels[i] != before->pixels[i];
            std::fprintf(stderr, "W2 %s: after the reload that added the font, the Line differs in %zu bytes\n", name.c_str(),
                         differing);
            EXPECT_GT(differing, 0u) << name << ": the font added to the folder was not drawn after the reload";

            events.clear();
            const auto unchanged = draw("unchanged");
            ASSERT_TRUE(unchanged);
            EXPECT_TRUE(events.empty()) << name << ": a prepare without a reload added the kept lease again";
            reloadSubtitleFonts();
            const auto kept = draw("kept");
            ASSERT_TRUE(kept);
            EXPECT_EQ(events, (std::vector<std::string>{"add " + family, "remove " + family}))
                << name << ": a reload with the same lease did not add it to the process again";
            EXPECT_EQ(gdiFace(L"Titillium Web"), L"Titillium Web") << name << ": the kept lease left the process";
            EXPECT_TRUE(kept->pixels == added->pixels) << name << ": the kept lease is not drawn after the reload";

            std::filesystem::remove(folder / "TitilliumWeb-Regular.ttf");
            reloadSubtitleFonts();
            readFolder("removed");
            const auto removed = draw("removed");
            ASSERT_TRUE(removed);
            EXPECT_NE(gdiFace(L"Titillium Web"), L"Titillium Web") << name << ": the removed font is still in the process";
            EXPECT_TRUE(removed->pixels == before->pixels) << name << ": the removed font is still drawn after the reload";
        }
        entry.second->setFontLoader(addToProcess);
    }
}

TEST(VsfilterMod, OverlayMatchesDrawingOnTheFrame)
{
    const auto renderers = vsfilterModRenderers();
    if (!renderers)
        GTEST_SKIP() << "no VSFilterMod: HIKARI_VSFILTERMOD_CSRI is not set and the build did not fetch it";
    ASSERT_EQ(renderers->names(), (std::vector<std::string>{"vsfiltermod_textsub"}));
    const csri::Renderer *mod = renderers->select("");
    ASSERT_TRUE(mod);
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
        const std::string marked = "\xEF\xBB\xBF" + f.text;
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

TEST(VsfilterMod, DrawsTheScriptsAttachedImageAndFonts)
{
    const auto renderers = vsfilterModRenderers();
    if (!renderers)
        GTEST_SKIP() << "no VSFilterMod: HIKARI_VSFILTERMOD_CSRI is not set and the build did not fetch it";
    QFile f(QStringLiteral(HIKARI_ATTACHMENT_FIXTURES "/attached.ass"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    std::vector<std::byte> source(static_cast<std::size_t>(data.size()));
    std::memcpy(source.data(), data.constData(), source.size());
    const core::Document document = core::loadAss(source).document;
    std::vector<FontLease> leases;
    for (auto &font : core::attachedFonts(document))
        leases.push_back({font.name, std::make_shared<const std::vector<std::byte>>(std::move(font.bytes))});
    ASSERT_EQ(leases.size(), 1u);
    const auto rendererScript = core::encodeAss(document, core::AssSaveOptions{.renderer = true});
    std::string script(reinterpret_cast<const char *>(rendererScript.data()), rendererScript.size());
    ASSERT_EQ(script.find("[Fonts]"), std::string::npos);
    const auto pixel = [](const OverlayFrame &frame, int x, int y) {
        return &frame.pixels[std::size_t(y) * frame.stride + std::size_t(x) * 4];
    };

    // Also drops the "HH" Line: VSFilterMod keeps the fonts it made, so a substitute made now would stand in later.
    {
        std::string bare = script;
        const auto graphics = bare.find("[Graphics]"), events = bare.find("[Events]");
        ASSERT_NE(graphics, std::string::npos);
        ASSERT_NE(events, std::string::npos);
        bare.erase(graphics, events - graphics);
        const auto hh = bare.find("}HH\r\n");
        ASSERT_NE(hh, std::string::npos);
        const auto line = bare.rfind("Dialogue:", hh);
        bare.erase(line, hh + 5 - line);
        CsriRenderer r(renderers, "");
        ASSERT_TRUE(r.prepare(snapshotOf(bare)));
        const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight);
        ASSERT_TRUE(frame);
        const std::uint8_t *p = pixel(*frame, 450, 90);
        std::fprintf(stderr, "38c VSFilterMod: without [Graphics] the square is %d,%d,%d,%d (BGRA)\n", p[0], p[1], p[2], p[3]);
        EXPECT_TRUE(p[0] > 200 && p[1] > 200 && p[2] > 200 && p[3] > 200) << "VSFilterMod drew no white box";
        image(*frame).save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/attached-without-graphics.png"));
    }

    CsriRenderer r(renderers, "");
    RenderSnapshot snapshot = snapshotOf(script);
    snapshot.fonts = leases;
    ASSERT_TRUE(r.prepare(std::move(snapshot)));
    const auto frame = r.render(core::DocumentTime(1'000'000), kWidth, kHeight);
    ASSERT_TRUE(frame);
    image(*frame).save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/attached.png"));
    const std::uint8_t *p = pixel(*frame, 450, 90);
    std::fprintf(stderr, "38c VSFilterMod: with [Graphics] the square is %d,%d,%d,%d (BGRA)\n", p[0], p[1], p[2], p[3]);
    long red = 0;
    for (int y = 45; y < 135; ++y)
        for (int x = 405; x < 495; ++x) {
            const std::uint8_t *q = pixel(*frame, x, y);
            red += q[0] < 30 && q[1] < 30 && q[2] > 225 && q[3] > 225;
        }
    EXPECT_EQ(red, 90L * 90L) << "the square is not filled with red.png";
    int left = kWidth, top = kHeight, right = -1, bottom = -1;
    long covered = 0;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < 360; ++x)
            if (pixel(*frame, x, y)[3] >= 128) {
                ++covered;
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    const double fill = right < left ? 0 : double(covered) / ((right - left + 1.0) * (bottom - top + 1.0));
    std::fprintf(stderr, "38c VSFilterMod: \"HH\" covers %ld pixels, %.2f of [%d,%d]-[%d,%d]\n", covered, fill, left, top,
                 right, bottom);
    EXPECT_GT(fill, 0.9) << "\"HH\" was not drawn in the attached font";
}

// VSFilterMod caches measured words module-wide and a cached word selects no font: use words no other test draws.
TEST(VsfilterMod, ReadsBackTheFacesItDrewWith)
{
    const auto renderers = vsfilterModRenderers();
    if (!renderers)
        GTEST_SKIP() << "no VSFilterMod: HIKARI_VSFILTERMOD_CSRI is not set and the build did not fetch it";
    ASSERT_NE(gdiFace(L"Hikari No Such Font"), L"Hikari No Such Font");
    std::string problem;
    const auto faces = readBack(
        renderers,
        snapshotOf(script("",
                          "Style: Times,Times New Roman,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,"
                          "1,2,2,8,20,20,20,1\n"
                          "Style: Missing,Hikari No Such Font,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,"
                          "100,0,0,1,2,2,5,20,20,20,1\n"
                          "Style: Styled,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,-1,-1,0,0,100,100,0,0,1,2,2,"
                          "2,20,20,60,1\n",
                          "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\an7}Modreadback arial face\n"
                          "Dialogue: 0,0:00:00.00,0:00:05.00,Times,,0,0,0,,Modreadback times face\n"
                          "Dialogue: 0,0:00:00.00,0:00:05.00,Missing,,0,0,0,,Modreadback missing face\n"
                          "Dialogue: 0,0:00:00.00,0:00:05.00,Styled,,0,0,0,,Modreadback styled face\n")),
        problem);
    ASSERT_TRUE(problem.empty()) << problem;
    const auto out = drawn::report("vsfiltermod-drawn-faces.txt");
    ASSERT_TRUE(out);
    out->write("# the faces VSFilterMod selected while drawing (requested -> GDI's face [full name])\n");
    for (const auto &f : faces) {
        out->write((faceLine(f) + QLatin1Char('\n')).toUtf8());
        std::fprintf(stderr, "W2 VSFilterMod drew %s\n", qPrintable(faceLine(f)));
    }
    const auto drawnFor = [&](const wchar_t *family, LONG weight, bool italic) {
        std::set<std::wstring> selected;
        for (const auto &f : faces)
            if (_wcsicmp(f.requested.c_str(), family) == 0 && f.weight == weight && f.italic == italic)
                selected.insert(f.selected);
        return selected;
    };
    EXPECT_EQ(drawnFor(L"Arial", FW_NORMAL, false), (std::set<std::wstring>{L"Arial"})) << "Arial";
    EXPECT_EQ(drawnFor(L"Arial", FW_BOLD, true), (std::set<std::wstring>{L"Arial"})) << "Arial bold italic";
    EXPECT_EQ(drawnFor(L"Times New Roman", FW_NORMAL, false), (std::set<std::wstring>{L"Times New Roman"}))
        << "Times New Roman";
    // GDI's outline-metrics full name ("Monotype:Arial Bold Italic (Microsoft)") names the real face, not a synthesised one.
    std::size_t boldItalic = 0;
    for (const auto &f : faces)
        if (_wcsicmp(f.requested.c_str(), L"Arial") == 0 && f.weight == FW_BOLD && f.italic) {
            EXPECT_NE(f.full.find(L"Arial Bold Italic"), std::wstring::npos) << QString::fromStdWString(f.full).toStdString();
            ++boldItalic;
        }
    EXPECT_GT(boldItalic, 0u);
    const auto missing = drawnFor(L"Hikari No Such Font", FW_NORMAL, false);
    ASSERT_FALSE(missing.empty()) << "the missing family was never selected";
    for (const auto &f : faces)
        if (_wcsicmp(f.requested.c_str(), L"Hikari No Such Font") == 0) {
            EXPECT_NE(_wcsicmp(f.selected.c_str(), L"Hikari No Such Font"), 0) << "a family GDI lacks is drawn with a substitute";
            EXPECT_EQ(f.selected, gdiFaceFor(f)) << "the substitute is not the one GDI gives the request";
        }
}

TEST(VsfilterMod, MatchesTheLegacyAppsCaptures)
{
    const auto renderers = vsfilterModRenderers();
    if (!renderers)
        GTEST_SKIP() << "no VSFilterMod: HIKARI_VSFILTERMOD_CSRI is not set and the build did not fetch it";
    const QString dir = QStringLiteral(HIKARI_W2_LEGACY_OBSERVATIONS);
    const auto captures = legacyCaptures(dir);
    ASSERT_FALSE(captures.empty()) << qPrintable(dir);
    auto out = drawn::report("vsfiltermod-legacy.txt");
    ASSERT_TRUE(out);
    const QImage plain = QImage(dir + QStringLiteral("/frames/background.png")).convertToFormat(QImage::Format_RGB32);
    ASSERT_EQ(plain.size(), QSize(kWidth, kHeight));
    std::size_t compared = 0;
    QString prepared;
    std::optional<CsriRenderer> r;
    for (const auto &c : captures) {
        const QString name = QStringLiteral("%1-%2").arg(c.fixture).arg(c.ms);
        SCOPED_TRACE(name.toStdString());
        if (prepared != c.fixture) {
            const auto snapshot = appSnapshot(dir + QStringLiteral("/scripts/") + c.fixture + QStringLiteral(".ass"));
            ASSERT_TRUE(snapshot);
            r.emplace(renderers, "");
            ASSERT_TRUE(r->prepare(*snapshot));
            prepared = c.fixture;
        }
        const QImage subbed =
            QImage(dir + QStringLiteral("/frames/") + name + QStringLiteral(".png")).convertToFormat(QImage::Format_RGB32);
        ASSERT_EQ(subbed.size(), QSize(kWidth, kHeight));
        const auto frame = r->render(core::DocumentTime(std::int64_t(c.ms) * 1000), kWidth, kHeight);
        ASSERT_TRUE(frame);
        QImage composite(kWidth, kHeight, QImage::Format_RGB32), diff(kWidth, kHeight, QImage::Format_RGB32);
        int worst = 0;
        std::size_t covered = 0, legacyDrawn = 0, off = 0;
        for (int y = 0; y < kHeight; ++y) {
            const auto *bg = reinterpret_cast<const std::uint8_t *>(plain.constScanLine(y));
            const auto *want = reinterpret_cast<const std::uint8_t *>(subbed.constScanLine(y));
            auto *got = reinterpret_cast<std::uint8_t *>(composite.scanLine(y));
            auto *d = reinterpret_cast<std::uint8_t *>(diff.scanLine(y));
            const std::uint8_t *o = frame->pixels.data() + std::size_t(y) * frame->stride;
            for (int x = 0; x < kWidth; ++x) {
                const int i = x * 4;
                covered += o[i + 3] != 0;
                legacyDrawn += std::memcmp(bg + i, want + i, 3) != 0;
                int pixelWorst = 0;
                for (int ch = 0; ch < 3; ++ch) {
                    got[i + ch] = static_cast<std::uint8_t>(o[i + ch] + (bg[i + ch] * (255 - o[i + 3]) + 127) / 255);
                    const int delta = std::abs(int(got[i + ch]) - int(want[i + ch]));
                    pixelWorst = (std::max)(pixelWorst, delta);
                    off += delta > 2;
                }
                got[i + 3] = 255;
                worst = (std::max)(worst, pixelWorst);
                const auto level = static_cast<std::uint8_t>((std::min)(255, pixelWorst * 32));
                d[i] = d[i + 1] = d[i + 2] = level;
                d[i + 3] = 255;
            }
        }
        out->write(QStringLiteral("%1: %2 covered pixels, %3 pixels legacy drew on, largest channel difference %4, "
                                  "%5 channels off by more than 2\n")
                       .arg(name)
                       .arg(covered)
                       .arg(legacyDrawn)
                       .arg(worst)
                       .arg(off)
                       .toUtf8());
        std::fprintf(stderr, "W2 VSFilterMod vs legacy %s: %zu covered, %zu legacy, largest difference %d, %zu off by more than 2\n",
                     qPrintable(name), covered, legacyDrawn, worst, off);
        EXPECT_GT(covered, 0u);
        EXPECT_GT(legacyDrawn, 0u) << "legacy's capture has no subtitles on it";
        EXPECT_LE(worst, 2);
        if (worst > 2) {
            composite.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/") + name + QStringLiteral("-composite.png"));
            diff.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/") + name + QStringLiteral("-diff.png"));
        }
        ++compared;
    }
    EXPECT_EQ(compared, captures.size());
}
