// W1: the DirectShow adapter alone, for diagnosis on a Windows machine and
// for its deployment manifest (dumpbin /dependents of this executable).
//
//   hikari_directshow_probe <media file> [seek ms]...
//   hikari_directshow_probe --graph <media file>
//
// Opens the file through the adapter, prints the graph's filters (legacy's
// Filters menu), the renderer's format and the description, then for each
// position the frame the seek delivered (its start and, for the test
// fixtures, the index its barcode carries), and finally plays one second.
// --graph prints what DirectShow itself makes of the file: the source filter
// AddSourceFilter picks with its pins and media types, and the graph
// RenderFile builds with its seeking capabilities.
#include "hikari/backends/directshow_player.h"

#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace {

std::mutex g_mutex;
std::deque<std::function<void()>> g_tasks;

void post(std::function<void()> task)
{
    std::lock_guard lock(g_mutex);
    g_tasks.push_back(std::move(task));
}

// Runs posted tasks and pumps window messages until `done` or the timeout.
bool runUntil(const std::function<bool()> &done, int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end)
            return false;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::deque<std::function<void()>> tasks;
        {
            std::lock_guard lock(g_mutex);
            tasks.swap(g_tasks);
        }
        for (auto &task : tasks)
            task();
        if (tasks.empty())
            Sleep(2);
    }
    return true;
}

// The fixtures' 4x4 barcode (tests/support/media/media_fixture.cpp).
int barcode(const hikari::application::IndexedFrame &f)
{
    if (f.width <= 0 || f.height <= 0)
        return -1;
    int value = 0;
    for (int by = 0; by < 4; ++by)
        for (int bx = 0; bx < 4; ++bx) {
            const int x = (2 * bx + 1) * f.width / 8, y = (2 * by + 1) * f.height / 8;
            const auto *p = reinterpret_cast<const unsigned char *>(f.bgra.data()) + y * f.stride + x * 4;
            if ((p[0] + p[1] + p[2]) / 3 > 128)
                value |= 1 << (by * 4 + bx);
        }
    return value;
}

std::string guid(const GUID &g)
{
    wchar_t text[64] = {};
    StringFromGUID2(g, text, 64);
    char out[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out, 64, nullptr, nullptr);
    return out;
}

std::string filterName(IBaseFilter *filter)
{
    FILTER_INFO info{};
    if (FAILED(filter->QueryFilterInfo(&info)))
        return "?";
    if (info.pGraph)
        info.pGraph->Release();
    char out[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, info.achName, -1, out, 256, nullptr, nullptr);
    CLSID clsid{};
    filter->GetClassID(&clsid);
    return std::string(out) + " " + guid(clsid);
}

void listFilters(IGraphBuilder *graph)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IEnumFilters> filters;
    if (FAILED(graph->EnumFilters(&filters)))
        return;
    ComPtr<IBaseFilter> filter;
    while (filters->Next(1, &filter, nullptr) == S_OK) {
        std::printf("  filter: %s\n", filterName(filter.Get()).c_str());
        filter.Reset();
    }
}

int dumpGraph(const char *path)
{
    using Microsoft::WRL::ComPtr;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t file[MAX_PATH] = {};
    MultiByteToWideChar(CP_UTF8, 0, path, -1, file, MAX_PATH);
    ComPtr<IGraphBuilder> graph;
    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph));
    if (FAILED(hr))
        return std::printf("no filter graph: 0x%08lx\n", hr), 1;
    ComPtr<IBaseFilter> source;
    hr = graph->AddSourceFilter(file, L"Source Filter", &source);
    std::printf("AddSourceFilter: 0x%08lx\n", hr);
    if (SUCCEEDED(hr)) {
        std::printf("source: %s\n", filterName(source.Get()).c_str());
        ComPtr<IEnumPins> pins;
        source->EnumPins(&pins);
        ComPtr<IPin> pin;
        while (pins && pins->Next(1, &pin, nullptr) == S_OK) {
            PIN_INFO info{};
            pin->QueryPinInfo(&info);
            if (info.pFilter)
                info.pFilter->Release();
            char name[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, info.achName, -1, name, 128, nullptr, nullptr);
            std::printf(" pin %s (%s)\n", name, info.dir == PINDIR_OUTPUT ? "out" : "in");
            ComPtr<IEnumMediaTypes> types;
            hr = pin->EnumMediaTypes(&types);
            if (FAILED(hr)) {
                std::printf("  EnumMediaTypes: 0x%08lx\n", hr);
            } else {
                AM_MEDIA_TYPE *type = nullptr;
                int n = 0;
                while ((hr = types->Next(1, &type, nullptr)) == S_OK && type) {
                    std::printf("  type %s %s %s\n", guid(type->majortype).c_str(), guid(type->subtype).c_str(),
                                guid(type->formattype).c_str());
                    if (type->cbFormat)
                        CoTaskMemFree(type->pbFormat);
                    if (type->pUnk)
                        type->pUnk->Release();
                    CoTaskMemFree(type);
                    ++n;
                }
                std::printf("  %d type(s), last Next 0x%08lx\n", n, hr);
            }
            pin.Reset();
        }
    }
    ComPtr<IGraphBuilder> rendered;
    CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rendered));
    hr = rendered->RenderFile(file, nullptr);
    std::printf("RenderFile: 0x%08lx\n", hr);
    listFilters(rendered.Get());
    ComPtr<IMediaSeeking> seeking;
    DWORD caps = 0;
    if (SUCCEEDED(rendered.As(&seeking)) && SUCCEEDED(seeking->GetCapabilities(&caps)))
        std::printf("seeking capabilities: 0x%lx\n", caps);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: hikari_directshow_probe <media file> [seek ms]... | --graph <media file>\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0); // nothing lost if a filter crashes the process
    if (std::string(argv[1]) == "--graph")
        return argc < 3 ? 2 : dumpGraph(argv[2]);
    using hikari::backends::DirectShowPlayer;
    DirectShowPlayer player(post);
    int lastBarcode = -1;
    std::int64_t lastStart = -1;
    int frames = 0;
    player.setFrameSink([&](hikari::application::IndexedFrame frame, std::int64_t startUs) {
        lastBarcode = barcode(frame);
        lastStart = startUs;
        ++frames;
    });
    player.setOpenFailed([](const std::string &message) { std::printf("open failed: %s\n", message.c_str()); });
    std::optional<bool> opened;
    player.open(argv[1], [&](auto result) {
        opened = result.has_value();
        if (result)
            std::printf("opened: duration %lld us, seekable %d, audio %d, video tracks %zu, audio tracks %zu\n",
                        static_cast<long long>(result->durationUs.value_or(-1)), result->seekable ? 1 : 0,
                        result->audioOutput ? 1 : 0, result->videoTracks.size(), result->audioTracks.size());
    });
    if (!runUntil([&] { return opened.has_value(); }, 10'000) || !*opened)
        return 1;
    for (const auto &f : player.filters())
        std::printf("filter: %s%s\n", f.name.c_str(), f.hasPropertyPages ? " [property pages]" : "");
    const auto format = player.videoFormat();
    std::printf("format: %dx%d %.3f fps %s aspect %d:%d\n", format.width, format.height, format.fps,
                format.subtype.c_str(), format.aspectX, format.aspectY);
    for (const auto &s : player.streams())
        std::printf("stream: %s\n", s.c_str());
    int failures = 0;
    for (int i = 2; i < argc; ++i) {
        const std::int64_t us = std::atoll(argv[i]) * 1000;
        std::optional<std::expected<hikari::application::SeekResult, hikari::application::PlayerError>> result;
        player.seek(us, [&](auto r) { result = std::move(r); });
        if (!runUntil([&] { return result.has_value(); }, 10'000) || !*result) {
            std::printf("seek %lld ms: no frame (%s)\n", static_cast<long long>(us / 1000),
                        !result ? "no answer" : *result ? "answered" : "refused");
            ++failures;
            continue;
        }
        runUntil([] { return false; }, 50); // the sink runs after the answer
        std::printf("seek %lld ms: delivered %lld us, barcode %d\n", static_cast<long long>(us / 1000),
                    static_cast<long long>((*result)->deliveredStartUs), lastBarcode);
    }
    const int before = frames;
    player.play();
    runUntil([] { return false; }, 1000);
    player.pause();
    runUntil([] { return false; }, 200);
    std::printf("played: %d frames in 1 s, last at %lld us, barcode %d\n", frames - before,
                static_cast<long long>(lastStart), lastBarcode);
    return failures == 0 && frames > before ? 0 : 1;
}
