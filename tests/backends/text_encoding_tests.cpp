#include "hikari/backends/text_encoding.h"

#include <gtest/gtest.h>

#include <string>

using namespace hikari::backends;
using namespace std::literals;

namespace {

std::vector<std::byte> bytesOf(std::string_view text)
{
    const auto *data = reinterpret_cast<const std::byte *>(text.data());
    return {data, data + text.size()};
}

std::string text(const std::vector<std::byte> &bytes)
{
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

const std::string kShiftJis = "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd\x81\x41\x90\xa2\x8a\x45\x81\x42"
                              "\x93\xfa\x96\x7b\x8c\xea\x82\xcc\x8e\x9a\x96\x8b\x82\xc5\x82\xb7\x81\x42";
const std::string kUtf8 = "こんにちは、世界。日本語の字幕です。";

}

TEST(TextEncoding, TheReopenListIsTheApprovedSeventeen)
{
    const auto list = reopenEncodings();
    ASSERT_EQ(list.size(), 17u);
    EXPECT_EQ(list.front().name, "UTF-8");
    EXPECT_EQ(list.back().name, "KOI8-R");
    EXPECT_EQ(encodingLabel("Shift_JIS"), "Shift-JIS");
    EXPECT_EQ(encodingLabel("shift_jis"), "Shift-JIS");
    EXPECT_EQ(encodingLabel("x-unknown"), "x-unknown");
}

TEST(TextEncoding, UtfEightIsNotSuggestedAndALegacyFileIs)
{
    EXPECT_FALSE(suggestedEncoding(bytesOf(kUtf8)));
    EXPECT_FALSE(suggestedEncoding(bytesOf("[Script Info]\nTitle: plain\n")));
    EXPECT_EQ(suggestedEncoding(bytesOf("\xff\xfe[\0S\0"sv)), std::optional<std::string>("UTF-16LE"));
    EXPECT_EQ(suggestedEncoding(bytesOf("\xfe\xff\0[\0S"sv)), std::optional<std::string>("UTF-16BE"));
    const auto suggested = suggestedEncoding(bytesOf(kShiftJis + kShiftJis + kShiftJis));
    EXPECT_EQ(suggested, std::optional<std::string>("Shift_JIS"));
}

TEST(TextEncoding, ReadingIsStrictAndDropsTheBom)
{
    EXPECT_EQ(utf8From(bytesOf(kShiftJis), "Shift_JIS").transform(text), std::optional(kUtf8));
    EXPECT_EQ(utf8From(bytesOf("\xef\xbb\xbfok"), "UTF-8").transform(text), std::optional<std::string>("ok"));
    EXPECT_FALSE(utf8From(bytesOf("\x82"), "Shift_JIS"));
    EXPECT_FALSE(utf8From(bytesOf("ok"), "no-such-encoding"));
}

TEST(TextEncoding, WritingListsWhatTheEncodingCannotHold)
{
    const auto japanese = encodeUtf8(bytesOf(kUtf8), "Shift_JIS");
    ASSERT_TRUE(japanese);
    EXPECT_EQ(text(japanese->bytes), kShiftJis);
    EXPECT_TRUE(japanese->unencodable.empty());

    const auto latin = encodeUtf8(bytesOf("\xef\xbb\xbf" "café 日本日"), "ISO-8859-1");
    ASSERT_TRUE(latin);
    EXPECT_EQ(text(latin->bytes), "caf\xe9 \x1a\x1a\x1a");
    EXPECT_EQ(latin->unencodable, U"日本");

    const auto wide = encodeUtf8(bytesOf("A"), "UTF-16LE");
    ASSERT_TRUE(wide);
    EXPECT_EQ(text(wide->bytes), std::string("\xff\xfe" "A\0", 4));
    EXPECT_FALSE(encodeUtf8(bytesOf("\xff"), "ISO-8859-1"));
}
