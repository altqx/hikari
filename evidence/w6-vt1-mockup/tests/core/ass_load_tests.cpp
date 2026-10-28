// C2: structural ASS load (docs/qt/document-model.md).

#include "hikari/core/ass_load.h"

#include <QCryptographicHash>
#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace hikari::core;

namespace {

std::vector<std::byte> bytesOf(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::vector<std::byte> fixtureBytes(const char *name)
{
    QFile f(QStringLiteral(HIKARI_FIXTURE_DIR "/inputs/") + QLatin1String(name));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << name;
    const QByteArray data = f.readAll();
    std::vector<std::byte> out(static_cast<std::size_t>(data.size()));
    std::memcpy(out.data(), data.constData(), out.size());
    return out;
}

const SourceSpan &spanOf(const Record &r)
{
    return std::visit([](const auto &rec) -> const SourceSpan & { return rec.span; }, r);
}

// Header and record spans plus terminators, in order, must tile the input.
std::vector<std::byte> reassemble(const Document &doc)
{
    const auto &b = doc.source().bytes;
    std::vector<std::byte> out;
    if (doc.source().encoding == TextEncoding::Utf8WithBom)
        out.insert(out.end(), b.begin(), b.begin() + 3);
    auto take = [&](const SourceSpan &s) {
        out.insert(out.end(), b.begin() + static_cast<std::ptrdiff_t>(s.offset),
                   b.begin() + static_cast<std::ptrdiff_t>(s.offset + s.length + s.terminatorLength));
    };
    for (const auto &section : doc.sections()) {
        if (section.headerSpan)
            take(*section.headerSpan);
        for (const auto &r : section.records)
            take(spanOf(r));
    }
    return out;
}

std::u8string u8(std::string_view s)
{
    return std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size());
}

bool hasDiagnostic(const LoadResult &r, Diagnostic::Kind kind)
{
    return std::ranges::any_of(r.diagnostics, [&](const auto &d) { return d.kind == kind; });
}

} // namespace

TEST(AssLoad, EveryFixtureByteIsCoveredExactlyOnce)
{
    for (const char *name : {"unknown-sections.ass", "conversion-source.ass", "PROTOTYPE_A.ass", "PROTOTYPE_B.ass",
                             "tlmode-pairs.ass"}) {
        const auto input = fixtureBytes(name);
        const auto result = loadAss(input);
        EXPECT_EQ(result.document.source().bytes, input) << name;   // untouched original
        EXPECT_EQ(reassemble(result.document), input) << name;     // spans tile the source
    }
}

TEST(AssLoad, UnknownAndRepeatedSectionsStayInPlace)
{
    // C03-preservation: the legacy loader moved "Token: repeated-section" into
    // Script Info and dropped both unknown sections (legacy observation run
    // 36591631319). Here every section stays, in authored order.
    const auto result = loadAss(fixtureBytes("unknown-sections.ass"));
    const auto &sections = result.document.sections();
    std::vector<SectionKind> kinds;
    for (const auto &s : sections)
        kinds.push_back(s.kind);
    EXPECT_EQ(kinds, (std::vector<SectionKind>{SectionKind::ScriptInfo, SectionKind::Styles, SectionKind::Events,
                                               SectionKind::Unknown, SectionKind::Fonts, SectionKind::Unknown,
                                               SectionKind::Events}));
    EXPECT_EQ(sections[3].header, u8"[Hikari Synthetic Opaque]");
    // Unknown-section records are opaque: not script properties, not Lines.
    for (const auto &r : sections[3].records)
        EXPECT_TRUE(std::holds_alternative<OpaqueRecord>(r));
    for (const auto &r : sections[5].records)
        EXPECT_TRUE(std::holds_alternative<OpaqueRecord>(r));
    // Script Info holds only its own five properties.
    std::size_t properties = 0;
    for (const auto &r : sections[0].records)
        properties += std::holds_alternative<PropertyRecord>(r);
    EXPECT_EQ(properties, 5u);
    EXPECT_TRUE(hasDiagnostic(result, Diagnostic::Kind::UnknownSection));
    EXPECT_TRUE(hasDiagnostic(result, Diagnostic::Kind::RepeatedSection));
    EXPECT_EQ(result.document.source().encoding, TextEncoding::Utf8WithBom);
}

TEST(AssLoad, LinesFromRepeatedEventsSectionsKeepOrderAndIds)
{
    const auto result = loadAss(fixtureBytes("unknown-sections.ass"));
    const auto lines = result.document.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0]->text, u8"Gate");
    EXPECT_EQ(lines[1]->text, u8("港 — ميناء"));
    EXPECT_LT(lines[0]->id, lines[1]->id);
    EXPECT_EQ(lines[0]->start.value, DocumentTime(1'000'000));
    EXPECT_EQ(lines[1]->end.value, DocumentTime(4'000'000));
    EXPECT_EQ(lines[0]->style, u8"Default");
    EXPECT_FALSE(lines[0]->comment);
}

TEST(AssLoad, EventFieldsArePositionalLikeTheLegacyLoader)
{
    const auto result = loadAss(bytesOf("[Events]\n"
                                        "Comment: 3,0:00:05.50,0:00:06.00,Sign, Narrator ,10,20,30, fx ,  a, b,c  \n"
                                        "Dialogue: Marked=0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,\n"));
    const auto lines = result.document.lines();
    ASSERT_EQ(lines.size(), 3u);
    const LineRecord &c = *lines[0];
    EXPECT_TRUE(c.comment);
    EXPECT_EQ(c.layer.value, 3);
    EXPECT_EQ(c.start.value, DocumentTime(5'500'000));
    EXPECT_EQ(c.style, u8"Sign");           // not trimmed (legacy)
    EXPECT_EQ(c.actor, u8"Narrator");       // trimmed (legacy)
    EXPECT_EQ(c.marginLeft.value, 10);
    EXPECT_EQ(c.marginVertical.value, 30);
    EXPECT_EQ(c.effect, u8"fx");
    EXPECT_EQ(c.text, u8"a, b,c");          // commas kept; surrounding whitespace trimmed
    EXPECT_EQ(lines[1]->layer.lexeme, u8"0"); // "Marked=0" form
    EXPECT_EQ(lines[2]->text, u8"");          // exactly 9 fields: empty text
    EXPECT_TRUE(result.diagnostics.empty());
}

TEST(AssLoad, UnreadableEventsBecomePlainTextLines)
{
    // LoadASS takes every "Dial"/"Comm" line, but SetRaw reads it as an event
    // only with a full "Dialogue"/"Comment" prefix and 9 fields. Anything else
    // is a visible Line whose text is the whole line, timed 0, Default style.
    const auto result = loadAss(bytesOf("[Events]\n"
                                        "Dialog: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,t\n"
                                        "Commentary: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,c\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0  \r\n"));
    const auto lines = result.document.lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_TRUE(lines[0]->unparsed);
    EXPECT_FALSE(lines[0]->comment);
    EXPECT_EQ(lines[0]->text, u8"Dialog: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,t");
    EXPECT_EQ(lines[0]->style, u8"Default");
    EXPECT_EQ(lines[0]->start.value, DocumentTime(0));
    // "Commentary" starts with "Comment": a parsed comment event.
    EXPECT_FALSE(lines[1]->unparsed);
    EXPECT_TRUE(lines[1]->comment);
    EXPECT_TRUE(lines[2]->unparsed);
    EXPECT_EQ(lines[2]->text, u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0"); // right-trimmed
    EXPECT_TRUE(hasDiagnostic(result, Diagnostic::Kind::MalformedEvent));
}

TEST(AssLoad, ActorMarkersFollowLegacyPrecedence)
{
    const auto result = loadAss(bytesOf("[Events]\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D,[bookmark]Ann,0,0,0,,a\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D,[hidden][bookmark]Bob,0,0,0,,b\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D,[bookmark]A[bookmark]B,0,0,0,,c\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D,[tree_closed]G,0,0,0,,d\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D,[visible] Cat ,0,0,0,,e\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D,X[hidden],0,0,0,,f\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D, [hidden],0,0,0,,g\n"
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,D,[tree_description],0,0,0,,h\n"));
    const auto l = result.document.lines();
    ASSERT_EQ(l.size(), 8u);
    EXPECT_TRUE(l[0]->bookmark);
    EXPECT_EQ(l[0]->actor, u8"Ann");
    // Only the first matching kind is removed: [hidden] stays as actor text.
    EXPECT_TRUE(l[1]->bookmark);
    EXPECT_EQ(l[1]->visibility, LineVisibility::Visible);
    EXPECT_EQ(l[1]->actor, u8"[hidden]Bob");
    EXPECT_EQ(l[2]->actor, u8"AB"); // every occurrence of the kind
    EXPECT_EQ(l[3]->group, GroupMarker::Closed);
    EXPECT_EQ(l[3]->visibility, LineVisibility::Hidden);
    EXPECT_EQ(l[4]->visibility, LineVisibility::VisibleBlock);
    EXPECT_EQ(l[4]->actor, u8"Cat");
    EXPECT_EQ(l[5]->actor, u8"X[hidden]"); // the field must start with '['
    EXPECT_EQ(l[6]->actor, u8"[hidden]");  // leading space: not examined, then trimmed
    EXPECT_EQ(l[7]->group, GroupMarker::Description);
    EXPECT_EQ(l[7]->actor, u8"");
}

TEST(AssLoad, InvalidUtf8LinesStayAsBytes)
{
    const auto input = bytesOf("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,ok\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,bad \xC3\x28 byte\n");
    const auto result = loadAss(input);
    EXPECT_EQ(result.document.lines().size(), 1u);
    EXPECT_TRUE(hasDiagnostic(result, Diagnostic::Kind::InvalidUtf8));
    EXPECT_EQ(reassemble(result.document), input);
}

TEST(AssLoad, Utf16IsKeptWholeAsUnsupported)
{
    const auto input = bytesOf(std::string("\xFF\xFE[\0S\0", 6));
    const auto result = loadAss(input);
    EXPECT_TRUE(hasDiagnostic(result, Diagnostic::Kind::UnsupportedEncoding));
    EXPECT_EQ(reassemble(result.document), input);
}

TEST(AssLoad, NewlineStylesAndMissingFinalNewline)
{
    const auto input = bytesOf("[Script Info]\r\nTitle: a\nScriptType: v4.00+");
    const auto result = loadAss(input);
    const auto &records = result.document.sections()[0].records;
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(result.document.sections()[0].headerSpan->terminatorLength, 2u);
    EXPECT_EQ(spanOf(records[0]).terminatorLength, 1u);
    EXPECT_EQ(spanOf(records[1]).terminatorLength, 0u);
    EXPECT_EQ(reassemble(result.document), input);
}

TEST(AssLoad, StylesKeepPositionalFieldsAndDuplicates)
{
    const auto result = loadAss(bytesOf("[V4+ Styles]\nStyle: Default,Arial,24\nStyle: Default,Times,30\n"));
    const auto &records = result.document.sections()[0].records;
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(std::get<StyleRecord>(records[0]).name, u8"Default");
    EXPECT_EQ(std::get<StyleRecord>(records[1]).fields[1], u8"Times"); // duplicates both kept
}

TEST(LegacyTime, PositionalParseMatchesSubsTime)
{
    using legacy::assTimeMilliseconds;
    EXPECT_EQ(assTimeMilliseconds(u8"0:00:01.00"), 1000);
    EXPECT_EQ(assTimeMilliseconds(u8"1:02:03.45"), 3'723'450);
    EXPECT_EQ(assTimeMilliseconds(u8"10:00:00.00"), 36'000'000);
    EXPECT_EQ(assTimeMilliseconds(u8" 0:00:02.50"), 2500);   // leading space: atoi skips it
    EXPECT_EQ(assTimeMilliseconds(u8"0:00:01.5"), 1050);     // one centisecond digit: "5" -> 5*10
    EXPECT_EQ(assTimeMilliseconds(u8""), 0);
    EXPECT_EQ(assTimeMilliseconds(u8"   "), 0);
    EXPECT_FALSE(legacy::isCanonicalAssTime(u8"0:00:01.5"));
    EXPECT_TRUE(legacy::isCanonicalAssTime(u8" 0:00:01.50 "));
}

TEST(LegacyAtoi, BehavesLikeWxAtoi)
{
    EXPECT_EQ(legacy::atoi(u8"  42abc"), 42);
    EXPECT_EQ(legacy::atoi(u8"-7"), -7);
    EXPECT_EQ(legacy::atoi(u8"abc"), 0);
    EXPECT_EQ(legacy::atoi(u8""), 0);
    EXPECT_EQ(legacy::atoi(u8"4294967297"), 1);      // long value truncated to int
    EXPECT_EQ(legacy::atoi(u8"99999999999999999999"), -1); // saturates at LONG_MAX, then truncates
}
