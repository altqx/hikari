#include <bitlib/bitlib.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

TEST(BitVectorConst, ElementAccessReadsWithoutWriting)
{
    bit::bit_vector<std::uint64_t> bits(130, bit::bit0);
    bits[0] = bit::bit1;
    bits[129] = bit::bit1;
    const auto &view = bits;
    EXPECT_TRUE(static_cast<bool>(view[0]));
    EXPECT_FALSE(static_cast<bool>(view[64]));
    EXPECT_TRUE(static_cast<bool>(view.at(129)));
    EXPECT_TRUE(static_cast<bool>(view.front()));
    EXPECT_TRUE(static_cast<bool>(view.back()));
    EXPECT_THROW(view.at(130), std::out_of_range);
    EXPECT_EQ(bit::count(view.begin(), view.end(), bit::bit1), 2);
    const bit::bit_vector<std::uint64_t> listed{bit::bit1, bit::bit0, bit::bit1};
    EXPECT_EQ(listed.size(), 3u);
    EXPECT_TRUE(static_cast<bool>(listed[2]));
}
