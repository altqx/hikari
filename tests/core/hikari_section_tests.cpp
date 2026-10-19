// WS2 (#216): the [Hikari] section, where an ASS file keeps its Document
// tab's panel arrangement. Read as "key=value" lines; written in place, or as
// a new section at the end of the file (before embedded data that ends it);
// unknown keys and malformed lines round-trip byte for byte (ADR 0013).

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/subtitle_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace hikari::core;

namespace {

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

const std::string kHead = "[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                          "Format: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n[Events]\n"
                          "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n";

std::string setLayout(const std::string &input, std::u8string_view value = u8"QUJD")
{
    auto loaded = loadAss(bytesOf(input));
    EXPECT_TRUE(loaded.document.setHikariProperty(u8"Layout", std::u8string(value)));
    return text(encodeAss(loaded.document));
}

} // namespace

TEST(HikariSection, ReadsKeyValueLinesAndKeepsEveryByte)
{
    const std::string input = kHead + "\n[Hikari]\r\nLayout=QUJD\r\nFuture = later; value\r\n"
                                      "Spaced: colon line\r\n; a comment=not a key\r\n\r\nLayout=TEFTVA==\r\n";
    const auto loaded = loadAss(bytesOf(input));
    // The last value wins, as in Script Info; values and keys are trimmed.
    EXPECT_EQ(loaded.document.hikariProperty(u8"Layout"), std::optional<std::u8string>(u8"TEFTVA=="));
    EXPECT_EQ(loaded.document.hikariProperty(u8"Future"), std::optional<std::u8string>(u8"later; value"));
    EXPECT_FALSE(loaded.document.hikariProperty(u8"Spaced")); // no '=': kept as it is
    EXPECT_FALSE(loaded.document.hikariProperty(u8"; a comment"));
    EXPECT_FALSE(loaded.document.scriptInfo(u8"Layout")); // never Script Info
    EXPECT_EQ(loaded.document.sections().back().kind, SectionKind::Hikari);
    for (const auto &d : loaded.diagnostics)
        EXPECT_NE(d.kind, Diagnostic::Kind::UnknownSection);
    EXPECT_EQ(text(encodeAss(loaded.document)), input);
}

TEST(HikariSection, AnUnchangedValueChangesNoByte)
{
    const std::string input = kHead + "\n[Hikari]\nLayout=QUJD\n";
    EXPECT_EQ(setLayout(input, u8"QUJD"), input);
}

TEST(HikariSection, ReplacesTheValueInPlaceKeepingUnknownKeysAndLineEnds)
{
    const std::string input = kHead + "\n[Hikari]\r\nBefore=1\r\nLayout=old\r\n;note\r\nAfter=2";
    EXPECT_EQ(setLayout(input), kHead + "\n[Hikari]\r\nBefore=1\r\nLayout=QUJD\r\n;note\r\nAfter=2");
}

TEST(HikariSection, RepeatedSectionsReplaceTheLastValue)
{
    const std::string input = kHead + "\n[Hikari]\nLayout=first\n\n[Hikari]\nLayout=second\nOther=x\n";
    EXPECT_EQ(setLayout(input), kHead + "\n[Hikari]\nLayout=first\n\n[Hikari]\nLayout=QUJD\nOther=x\n");
}

TEST(HikariSection, AddsTheKeyAfterTheLastPropertyOfTheSection)
{
    const std::string input = kHead + "\n[Hikari]\nOther=x\n\n";
    EXPECT_EQ(setLayout(input), kHead + "\n[Hikari]\nOther=x\nLayout=QUJD\n\n");
}

TEST(HikariSection, ANewSectionGoesAtTheEndAfterABlankLine)
{
    // LF, CRLF, a file without a final line end and one ending in a blank line.
    EXPECT_EQ(setLayout(kHead), kHead + "\n[Hikari]\nLayout=QUJD\n");
    std::string crlf = kHead;
    for (std::size_t at = 0; (at = crlf.find('\n', at)) != std::string::npos; at += 2)
        crlf.insert(at, "\r");
    EXPECT_EQ(setLayout(crlf), crlf + "\r\n[Hikari]\r\nLayout=QUJD\r\n");
    const std::string open = kHead.substr(0, kHead.size() - 1);
    EXPECT_EQ(setLayout(open), open + "\n\n[Hikari]\nLayout=QUJD\n");
    EXPECT_EQ(setLayout(kHead + "\n"), kHead + "\n[Hikari]\nLayout=QUJD\n");
    // Another tool's section after the Events stays before it.
    const std::string garbage = kHead + "\n[Aegisub Extradata]\nData: 1,x,y\n";
    EXPECT_EQ(setLayout(garbage), garbage + "\n[Hikari]\nLayout=QUJD\n");
}

TEST(HikariSection, ANewSectionGoesBeforeEmbeddedDataEndingTheFile)
{
    // libass reads every line after [Fonts] as font data until a header it
    // knows; VSFilter until a blank line. The section goes before them.
    const std::string fonts = "[Fonts]\nfontname: a_0.ttf\n!!!!\n[!!!\n\n[Graphics]\nfilename: b.png\n!!!!\n";
    const std::string input = kHead + "\n" + fonts;
    EXPECT_EQ(setLayout(input), kHead + "\n[Hikari]\nLayout=QUJD\n\n" + fonts);
    // The loader reads the continuation as an attachment's ('[' plus encoding characters).
    const auto saved = loadAss(bytesOf(setLayout(input)));
    EXPECT_EQ(saved.document.hikariProperty(u8"Layout"), std::optional<std::u8string>(u8"QUJD"));
}

TEST(HikariSection, MalformedContentRoundTrips)
{
    // A header with text after it, a line without '=', an empty key, invalid
    // UTF-8 and a Dialogue line (a Line, as legacy LoadASS reads it anywhere).
    std::string input = kHead + "\n[Hikari] extra\nLayout\n=nothing\nBad=\xff\xfe\n"
                                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\nLayout=%%%\n";
    const auto loaded = loadAss(bytesOf(input));
    EXPECT_EQ(loaded.document.sections().back().kind, SectionKind::Hikari);
    EXPECT_EQ(loaded.document.hikariProperty(u8"Layout"), std::optional<std::u8string>(u8"%%%"));
    EXPECT_EQ(loaded.document.hikariProperty(u8""), std::optional<std::u8string>(u8"nothing"));
    EXPECT_FALSE(loaded.document.hikariProperty(u8"Bad")); // invalid UTF-8 stays bytes
    EXPECT_EQ(loaded.document.lines().size(), 2u);
    EXPECT_EQ(text(encodeAss(loaded.document)), input);
    // Replacing the value touches only its line.
    const std::string out = setLayout(input);
    input.replace(input.find("Layout=%%%"), 10, "Layout=QUJD");
    EXPECT_EQ(out, input);
}

TEST(HikariSection, WhatItWritesLegacyReadersPassOver)
{
    // Legacy LoadASS takes a line with ':' into Script Info and any line
    // starting "Dial", "Comm" or "Style:" as a record; VSFilter reads
    // "key: value" lines in any section. Base64 has no ':'.
    const std::string out = setLayout(kHead, u8"eyJzY2hlbWEiOjF9+/=");
    const std::string section = out.substr(out.find("[Hikari]"));
    for (std::size_t at = 0, next; at < section.size(); at = next + 1) {
        next = section.find('\n', at);
        const std::string line = section.substr(at, next - at);
        EXPECT_EQ(line.find(':'), std::string::npos) << line;
        for (const char *prefix : {"Dial", "Comm", "Style", "Format", ";"})
            EXPECT_FALSE(line.starts_with(prefix)) << line;
    }
}

TEST(HikariSection, SsaFilesKeepItToo)
{
    const std::string ssa = "[Script Info]\nScriptType: v4.00\n\n[V4 Styles]\nFormat: Name, Fontname\nStyle: Default,Arial\n\n"
                            "[Events]\nFormat: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                            "Dialogue: Marked=0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n";
    EXPECT_EQ(setLayout(ssa), ssa + "\n[Hikari]\nLayout=QUJD\n");
}

TEST(HikariSection, OtherFormatsHaveNone)
{
    const std::string srt = "1\n00:00:01,000 --> 00:00:02,000\none\n";
    auto loaded = loadSubtitle(bytesOf(srt), u8"srt");
    ASSERT_TRUE(loaded);
    EXPECT_FALSE(loaded->document.setHikariProperty(u8"Layout", u8"QUJD"));
    EXPECT_EQ(text(encodeSubtitle(loaded->document)), srt);
}

TEST(HikariSection, TheRendererScriptLeavesItOut)
{
    const std::string input = kHead + "\n[Hikari]\nLayout=QUJD\n";
    const std::string script = text(encodeAss(loadAss(bytesOf(input)).document, AssSaveOptions{.renderer = true}));
    EXPECT_EQ(script.find("[Hikari]"), std::string::npos);
    EXPECT_EQ(script, kHead + "\n");
}

namespace {

std::string fixture(const char *name)
{
    std::FILE *f = std::fopen((std::string(HIKARI_SECTION_FIXTURES "/") + name).c_str(), "rb");
    EXPECT_NE(f, nullptr) << name;
    std::string out;
    if (!f)
        return out;
    char buffer[4096];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
        out.append(buffer, n);
    std::fclose(f);
    return out;
}

} // namespace

// The compatibility fixtures' -hikari files are what the rewrite writes
// from the others (tests/fixtures/hikari-section/make-fixtures.py).
TEST(HikariSection, TheCompatibilityFixturesAreWhatItWrites)
{
    std::string layout = fixture("layout.txt");
    while (!layout.empty() && (layout.back() == '\n' || layout.back() == '\r'))
        layout.pop_back();
    ASSERT_FALSE(layout.empty());
    const std::u8string value(layout.begin(), layout.end());
    EXPECT_EQ(setLayout(fixture("plain.ass"), value), fixture("plain-hikari.ass"));
    EXPECT_EQ(setLayout(fixture("fonts.ass"), value), fixture("fonts-hikari.ass"));
    // Read back, the value is the one written; nothing else changed.
    const auto loaded = loadAss(bytesOf(fixture("fonts-hikari.ass")));
    EXPECT_EQ(loaded.document.hikariProperty(u8"Layout"), std::optional<std::u8string>(value));
    EXPECT_EQ(text(encodeAss(loaded.document)), fixture("fonts-hikari.ass"));
}
