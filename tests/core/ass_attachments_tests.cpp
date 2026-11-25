#include "hikari/core/ass_attachments.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/srt.h"

#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
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

}

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
    EXPECT_TRUE(decodeAssAttachment("!!!!!").empty());
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
    EXPECT_EQ(saved, text(bytes));
    EXPECT_EQ(renderer.find("[Fonts]"), std::string::npos);
    EXPECT_EQ(renderer.find("fontname:"), std::string::npos);
    const std::size_t from = saved.find("[Fonts]"), to = saved.find("[Graphics]");
    ASSERT_NE(from, std::string::npos);
    ASSERT_NE(to, std::string::npos);
    EXPECT_EQ(renderer, saved.substr(0, from) + saved.substr(to));
    EXPECT_NE(renderer.find("[Graphics]\r\nfilename: red.png\r\n"), std::string::npos);
    EXPECT_NE(renderer.find("\\1img(red.png)"), std::string::npos);
}

// Aegisub's encoding begins one line in 64 with '[', which reads as a section header.
TEST(AssAttachments, ALineThatBeginsWithABracketGoesOnWithTheFont)
{
    std::vector<std::byte> font;
    for (int i = 0; i < 240; ++i)
        font.push_back(static_cast<std::byte>(i % 3 ? (0x5A + i) & 0xFF : 0xE8));
    const std::string encoded = encode(font);
    std::string fontLines;
    int bracketLines = 0;
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
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
    const auto two = attachedFonts(load("[Fonts]\r\nfontname: a.ttf\r\n" + encode(font).substr(0, 8) +
                                        "\r\n[Aegisub Extradata]\r\n" + fontLines));
    ASSERT_EQ(two.size(), 1u);
    EXPECT_EQ(two[0].bytes.size(), 6u);
}

namespace {

std::vector<std::byte> pattern(std::size_t size, int seed)
{
    std::vector<std::byte> data;
    for (std::size_t i = 0; i < size; ++i)
        data.push_back(static_cast<std::byte>((seed + 37 * i + i * i) & 0xFF));
    return data;
}

std::string block(const std::string &first, const std::vector<std::byte> &data, const char *newline)
{
    std::string out = first + newline;
    for (const auto &line : encodeAssAttachment(data))
        out += std::string(line.begin(), line.end()) + newline;
    return out;
}

std::vector<std::byte> sfnt(std::uint16_t fsType, bool collection)
{
    std::vector<std::byte> font;
    const auto put = [&](std::uint32_t value, int width) {
        for (int shift = (width - 1) * 8; shift >= 0; shift -= 8)
            font.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
    };
    const std::uint32_t base = collection ? 16 : 0;
    if (collection) {
        put(0x74746366, 4);
        put(0x00010000, 4);
        put(1, 4);
        put(base, 4);
    }
    put(0x00010000, 4);
    put(1, 2);
    put(0, 6);
    put(0x4F532F32, 4);
    put(0, 4);
    put(base + 28, 4);
    put(10, 4);
    put(0x0004, 2);
    put(500, 2);
    put(400, 2);
    put(5, 2);
    put(fsType, 2);
    return font;
}

}

TEST(AssAttachments, EncodesInLinesOfEightyThatDecodeBack)
{
    for (std::size_t size : {0u, 1u, 2u, 3u, 59u, 60u, 61u, 62u, 121u, 1000u}) {
        const auto data = pattern(size, 11);
        const auto lines = encodeAssAttachment(data);
        std::string joined;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            EXPECT_LE(lines[i].size(), 80u) << size;
            if (i + 1 < lines.size())
                EXPECT_EQ(lines[i].size(), 80u) << size;
            joined += std::string(lines[i].begin(), lines[i].end());
        }
        EXPECT_EQ(joined, encode(data)) << size;
        EXPECT_EQ(decodeAssAttachment(joined), data) << size;
    }
}

TEST(AssAttachments, ListsFontsAndGraphicsWithTheirDecodedSizes)
{
    const auto bytes = readFixture("attached.ass");
    const Document document = loadAss(bytes).document;
    const auto list = attachments(document);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].kind, AttachmentKind::Font);
    EXPECT_EQ(list[0].name, "HikariAttachedTest_0.ttf");
    EXPECT_EQ(list[0].size, readFixture("HikariAttachedTest.ttf").size());
    EXPECT_EQ(attachmentBytes(document, 0), readFixture("HikariAttachedTest.ttf"));
    EXPECT_EQ(list[1].kind, AttachmentKind::Graphic);
    EXPECT_EQ(list[1].name, "red.png");
    EXPECT_EQ(list[1].size, readFixture("red.png").size());
    EXPECT_EQ(attachmentBytes(document, 1), readFixture("red.png"));
    EXPECT_TRUE(attachmentBytes(document, 2).empty());
}

TEST(AssAttachments, AnAddedAttachmentFollowsTheLastOfItsKindAndRemovingItRestoresTheFile)
{
    const auto bytes = readFixture("attached.ass");
    const std::string original = text(bytes);
    Document document = loadAss(bytes).document;
    const auto font = pattern(130, 3), graphic = pattern(70, 9);
    ASSERT_TRUE(document.addAttachment(AttachmentKind::Font, u8"Added_0.ttf", font));
    ASSERT_TRUE(document.addAttachment(AttachmentKind::Graphic, u8"logo.png", graphic));

    const std::size_t fontsEnd = original.find("\r\n[Graphics]");
    const std::size_t graphicsEnd = original.find("\r\n[Events]");
    ASSERT_NE(fontsEnd, std::string::npos);
    ASSERT_NE(graphicsEnd, std::string::npos);
    const std::string expected = original.substr(0, fontsEnd) + block("fontname: Added_0.ttf", font, "\r\n") +
                                 original.substr(fontsEnd, graphicsEnd - fontsEnd) +
                                 block("filename: logo.png", graphic, "\r\n") + original.substr(graphicsEnd);
    const auto saved = encodeAss(document);
    EXPECT_EQ(text(saved), expected);

    const Document reloaded = loadAss(saved).document;
    const auto list = attachments(reloaded);
    ASSERT_EQ(list.size(), 4u);
    EXPECT_EQ(list[1].name, "Added_0.ttf");
    EXPECT_EQ(attachmentBytes(reloaded, 1), font);
    EXPECT_EQ(list[3].name, "logo.png");
    EXPECT_EQ(attachmentBytes(reloaded, 3), graphic);
    ASSERT_EQ(attachedFonts(document).size(), 2u);
    EXPECT_EQ(attachedFonts(document)[1].bytes, font);
    EXPECT_NE(attachedFontsKey(document), attachedFontsKey(loadAss(bytes).document));

    ASSERT_TRUE(document.removeAttachment(3));
    ASSERT_TRUE(document.removeAttachment(1));
    EXPECT_FALSE(document.removeAttachment(2));
    EXPECT_EQ(text(encodeAss(document)), original);
}

TEST(AssAttachments, AScriptWithoutAttachmentsGetsSectionsOfItsOwnThatGoWithTheirLast)
{
    const std::string script = "[Script Info]\nScriptType: v4.00+\n\n[Events]\nFormat: Layer, Start, End, Style, Name, "
                               "MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,x";
    Document document = load(script);
    const auto font = pattern(5, 1), graphic = pattern(4, 2);
    ASSERT_TRUE(document.addAttachment(AttachmentKind::Font, u8"a_0.ttf", font));
    ASSERT_TRUE(document.addAttachment(AttachmentKind::Graphic, u8"b.png", graphic));
    EXPECT_EQ(text(encodeAss(document)), script + "\n\n" + block("[Fonts]\nfontname: a_0.ttf", font, "\n") + "\n" +
                                             block("[Graphics]\nfilename: b.png", graphic, "\n"));
    const std::string renderer = text(encodeAss(document, AssSaveOptions{.renderer = true}));
    EXPECT_EQ(renderer.find("[Fonts]"), std::string::npos);
    ASSERT_TRUE(document.removeAttachment(0));
    ASSERT_TRUE(document.removeAttachment(0));
    EXPECT_TRUE(attachments(document).empty());
    EXPECT_EQ(text(encodeAss(document)), script);
}

TEST(AssAttachments, AddingAfterALineThatBeganWithABracketKeepsItWithTheFont)
{
    std::vector<std::byte> big;
    for (int i = 0; i < 240; ++i)
        big.push_back(static_cast<std::byte>(i % 3 ? (0x5A + i) & 0xFF : 0xE8));
    const std::string encoded = encode(big);
    std::string bigLines;
    for (std::size_t i = 0; i < encoded.size(); i += 4)
        bigLines += encoded.substr(i, 4) + "\r\n";
    const std::string head = "[Script Info]\r\nScriptType: v4.00+\r\n\r\n[Fonts]\r\nfontname: big.ttf\r\n" + bigLines;
    const std::string events = "\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
                               "Effect, Text\r\n";
    Document document = load(head + events);
    const auto font = pattern(9, 4);
    ASSERT_TRUE(document.addAttachment(AttachmentKind::Font, u8"next_0.ttf", font));
    EXPECT_EQ(text(encodeAss(document)), head + block("fontname: next_0.ttf", font, "\r\n") + events);
    const auto fonts = attachedFonts(document);
    ASSERT_EQ(fonts.size(), 2u);
    EXPECT_EQ(fonts[0].bytes, big);
    EXPECT_EQ(fonts[1].bytes, font);
    ASSERT_TRUE(document.removeAttachment(0));
    ASSERT_EQ(attachments(document).size(), 1u);
    EXPECT_EQ(text(encodeAss(document)),
              "[Script Info]\r\nScriptType: v4.00+\r\n\r\n[Fonts]\r\n" + block("fontname: next_0.ttf", font, "\r\n") +
                  events);
}

TEST(AssAttachments, FontsNameWithAZeroBeforeTheExtension)
{
    EXPECT_EQ(attachmentName(AttachmentKind::Font, u8"Noto Sans.ttf"), u8"Noto Sans_0.ttf");
    EXPECT_EQ(attachmentName(AttachmentKind::Font, u8"a.b.otf"), u8"a.b_0.otf");
    EXPECT_EQ(attachmentName(AttachmentKind::Font, u8"plain"), u8"plain_0");
    EXPECT_EQ(attachmentName(AttachmentKind::Graphic, u8"logo.png"), u8"logo.png");
}

TEST(AssAttachments, ReadsWhetherTheLicenceRestrictsEmbedding)
{
    EXPECT_TRUE(embeddingRestricted(sfnt(0x0002, false)));
    EXPECT_TRUE(embeddingRestricted(sfnt(0x0002, true)));
    EXPECT_TRUE(embeddingRestricted(sfnt(0x0302, false)));
    EXPECT_FALSE(embeddingRestricted(sfnt(0x0000, false)));
    EXPECT_FALSE(embeddingRestricted(sfnt(0x0004, false)));
    EXPECT_FALSE(embeddingRestricted(sfnt(0x0006, false)));
    EXPECT_FALSE(embeddingRestricted(sfnt(0x0008, true)));
    EXPECT_FALSE(embeddingRestricted(readFixture("red.png")));
    EXPECT_FALSE(embeddingRestricted({}));
    auto cut = sfnt(0x0002, false);
    cut.resize(30);
    EXPECT_FALSE(embeddingRestricted(cut));
}

TEST(AssAttachments, OnlyAnAssScriptTakesAttachments)
{
    const std::string srt = "1\n00:00:00,000 --> 00:00:01,000\nx\n";
    Document document = loadSrt(std::as_bytes(std::span(srt))).document;
    ASSERT_EQ(document.format(), SubtitleFormat::Srt);
    EXPECT_FALSE(document.addAttachment(AttachmentKind::Font, u8"a_0.ttf", pattern(3, 0)));
    EXPECT_TRUE(attachments(document).empty());
}
