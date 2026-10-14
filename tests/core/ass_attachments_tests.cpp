// 38c (user decision, 2026-10-14): a script's [Fonts] are decoded for the
// renderers, which take them as font leases, so the renderer script leaves
// [Fonts] out and keeps [Graphics] for VSFilterMod's image tags.
// tests/fixtures/attachments/attached.ass (make-fixtures.py) carries
// HikariAttachedTest.ttf in [Fonts] and red.png in [Graphics].

#include "hikari/core/ass_attachments.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari::core;

namespace {

std::vector<std::byte> readFixture(const char *name)
{
    QFile f(QStringLiteral(HIKARI_ATTACHMENT_FIXTURES "/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(data.size()));
    std::memcpy(out.data(), data.constData(), out.size());
    return out;
}

std::string text(const std::vector<std::byte> &bytes)
{
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

// Aegisub's encoding, written from its description, not from the decoder.
std::string encode(const std::vector<std::byte> &data)
{
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, data.size() - i);
        unsigned b[3] = {unsigned(data[i]), n > 1 ? unsigned(data[i + 1]) : 0u, n > 2 ? unsigned(data[i + 2]) : 0u};
        const unsigned v[4] = {b[0] >> 2, ((b[0] & 3) << 4) | (b[1] >> 4), ((b[1] & 15) << 2) | (b[2] >> 6), b[2] & 63};
        for (std::size_t k = 0; k <= n; ++k)
            out += static_cast<char>(v[k] + 33);
    }
    return out;
}

Document load(const std::string &script)
{
    std::vector<std::byte> bytes(script.size());
    std::memcpy(bytes.data(), script.data(), script.size());
    return loadAss(bytes).document;
}

} // namespace

TEST(AssAttachments, DecodesTheScriptsFonts)
{
    const auto bytes = readFixture("attached.ass");
    ASSERT_FALSE(bytes.empty());
    const Document document = loadAss(bytes).document;
    const auto fonts = attachedFonts(document);
    ASSERT_EQ(fonts.size(), 1u);
    EXPECT_EQ(fonts[0].name, "HikariAttachedTest_0.ttf");
    EXPECT_EQ(fonts[0].bytes, readFixture("HikariAttachedTest.ttf"));
}

TEST(AssAttachments, DecodesEveryLengthOfTheLastGroup)
{
    for (std::size_t size = 0; size < 12; ++size) {
        std::vector<std::byte> data;
        for (std::size_t i = 0; i < size; ++i)
            data.push_back(static_cast<std::byte>(0xF7 - 37 * i));
        EXPECT_EQ(decodeAssAttachment(encode(data)), data) << size;
    }
    EXPECT_TRUE(decodeAssAttachment("!!!!!").empty()); // one character over is no byte
}

TEST(AssAttachments, KeepsEveryFontOfEverySection)
{
    const std::vector<std::byte> a{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}};
    const std::vector<std::byte> b{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const std::string ea = encode(a);
    const Document document = load("[Script Info]\nScriptType: v4.00+\n\n[Fonts]\nfontname: a.ttf\n" + ea.substr(0, 2) +
                                   "\n" + ea.substr(2) + "\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
                                   "MarginR, MarginV, Effect, Text\n\n[Fonts]\nfontname: b.otf\r\n" + encode(b) + "\r\n");
    const auto fonts = attachedFonts(document);
    ASSERT_EQ(fonts.size(), 2u);
    EXPECT_EQ(fonts[0].name, "a.ttf");
    EXPECT_EQ(fonts[0].bytes, a);
    EXPECT_EQ(fonts[1].name, "b.otf");
    EXPECT_EQ(fonts[1].bytes, b);
    EXPECT_NE(attachedFontsKey(document), AttachedFontsKey{});
    EXPECT_EQ(attachedFontsKey(load("[Script Info]\n")), AttachedFontsKey{});
}

TEST(AssAttachments, TheRendererScriptLeavesTheFontsOutAndKeepsTheGraphics)
{
    const auto bytes = readFixture("attached.ass");
    const Document document = loadAss(bytes).document;
    const std::string saved = text(encodeAss(document));
    const std::string renderer = text(encodeAss(document, AssSaveOptions{.renderer = true}));
    EXPECT_EQ(saved, text(bytes)); // a save keeps both
    EXPECT_EQ(renderer.find("[Fonts]"), std::string::npos);
    EXPECT_EQ(renderer.find("fontname:"), std::string::npos);
    // everything else byte for byte: the script without its [Fonts] section
    const std::size_t from = saved.find("[Fonts]"), to = saved.find("[Graphics]");
    ASSERT_NE(from, std::string::npos);
    ASSERT_NE(to, std::string::npos);
    EXPECT_EQ(renderer, saved.substr(0, from) + saved.substr(to));
    EXPECT_NE(renderer.find("[Graphics]\r\nfilename: red.png\r\n"), std::string::npos);
    EXPECT_NE(renderer.find("\\1img(red.png)"), std::string::npos);
}

// One line in 64 of an attachment begins with '[' ('[' less 33 is 58, the
// top six bits of 0xE8), which the loader takes for a section header as
// legacy's did. The font goes on through it, and the renderer script leaves
// it out with its [Fonts]; a line of [Graphics] like it stays.
TEST(AssAttachments, ALineThatBeginsWithABracketGoesOnWithTheFont)
{
    std::vector<std::byte> font;
    for (int i = 0; i < 240; ++i)
        font.push_back(static_cast<std::byte>(i % 3 ? (0x5A + i) & 0xFF : 0xE8));
    const std::string encoded = encode(font);
    std::string fontLines;
    int bracketLines = 0;
    for (std::size_t i = 0; i < encoded.size(); i += 4) { // a line a group: every one of 0xE8 begins with '['
        fontLines += encoded.substr(i, 4) + "\r\n";
        bracketLines += encoded[i] == '[';
    }
    ASSERT_GT(bracketLines, 10);
    const std::string events = "[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, "
                               "Text\r\nDialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,x\r\n";
    const std::string graphics = "[Graphics]\r\nfilename: g.png\r\n" + fontLines;
    const Document document = load("[Script Info]\r\nScriptType: v4.00+\r\n\r\n[Fonts]\r\nfontname: big.ttf\r\n" +
                                   fontLines + graphics + events);
    const auto fonts = attachedFonts(document);
    ASSERT_EQ(fonts.size(), 1u);
    EXPECT_EQ(fonts[0].name, "big.ttf");
    EXPECT_EQ(fonts[0].bytes, font);
    const std::string renderer = text(encodeAss(document, AssSaveOptions{.renderer = true}));
    EXPECT_EQ(renderer, "[Script Info]\r\nScriptType: v4.00+\r\n\r\n" + graphics + events);
    // a real header ends the font
    const auto two = attachedFonts(load("[Fonts]\r\nfontname: a.ttf\r\n" + encode(font).substr(0, 8) +
                                        "\r\n[Aegisub Extradata]\r\n" + fontLines));
    ASSERT_EQ(two.size(), 1u);
    EXPECT_EQ(two[0].bytes.size(), 6u);
}
