// WS2 (#216): what a CSRI renderer (xy-VSFilter, VSFilterMod) draws for a
// script file read as a player reads it (csri_open_file), so the fixtures
// with and without [Hikari] can be compared frame by frame.
//   csri-probe <renderer.dll> <script.ass>...
// For each script: "<script> open failed", or one line per time with the
// frame's FNV-1a hash and how many bytes the renderer changed, drawn onto
// a gradient at 640x360, BGRA asked for first and BGR_ after.
// Built in the Winix VM by run.ps1 (cl /EHa: a renderer's exception on open
// is reported, not fatal).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct Fmt {
    int pixfmt;
    unsigned width, height;
};
struct Frame {
    int pixfmt;
    unsigned char *planes[4];
    ptrdiff_t strides[4];
};
using DefaultFn = void *(__cdecl *)();
using OpenFileFn = void *(__cdecl *)(void *, const char *, void *);
using CloseFn = void(__cdecl *)(void *);
using RequestFmtFn = int(__cdecl *)(void *, const Fmt *);
using RenderFn = void(__cdecl *)(void *, Frame *, double);

constexpr int kBgra = 2, kBgrx = 0x102;
constexpr unsigned kWidth = 640, kHeight = 360;
const double kTimes[] = {0.0, 0.75, 1.5, 2.25, 3.0, 3.75};

void *open(OpenFileFn openFile, void *rend, const char *path)
{
    try {
        return openFile(rend, path, nullptr);
    } catch (...) {
        return nullptr;
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: csri-probe <renderer.dll> <script>...\n");
        return 2;
    }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) {
        std::printf("%s: LoadLibrary failed (%lu)\n", argv[1], GetLastError());
        return 1;
    }
    auto def = reinterpret_cast<DefaultFn>(GetProcAddress(dll, "csri_renderer_default"));
    auto openFile = reinterpret_cast<OpenFileFn>(GetProcAddress(dll, "csri_open_file"));
    auto close = reinterpret_cast<CloseFn>(GetProcAddress(dll, "csri_close"));
    auto requestFmt = reinterpret_cast<RequestFmtFn>(GetProcAddress(dll, "csri_request_fmt"));
    auto render = reinterpret_cast<RenderFn>(GetProcAddress(dll, "csri_render"));
    void *rend = def ? def() : nullptr;
    if (!openFile || !close || !requestFmt || !render || !rend) {
        std::printf("%s: not a CSRI renderer\n", argv[1]);
        return 1;
    }
    std::vector<unsigned char> background(kWidth * kHeight * 4), frame;
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            unsigned char *p = &background[(y * kWidth + x) * 4];
            p[0] = static_cast<unsigned char>(x * 255 / kWidth);
            p[1] = static_cast<unsigned char>(y * 255 / kHeight);
            p[2] = 96;
            p[3] = 0;
        }
    for (int i = 2; i < argc; ++i) {
        void *inst = open(openFile, rend, argv[i]);
        if (!inst) {
            std::printf("%s open failed\n", argv[i]);
            continue;
        }
        int pixfmt = kBgra;
        Fmt fmt{pixfmt, kWidth, kHeight};
        if (requestFmt(inst, &fmt) != 0) {
            fmt.pixfmt = pixfmt = kBgrx;
            if (requestFmt(inst, &fmt) != 0) {
                std::printf("%s no format\n", argv[i]);
                close(inst);
                continue;
            }
        }
        for (double t : kTimes) {
            frame = background;
            Frame f{pixfmt, {frame.data(), nullptr, nullptr, nullptr}, {kWidth * 4, 0, 0, 0}};
            render(inst, &f, t);
            std::uint64_t hash = 1469598103934665603ull;
            std::size_t changed = 0;
            for (std::size_t b = 0; b < frame.size(); ++b) {
                hash = (hash ^ frame[b]) * 1099511628211ull;
                changed += frame[b] != background[b];
            }
            std::printf("%s %s t=%.2f hash=%016llx changed=%zu\n", argv[i], pixfmt == kBgra ? "BGRA" : "BGR_", t,
                        static_cast<unsigned long long>(hash), changed);
        }
        close(inst);
    }
    return 0;
}
