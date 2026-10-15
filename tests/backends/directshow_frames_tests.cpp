// W1 / V1: DirectShow samples kept as Y'CbCr planes for the presenter's GPU
// conversion give, converted, exactly the BGRA the CPU conversion (toBgra,
// legacy's coefficients) gives: YV12 and NV12 at even, odd and one-pixel
// sizes, either matrix. The conversion code has no Windows dependency, so
// this runs on every platform.
#include "directshow_frames.h"
#include "hikari/application/ycbcr_frame.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using hikari::application::IndexedFrame;
using namespace hikari::backends::dshow;

namespace {

std::vector<std::uint8_t> noise(std::size_t bytes, unsigned seed)
{
    std::mt19937 random(seed);
    std::vector<std::uint8_t> out(bytes);
    for (auto &b : out)
        b = static_cast<std::uint8_t>(random());
    return out;
}

} // namespace

TEST(DirectShowFrames, PlanesConvertToTheCpuPathsBytes)
{
    const int sizes[][2] = {{38, 10}, {64, 36}, {38, 9}, {7, 4}, {1, 2}};
    for (const Subtype subtype : {Subtype::YV12, Subtype::NV12})
        for (const auto &size : sizes)
            for (const bool bt709 : {false, true}) {
                const SampleFormat format{subtype, size[0], size[1], false};
                const auto sample = noise(sampleBytes(format), 11);
                IndexedFrame bgra, planes, back;
                ASSERT_TRUE(toBgra(format, sample.data(), sample.size(), bt709, bgra));
                ASSERT_TRUE(toYcbcr(format, sample.data(), sample.size(), bt709, planes));
                ASSERT_TRUE(planes.ycbcr.has_value()) << subtypeName(subtype);
                EXPECT_EQ(planes.ycbcr->interleaved, subtype == Subtype::NV12);
                EXPECT_EQ(hikari::application::ycbcrLayoutError(planes), "");
                ASSERT_TRUE(hikari::application::ycbcrToBgra(planes, back));
                ASSERT_EQ(back.width, bgra.width);
                ASSERT_EQ(back.height, bgra.height);
                EXPECT_EQ(back.bgra, bgra.bgra) << subtypeName(subtype) << " " << size[0] << "x" << size[1]
                                                << (bt709 ? " BT.709" : " BT.601");
            }
}

TEST(DirectShowFrames, PackedSamplesStayBgra)
{
    for (const Subtype subtype : {Subtype::RGB32, Subtype::YUY2}) {
        const SampleFormat format{subtype, 6, 3, true};
        const auto sample = noise(sampleBytes(format), 5);
        IndexedFrame bgra, kept;
        ASSERT_TRUE(toBgra(format, sample.data(), sample.size(), false, bgra));
        ASSERT_TRUE(toYcbcr(format, sample.data(), sample.size(), false, kept));
        EXPECT_FALSE(kept.ycbcr.has_value());
        EXPECT_EQ(kept.bgra, bgra.bgra);
    }
}

TEST(DirectShowFrames, AShortSampleGivesNothing)
{
    const SampleFormat format{Subtype::NV12, 8, 4, false};
    const auto sample = noise(sampleBytes(format) - 1, 1);
    IndexedFrame planes;
    EXPECT_FALSE(toYcbcr(format, sample.data(), sample.size(), false, planes));
}
