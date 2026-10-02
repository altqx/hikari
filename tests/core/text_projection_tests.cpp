// Hidden-tag projection (V2; docs/qt/ux/ass-editor.md). Ports the accepted
// prototype's mapping fixtures for the accepted defaults: insertion after
// boundary tags, and replacements that keep intervening tags.

#include "hikari/core/text_projection.h"

#include <QString>
#include <QTextBoundaryFinder>
#include <gtest/gtest.h>

using namespace hikari::core;

namespace {

// Grapheme boundaries of the projection, as the UI computes them.
std::vector<std::size_t> graphemes(const std::u16string &text)
{
    const QString s = QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, s);
    std::vector<std::size_t> out{0};
    for (qsizetype b; (b = finder.toNextBoundary()) >= 0;)
        out.push_back(static_cast<std::size_t>(b));
    return out;
}

std::expected<std::u16string, MapRefusal> edit(std::u16string_view raw, std::size_t start, std::size_t end,
                                               std::u16string_view inserted)
{
    const auto bounds = graphemes(project(raw).text);
    return mappedReplace(raw, start, end, inserted, bounds);
}

void expectEdit(std::u16string_view raw, std::size_t start, std::size_t end, std::u16string_view inserted,
                std::u16string_view expected)
{
    const auto r = edit(raw, start, end, inserted);
    ASSERT_TRUE(r) << "refused: " << static_cast<int>(r.error());
    EXPECT_EQ(toUtf8(*r), toUtf8(expected));
}

void expectRefused(std::u16string_view raw, std::size_t start, std::size_t end, std::u16string_view inserted,
                   MapRefusal why)
{
    const auto r = edit(raw, start, end, inserted);
    ASSERT_FALSE(r) << toUtf8(*r).size();
    EXPECT_EQ(r.error(), why);
}

} // namespace

TEST(Projection, ShowsTextAndEscapesAndHidesOverrides)
{
    const auto p = project(u"{\\i1}A\\NB\\nC\\hD{\\i0}");
    EXPECT_EQ(p.text, u"A\nB C D");
    EXPECT_EQ(p.spans.front().kind, SpanKind::HiddenOverride);
    EXPECT_EQ(project(u"{\\p1}m 0 0 l 1 1{\\p0}x").text, std::u16string(1, kProtectedMarker) + u"x");
    EXPECT_EQ(project(u"{\\pos(1,2)}x").text, u"x"); // \pos is not drawing mode
    EXPECT_EQ(project(u"{\\t(\\p1)}x").text, u"x");  // not interpreted inside \t
}

TEST(Projection, BoundaryAndCrossingDefaults)
{
    expectEdit(u"A{\\i1}B{\\i0}C", 1, 1, u"X", u"A{\\i1}XB{\\i0}C");     // after the boundary tags
    expectEdit(u"A{\\i1}B{\\i0}C", 0, 3, u"X", u"X{\\i1}{\\i0}");         // intervening tags kept
    expectEdit(u"A{\\b1}B{\\b0}C", 0, 2, u"", u"{\\b1}{\\b0}C");          // deletion keeps tags
    expectEdit(u"{\\an8\\bord2}AB", 0, 0, u"X", u"{\\an8\\bord2}XAB");    // leading tags
    expectEdit(u"{\\i1}{\\i0}", 0, 0, u"X", u"{\\i1}{\\i0}X");            // all hidden
}

TEST(Projection, EscapesAndPastedText)
{
    expectEdit(u"A\\NB\\nC\\hD", 1, 2, u"-", u"A-B\\nC\\hD");     // a hard break is one token
    expectEdit(u"A\\NB\\nC\\hD", 0, 1, u"X", u"X\\NB\\nC\\hD");   // untouched escapes stay
    expectEdit(u"{\\i1}AB{\\i0}", 1, 1, u"X\nY", u"{\\i1}AX\\NYB{\\i0}");
    expectEdit(u"{\\i1}AB{\\i0}", 1, 1, u"X\r\nY", u"{\\i1}AX\\NYB{\\i0}");
    expectEdit(u"AB", 1, 1, u" ", u"A\\hB");
    expectRefused(u"{\\i1}AB{\\i0}", 0, 2, u"{\\b1}X", MapRefusal::AssSyntax);
    expectRefused(u"AB", 0, 1, u"a\\b", MapRefusal::AssSyntax);
}

TEST(Projection, CharactersAreNeverSplit)
{
    expectEdit(u"{\\b1}A\U0001F600B{\\b0}", 1, 3, u"\U0001F3AC", u"{\\b1}A\U0001F3ACB{\\b0}");
    expectRefused(u"A\U0001F600B", 2, 3, u"X", MapRefusal::SplitsCharacter);
    expectRefused(u"AéB", 1, 2, u"X", MapRefusal::SplitsCharacter);
    expectEdit(u"AéB", 1, 3, u"é", u"AéB");
    const std::u16string zwj = u"A\U0001F469\U0001F3FD‍\U0001F4BBB";
    expectEdit(zwj, 1, 8, u"X", u"AXB");
    expectRefused(zwj, 3, 3, u"X", MapRefusal::SplitsCharacter);
}

TEST(Projection, KaraokeAndRtlSourceStays)
{
    expectEdit(u"{\\k20}Ka{\\kf30}ra{\\ko15}oke", 0, 4, u"New", u"{\\k20}New{\\kf30}{\\ko15}oke");
    expectEdit(u"{\\an7}مرحبا {\\i1}שלום{\\i0}", 0, 5,
               u"أهلا",
               u"{\\an7}أهلا {\\i1}שלום{\\i0}");
}

TEST(Projection, DrawingAndMalformedSpansAreProtected)
{
    const std::u16string drawing = u"{\\p1}m 0 0 l 20 30{\\p0}Text";
    expectEdit(drawing, 1, 5, u"字幕", u"{\\p1}m 0 0 l 20 30{\\p0}字幕");
    expectRefused(drawing, 0, 1, u"X", MapRefusal::Protected);
    expectRefused(drawing, 0, 0, u"X", MapRefusal::Reinterpreted); // would become drawing payload
    expectEdit(u"Text {\\i1", 0, 4, u"Word", u"Word {\\i1");
    expectRefused(u"Text {\\i1", 5, 6, u"X", MapRefusal::Protected);
    expectEdit(u"A{\\i1{bad}B", 0, 1, u"X", u"X{\\i1{bad}B");
    expectRefused(u"A}B", 1, 2, u"X", MapRefusal::Protected);
}

TEST(Projection, Utf8RoundTrip)
{
    const std::u8string s = u8"Aé字\U0001F600";
    EXPECT_EQ(toUtf8(toUtf16(s)), s);
    EXPECT_EQ(toUtf16(s).size(), 5u);
}
