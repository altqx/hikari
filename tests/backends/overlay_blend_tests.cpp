#include "hikari/backends/overlay_blend.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

using namespace hikari;
using namespace hikari::backends;
using application::OverlayPixels;
using application::PixelRect;

namespace {

std::vector<BlendPath> availablePaths()
{
    std::vector<BlendPath> out;
    for (BlendPath p : {BlendPath::Plain, BlendPath::Sse2, BlendPath::Avx2})
        if (blendPathAvailable(p))
            out.push_back(p);
    return out;
}

const char *nameOf(BlendPath p)
{
    switch (p) {
    case BlendPath::Plain: return "plain";
    case BlendPath::Sse2: return "SSE2";
    case BlendPath::Avx2: return "AVX2";
    }
    return "?";
}

}

TEST(OverlayBlend, ThePathIsOneThisMachineHas)
{
    EXPECT_TRUE(blendPathAvailable(blendPath()));
    EXPECT_TRUE(blendPathAvailable(BlendPath::Plain));
#if defined(_M_X64) || defined(__x86_64__)
    EXPECT_NE(blendPath(), BlendPath::Plain); // SSE2 is the x64 baseline
#endif
    std::printf("blend path: %s\n", nameOf(blendPath()));
}

TEST(OverlayBlend, EveryCoverageAndOpacityMatchesThePlainPath)
{
    constexpr int kW = 256;
    std::vector<std::uint8_t> cover(kW);
    for (int i = 0; i < kW; ++i)
        cover[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    for (BlendPath path : availablePaths()) {
        for (unsigned transparency = 0; transparency < 256; ++transparency) {
            for (unsigned under : {0u, 1u, 77u, 128u, 254u, 255u}) {
                const std::uint32_t color = (0x30A0E000u ^ (under << 16)) | transparency;
                std::vector<std::uint8_t> plain(kW * 4), ours(kW * 4);
                for (int i = 0; i < kW * 4; ++i)
                    plain[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((under + i * 37) & 0xff);
                ours = plain;
                blendAssBitmap(BlendPath::Plain, plain.data(), kW * 4, cover.data(), kW, kW, 1, color);
                blendAssBitmap(path, ours.data(), kW * 4, cover.data(), kW, kW, 1, color);
                ASSERT_EQ(ours, plain) << nameOf(path) << " transparency " << transparency << " under " << under;
            }
        }
    }
}

TEST(OverlayBlend, SampleBitmapsMatchThePlainPath)
{
    std::mt19937 rng(20261013);
    const auto byte = [&] { return static_cast<std::uint8_t>(rng() & 0xff); };
    constexpr int kFrameW = 97, kFrameH = 41, kPitch = kFrameW * 4 + 12;
    for (BlendPath path : availablePaths()) {
        std::vector<std::uint8_t> plain(static_cast<std::size_t>(kPitch) * kFrameH, 0);
        std::vector<std::uint8_t> ours = plain;
        for (int n = 0; n < 400; ++n) {
            const int w = 1 + static_cast<int>(rng() % 70), h = 1 + static_cast<int>(rng() % 12);
            const int x = static_cast<int>(rng() % static_cast<unsigned>(kFrameW - w + 1));
            const int y = static_cast<int>(rng() % static_cast<unsigned>(kFrameH - h + 1));
            const int stride = w + static_cast<int>(rng() % 16);
            std::vector<std::uint8_t> bitmap(static_cast<std::size_t>(stride) * h);
            const unsigned kind = rng() % 4;
            for (auto &b : bitmap) {
                switch (kind) {
                case 0: b = byte(); break;
                case 1: b = (rng() % 3) ? 0 : byte(); break;
                case 2: b = (rng() % 2) ? 255 : 0; break;
                default: b = static_cast<std::uint8_t>(rng() % 8);
                }
            }
            std::uint32_t color = rng();
            if (n % 7 == 0)
                color &= 0xffffff00u;
            if (n % 11 == 0)
                color |= 0xffu;
            const std::size_t at = static_cast<std::size_t>(y) * kPitch + static_cast<std::size_t>(x) * 4;
            blendAssBitmap(BlendPath::Plain, plain.data() + at, kPitch, bitmap.data(), stride, w, h, color);
            blendAssBitmap(path, ours.data() + at, kPitch, bitmap.data(), stride, w, h, color);
            ASSERT_EQ(ours, plain) << nameOf(path) << " bitmap " << n << " " << w << "x" << h;
        }
    }
}

TEST(OverlayBlend, DivisionsRoundAsLegacy)
{
    const std::uint8_t cover = 200;
    std::uint8_t pixel[4] = {0, 0, 0, 0};
    blendAssBitmap(pixel, 4, &cover, 1, 1, 1, 0xFFFFFF40u); // libass RGBT: white, transparency 0x40
    EXPECT_EQ(pixel[3], 150);
    EXPECT_EQ(pixel[0], 150);
}

TEST(OverlayBlend, DrawnBoxesMergeOverlapsAndClearOnlyThem)
{
    DrawnBoxes boxes;
    boxes.add({10, 10, 20, 10});
    boxes.add({12, 11, 20, 10});
    boxes.add({0, 0, 0, 5});
    boxes.add({60, 30, 4, 4});
    ASSERT_EQ(boxes.boxes().size(), 2u);
    EXPECT_EQ(boxes.boxes()[0], (PixelRect{10, 10, 22, 11}));
    EXPECT_EQ(boxes.boxes()[1], (PixelRect{60, 30, 4, 4}));
    DrawnBoxes corners;
    corners.add({0, 0, 4, 4});
    corners.add({90, 50, 4, 4});
    EXPECT_EQ(corners.boxes().size(), 2u);

    constexpr int kW = 80, kH = 50;
    std::vector<std::uint8_t> pixels(kW * kH * 4, 0xAB);
    boxes.zero(pixels.data(), kW * 4);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const bool inside = std::ranges::any_of(boxes.boxes(), [&](const PixelRect &r) {
                return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
            });
            ASSERT_EQ(pixels[static_cast<std::size_t>(y * kW + x) * 4], inside ? 0 : 0xAB) << x << "," << y;
        }
}

TEST(OverlayBlend, BuffersAreReusedOnlyWhenNobodyHoldsThem)
{
    OverlayBuffers buffers;
    EXPECT_TRUE(buffers.setSize(4, 2));
    EXPECT_FALSE(buffers.setSize(4, 2));
    EXPECT_EQ(buffers.shown(), nullptr);
    auto &first = buffers.acquire();
    ASSERT_EQ(first.bytes->size(), 32u);
    (*first.bytes)[0] = 9;
    first.drawn.add({0, 0, 1, 1});
    OverlayPixels held(first.bytes);
    const std::uint8_t *firstBytes = held.data();
    auto &second = buffers.acquire();
    EXPECT_NE(second.bytes->data(), firstBytes);
    (*second.bytes)[4] = 5;
    EXPECT_EQ(held[0], 9);
    EXPECT_EQ(buffers.count(), 2u);
    EXPECT_EQ(buffers.shown()->bytes->data(), second.bytes->data());
    held = OverlayPixels();
    auto &third = buffers.acquire();
    EXPECT_EQ(third.bytes->data(), firstBytes);
    EXPECT_EQ(buffers.count(), 2u);
    EXPECT_TRUE(std::ranges::all_of(*third.bytes, [](std::uint8_t b) { return b == 0; }));
    EXPECT_EQ(buffers.shown()->bytes->data(), firstBytes);
    std::vector<OverlayPixels> holders;
    for (int i = 0; i < 6; ++i)
        holders.emplace_back(buffers.acquire().bytes);
    EXPECT_EQ(buffers.count(), 4u);
    for (std::size_t i = 0; i < holders.size(); ++i)
        for (std::size_t j = i + 1; j < holders.size(); ++j)
            EXPECT_FALSE(holders[i].sameBuffer(holders[j]));
    EXPECT_TRUE(buffers.setSize(2, 2));
    EXPECT_EQ(buffers.count(), 0u);
    EXPECT_EQ(buffers.acquire().bytes->size(), 16u);
}

TEST(OverlayBlend, PixelsAreSharedAndCompareByContent)
{
    const OverlayPixels a{1, 2, 3, 4};
    const OverlayPixels b = a;
    EXPECT_TRUE(a.sameBuffer(b));
    const OverlayPixels c{1, 2, 3, 4};
    EXPECT_EQ(a, c);
    EXPECT_FALSE(a.sameBuffer(c));
    OverlayPixels d = a;
    d.resize(6);
    EXPECT_EQ(d.size(), 6u);
    EXPECT_EQ(d[3], 4);
    EXPECT_EQ(d[5], 0);
    EXPECT_EQ(a.size(), 4u);
    d.assign(2, 7);
    EXPECT_EQ(d, (OverlayPixels{7, 7}));
    EXPECT_TRUE(OverlayPixels().empty());
    EXPECT_FALSE(OverlayPixels().sameBuffer(OverlayPixels()));
}

TEST(OverlayBlend, PremultiplyMatchesThePerPixelPassAndBoundsWhatIsDrawn)
{
    std::mt19937 rng(7);
    for (const auto [w, h] : {std::pair{37, 9}, std::pair{64, 16}, std::pair{1, 1}, std::pair{2, 3}}) {
        const int stride = w * 4;
        std::vector<std::uint8_t> frame(static_cast<std::size_t>(stride) * h, 0);
        int x0 = w, y0 = h, x1 = 0, y1 = 0;
        for (int n = 0; n < (w * h) / 6 + 1; ++n) {
            const int x = static_cast<int>(rng() % static_cast<unsigned>(w));
            const int y = static_cast<int>(rng() % static_cast<unsigned>(h));
            std::uint8_t *p = &frame[static_cast<std::size_t>(y * stride + x * 4)];
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<std::uint8_t>(rng());
            p[3] = (n % 4 == 0) ? 0 : (n % 5 == 0 ? 255 : static_cast<std::uint8_t>(rng()));
            if (p[3]) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x + 1);
                y1 = std::max(y1, y + 1);
            }
        }
        std::vector<std::uint8_t> expected = frame;
        for (std::size_t i = 0; i < expected.size(); i += 4) {
            std::uint8_t *p = &expected[i];
            const unsigned a = p[3];
            if (a == 0) {
                p[0] = p[1] = p[2] = 0;
                continue;
            }
            if (a != 255)
                for (int c = 0; c < 3; ++c)
                    p[c] = static_cast<std::uint8_t>((p[c] * a + 127) / 255);
        }
        const PixelRect bounds = premultiplyStraightAlpha(frame.data(), w, h, stride);
        EXPECT_EQ(frame, expected) << w << "x" << h;
        const PixelRect want = x1 > x0 ? PixelRect{x0, y0, x1 - x0, y1 - y0} : PixelRect{};
        EXPECT_EQ(bounds, want) << w << "x" << h;
    }
    std::vector<std::uint8_t> blank(8 * 4 * 3, 0);
    EXPECT_EQ(premultiplyStraightAlpha(blank.data(), 8, 3, 32), PixelRect{});
}
