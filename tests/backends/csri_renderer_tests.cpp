// W2: the CSRI adapter against legacy SubtitlesVSFilter / CsriMod (20d647c4),
// through a fake renderer that records the CSRI calls. On Linux this adapter
// is compiled only into this test (the application has libass alone).

#include "hikari/backends/csri_renderer.h"
#include "hikari/backends/subtitle_renderer_selection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::backends;

namespace {

// What the fake renderer saw; one global record, as CSRI has C entry points.
struct Calls {
    std::vector<std::string> opened;
    int closed = 0;
    std::vector<csri::Fmt> requested;
    std::vector<int> refuse; // pixel formats request_fmt refuses
    std::vector<double> times;
    std::vector<int> renderedFormats;
    std::vector<std::ptrdiff_t> strides;
    bool failOpen = false;
    // The pixel the fake draws at (1, 1): straight-alpha BGRA, as xy-VSFilter
    // writes into an MSP_RGBA target (MemSubPic.cpp AlphaBltOther).
    std::uint8_t pixel[4] = {200, 100, 50, 128};
} calls;

int instanceToken = 0;

void *openMem(void *, const void *data, std::size_t length, void *)
{
    if (calls.failOpen)
        return nullptr;
    calls.opened.emplace_back(static_cast<const char *>(data), length);
    return &instanceToken;
}
void closeInstance(void *) { ++calls.closed; }
int requestFmt(void *, const csri::Fmt *fmt)
{
    calls.requested.push_back(*fmt);
    for (int f : calls.refuse)
        if (f == fmt->pixfmt)
            return -1;
    return 0;
}
void render(void *, csri::Frame *frame, double time)
{
    calls.times.push_back(time);
    calls.renderedFormats.push_back(frame->pixfmt);
    calls.strides.push_back(frame->strides[0]);
    unsigned char *p = frame->planes[0] + frame->strides[0] * 1 + 4 * 1;
    std::memcpy(p, calls.pixel, 4);
    // BGR_ is xy-VSFilter's MSP_RGB32 target: its AlphaBlt masks the colour
    // channels and leaves the padding byte 0 (MemSubPic.cpp MSP_RGB32).
    if (frame->pixfmt == csri::BGR_)
        p[3] = 0;
}

const csri::Info xyInfo{"xy-vsfilter_textsub", "3.2", "xy-VSFilter/TextSub", "Gabest", "Copyright"};
const csri::Info modInfo{"vsfiltermod_textsub", "1", "VSFilterMod", "someone", "Copyright"};

csri::Renderer renderer(const csri::Info *info, void *rend)
{
    csri::Renderer r;
    r.rend = rend;
    r.openMem = openMem;
    r.close = closeInstance;
    r.requestFmt = requestFmt;
    r.render = render;
    r.info = info;
    return r;
}

int rendA = 0, rendB = 0, rendC = 0;

std::shared_ptr<CsriRenderers> twoRenderers()
{
    // Legacy loads Csri\VSFiltermod.dll then Csri\xy-VSFilter_hikarisub.dll,
    // each added at the front: xy-VSFilter is first, the default.
    return std::make_shared<CsriRenderers>(
        std::vector<csri::Renderer>{renderer(&xyInfo, &rendA), renderer(&modInfo, &rendB)});
}

std::vector<std::byte> bytes(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

RenderSnapshot snapshotOf(std::string_view script)
{
    RenderSnapshot s;
    s.script = bytes(script);
    return s;
}

class Csri : public ::testing::Test {
protected:
    void SetUp() override { calls = Calls{}; }
    std::vector<std::string> log;
    CsriRenderers::Log logger()
    {
        return [this](const std::string &t) { log.push_back(t); };
    }
};

} // namespace

TEST_F(Csri, ProvidersListTheRenderersThenLibass)
{
    const auto renderers = twoRenderers();
    EXPECT_EQ(renderers->names(), (std::vector<std::string>{"xy-vsfilter_textsub", "vsfiltermod_textsub"}));
    // GetProviders lists nothing when the default renderer has no info, and
    // skips a later renderer without info.
    EXPECT_TRUE(CsriRenderers({renderer(nullptr, &rendA), renderer(&xyInfo, &rendB)}).names().empty());
    EXPECT_EQ(CsriRenderers({renderer(&xyInfo, &rendA), renderer(nullptr, &rendB), renderer(&modInfo, &rendC)}).names(),
              (std::vector<std::string>{"xy-vsfilter_textsub", "vsfiltermod_textsub"}));
}

TEST_F(Csri, TheNamedRendererElseTheDefault)
{
    const auto renderers = twoRenderers();
    EXPECT_EQ(renderers->select("vsfiltermod_textsub")->rend, &rendB);
    EXPECT_EQ(renderers->select("xy-vsfilter_textsub")->rend, &rendA);
    EXPECT_EQ(renderers->select("")->rend, &rendA);
    EXPECT_EQ(renderers->select("not installed")->rend, &rendA); // GetVSFilter's default
    // A renderer without info ends GetVSFilter's walk and is the one used.
    CsriRenderers gap({renderer(&xyInfo, &rendA), renderer(nullptr, &rendB), renderer(&modInfo, &rendC)});
    EXPECT_EQ(gap.select("vsfiltermod_textsub")->rend, &rendB);
    EXPECT_EQ(CsriRenderers({}).select("xy-vsfilter_textsub"), nullptr);
}

TEST_F(Csri, NoRendererIsCannotInitializeCsri)
{
    CsriRenderer r(std::make_shared<CsriRenderers>(std::vector<csri::Renderer>{}), "xy-vsfilter_textsub", logger());
    EXPECT_EQ(r.prepare(snapshotOf("[Script Info]\n")).error(), RenderError::BackendFailure);
    EXPECT_EQ(log, (std::vector<std::string>{"Cannot initialize CSRI."}));
    EXPECT_EQ(r.render(core::DocumentTime(0), 8, 8).error(), RenderError::NoSnapshot);
}

TEST_F(Csri, TheScriptIsOpenedFromMemoryUpToItsFirstNul)
{
    CsriRenderer r(twoRenderers(), "vsfiltermod_textsub", logger());
    std::string script = "[Script Info]\nTitle: x\n";
    script.push_back('\0');
    script += "ignored";
    ASSERT_TRUE(r.prepare(snapshotOf(script)));
    ASSERT_EQ(calls.opened.size(), 1u);
    EXPECT_EQ(calls.opened[0], "[Script Info]\nTitle: x\n"); // strlen(mb_str)
    EXPECT_EQ(r.selectedName(), "vsfiltermod_textsub");
    calls.failOpen = true;
    EXPECT_EQ(r.prepare(snapshotOf("x")).error(), RenderError::InvalidInput);
    EXPECT_EQ(log, (std::vector<std::string>{"Cannot create CSRI instance."}));
    EXPECT_EQ(calls.closed, 1); // the previous instance
}

TEST_F(Csri, TheOverlayIsDrawnWholeOntoAClearedBgraFrame)
{
    CsriRenderer r(twoRenderers(), "", logger());
    const auto generation = r.prepare(snapshotOf("[Script Info]\n"));
    ASSERT_TRUE(generation);
    // 1.2345 s is drawn at the frame's whole milliseconds, in seconds.
    auto frame = r.render(core::DocumentTime(1'234'567), 4, 3);
    ASSERT_TRUE(frame);
    ASSERT_EQ(calls.requested.size(), 1u);
    EXPECT_EQ(calls.requested[0].pixfmt, csri::BGRA);
    EXPECT_EQ(calls.requested[0].width, 4u);
    EXPECT_EQ(calls.requested[0].height, 3u);
    EXPECT_EQ(calls.times, (std::vector<double>{1.234}));
    EXPECT_EQ(calls.strides, (std::vector<std::ptrdiff_t>{16})); // top-down, not swapped
    EXPECT_EQ(frame->generation, *generation);
    EXPECT_FALSE(frame->empty);
    EXPECT_EQ(frame->changed, (std::vector<PixelRect>{{0, 0, 4, 3}}));
    // Straight alpha becomes premultiplied: 200 * 128 / 255 = 100.4 -> 100.
    const std::uint8_t *p = &frame->pixels[1 * 16 + 4];
    EXPECT_EQ(p[0], 100);
    EXPECT_EQ(p[1], 50);
    EXPECT_EQ(p[2], 25);
    EXPECT_EQ(p[3], 128);
    for (std::size_t i = 0; i < frame->pixels.size(); ++i)
        if (i < 20 || i >= 24)
            EXPECT_EQ(frame->pixels[i], 0) << i;
    // The same size is not requested again; the next frame starts cleared.
    calls.pixel[3] = 0;
    frame = r.render(core::DocumentTime(2'000'000), 4, 3);
    ASSERT_TRUE(frame);
    EXPECT_EQ(calls.requested.size(), 1u);
    EXPECT_TRUE(frame->empty);
    EXPECT_EQ(frame->changed, (std::vector<PixelRect>{{0, 0, 4, 3}}));
    EXPECT_TRUE(std::ranges::all_of(frame->pixels, [](std::uint8_t b) { return b == 0; }));
    // A new size is requested again.
    ASSERT_TRUE(r.render(core::DocumentTime(0), 6, 2));
    ASSERT_EQ(calls.requested.size(), 2u);
    EXPECT_EQ(calls.requested[1].width, 6u);
}

TEST_F(Csri, BgraRefusedFallsBackToBgrx)
{
    calls.refuse = {csri::BGRA};
    CsriRenderer r(twoRenderers(), "", logger());
    ASSERT_TRUE(r.prepare(snapshotOf("[Script Info]\n")));
    const auto frame = r.render(core::DocumentTime(0), 4, 4);
    ASSERT_TRUE(frame);
    ASSERT_EQ(calls.requested.size(), 2u);
    EXPECT_EQ(calls.requested[1].pixfmt, csri::BGR_);
    EXPECT_EQ(calls.renderedFormats, (std::vector<int>{csri::BGR_}));
    EXPECT_TRUE(log.empty());
    // The BGR_ output carries no alpha, so nothing shows: legacy uploaded the
    // overlay as D3DFMT_A8R8G8B8 and blended it with D3DBLEND_SRCALPHA
    // (RendererVideo.cpp:411-412, RendererFFMS2.cpp:298-303 and :725), where
    // alpha 0 keeps the video. The fallback opens the instance but draws no
    // subtitles; xy-VSFilter itself accepts BGRA (csriapi.cpp), so only
    // another CSRI renderer reaches it.
    EXPECT_TRUE(frame->empty);
    EXPECT_TRUE(std::ranges::all_of(frame->pixels, [](std::uint8_t b) { return b == 0; }));
}

TEST_F(Csri, AnUnsupportedFormatClosesTheInstance)
{
    calls.refuse = {csri::BGRA, csri::BGR_};
    CsriRenderer r(twoRenderers(), "", logger());
    ASSERT_TRUE(r.prepare(snapshotOf("[Script Info]\n")));
    EXPECT_EQ(r.render(core::DocumentTime(0), 4, 4).error(), RenderError::BackendFailure);
    EXPECT_EQ(log, (std::vector<std::string>{"CSRI does not support this format."}));
    EXPECT_EQ(calls.closed, 1);
    EXPECT_EQ(r.render(core::DocumentTime(0), 4, 4).error(), RenderError::NoSnapshot);
    EXPECT_EQ(r.render(core::DocumentTime(0), 0, 4).error(), RenderError::InvalidSize);
}

#ifndef _WIN32
// Linux evidence for W2: libass is the only renderer, whatever the setting says.
TEST(RendererSelection, LibassIsTheOnlyRendererOnLinux)
{
    SubtitleRendererSelection selection;
    selection.setCsriFolder("/nonexistent/Csri");
    EXPECT_EQ(selection.providers(), (std::vector<std::string>{"libass"}));
    EXPECT_FALSE(selection.select("xy-vsfilter_textsub"));
    EXPECT_TRUE(selection.usesLibass());
    EXPECT_EQ(selection.activeName(), "libass");
    EXPECT_FALSE(selection.select(""));
    EXPECT_TRUE(selection.usesLibass());
}
#endif
