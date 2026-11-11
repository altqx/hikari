#include "hikari/backends/lyrics_text.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace hikari;
using core::LyricsEncoding;

namespace {

std::vector<std::byte> fixture(const std::string &name)
{
    std::ifstream in(std::string(HIKARI_LYRICS_FIXTURES) + "/" + name, std::ios::binary);
    EXPECT_TRUE(in) << name;
    const std::string text(std::istreambuf_iterator<char>(in), {});
    std::vector<std::byte> out(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
        out[i] = static_cast<std::byte>(text[i]);
    return out;
}

std::string text(const std::optional<std::vector<std::byte>> &bytes)
{
    if (!bytes)
        return "<not text>";
    return std::string(reinterpret_cast<const char *>(bytes->data()), bytes->size());
}

const std::string kSimplified = "[ti:夜空]\r\n[ar:星光乐队]\r\n[00:01.00]星星在夜里闪烁\r\n[00:04.00]风轻轻吹过窗台\r\n";

}

TEST(LyricsText, EachEncodingDecodesToTheSameText)
{
    EXPECT_EQ(text(backends::lyricsUtf8(fixture("gb18030.lrc"), LyricsEncoding::Gb18030)), kSimplified);
    EXPECT_EQ(text(backends::lyricsUtf8(fixture("utf16le.lrc"), LyricsEncoding::Utf16Le)), kSimplified);
    EXPECT_EQ(text(backends::lyricsUtf8(fixture("utf16be.lrc"), LyricsEncoding::Utf16Be)), kSimplified);
    EXPECT_EQ(text(backends::lyricsUtf8(fixture("big5.lrc"), LyricsEncoding::Big5)),
              "[ti:夜空]\r\n[00:01.00]星星在夜裡閃爍\r\n[00:04.00]風輕輕吹過窗台\r\n");
    const std::string utf8 = text(backends::lyricsUtf8(fixture("enhanced.lrc"), LyricsEncoding::Utf8));
    EXPECT_TRUE(utf8.starts_with("[ti:Twinkle")) << utf8.substr(0, 12);
}

TEST(LyricsText, EncodingsAreDetected)
{
    EXPECT_EQ(backends::detectLyricsEncoding(fixture("utf16le.lrc")), LyricsEncoding::Utf16Le);
    EXPECT_EQ(backends::detectLyricsEncoding(fixture("utf16be.lrc")), LyricsEncoding::Utf16Be);
    EXPECT_EQ(backends::detectLyricsEncoding(fixture("decoded.krc")), LyricsEncoding::Utf8);
    EXPECT_EQ(backends::detectLyricsEncoding(fixture("gb18030.lrc")), LyricsEncoding::Gb18030);
}

TEST(LyricsText, BytesThatAreNotTextInTheEncodingAreRefused)
{
    EXPECT_EQ(backends::lyricsUtf8(fixture("gb18030.lrc"), LyricsEncoding::Utf8), std::nullopt);
    EXPECT_EQ(backends::lyricsUtf8(fixture("utf16le.lrc"), LyricsEncoding::Utf8), std::nullopt);
}
