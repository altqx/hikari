#include "hikari/regex/regex.h"

#include <gtest/gtest.h>

#include <string>

using hikari::regex::Pattern;
using hikari::regex::Syntax;

namespace {

Pattern compiled(std::string_view pattern, Syntax syntax = Syntax::Pcre)
{
    auto p = Pattern::compile(pattern, syntax);
    EXPECT_TRUE(p) << pattern << ": " << (p ? std::string() : p.error().message);
    return *p;
}

TEST(Regex, GroupsAreViewsIntoTheSubject)
{
    const std::string subject = "at 12:34 and 56:07";
    const auto m = compiled("([0-9]+):([0-9]+)(x)?").search(subject);
    ASSERT_TRUE(m);
    ASSERT_EQ(m->size(), 4u);
    EXPECT_EQ((*m)[0], "12:34");
    EXPECT_EQ((*m)[1], "12");
    EXPECT_EQ((*m)[2], "34");
    EXPECT_EQ((*m)[1].data(), subject.data() + 3);
    EXPECT_EQ(m->position(), 3u);
    EXPECT_EQ(m->end(), 8u);
    EXPECT_FALSE(m->matched(3));
    EXPECT_EQ((*m)[3], "");
}

TEST(Regex, SearchResumesFromAnOffset)
{
    const auto p = compiled("[0-9]+");
    const std::string subject = "a1b22c333";
    std::string seen;
    for (auto m = p.search(subject); m; m = p.search(subject, m->end()))
        seen += std::string((*m)[0]) + ",";
    EXPECT_EQ(seen, "1,22,333,");
    EXPECT_FALSE(p.search(subject, subject.size() + 1));
    EXPECT_FALSE(compiled("^b").search("ab", 1)) << "^ is the start of the subject, not of the search";
}

TEST(Regex, NoMatch)
{
    EXPECT_FALSE(compiled("z").search("abc"));
    EXPECT_FALSE(compiled("b").match("abc"));
    EXPECT_FALSE(compiled("").search("abc", 4));
}

TEST(Regex, MatchCoversTheWholeSubject)
{
    const auto p = compiled("[a-z]+(\\.[a-z]+)+");
    EXPECT_TRUE(p.match("file.open"));
    EXPECT_FALSE(p.match("file.open!"));
    EXPECT_FALSE(p.match(" file.open"));
    EXPECT_TRUE(p.search(" file.open!"));
    EXPECT_TRUE(compiled("a|ab").match("ab")) << "the whole subject, not the first alternative";
}

TEST(Regex, Utf8IsCodePoints)
{
    const std::string subject = "x\xE3\x81\x82\xE3\x81\x84y";
    const auto m = compiled("^x(.)(.)y$").match(subject);
    ASSERT_TRUE(m);
    EXPECT_EQ((*m)[1], "\xE3\x81\x82");
    EXPECT_EQ((*m)[2], "\xE3\x81\x84");
    EXPECT_TRUE(compiled("\xE3\x81\x84").search(subject));
    EXPECT_FALSE(compiled("^x.y$").match("x\xE3\x81y")) << "a truncated sequence is no character";
    EXPECT_TRUE(compiled("y").search("\xFFy")) << "invalid bytes elsewhere do not stop a match";
}

TEST(Regex, CompileErrorsReportTheirOffset)
{
    const auto unclosed = Pattern::compile("ab(cd");
    ASSERT_FALSE(unclosed);
    EXPECT_EQ(unclosed.error().offset, 5u);
    EXPECT_FALSE(unclosed.error().message.empty());
    const auto badClass = Pattern::compile("abc[z-a]");
    ASSERT_FALSE(badClass);
    EXPECT_EQ(badClass.error().offset, 7u);
    EXPECT_FALSE(Pattern::compile("\\u0041"));
}

TEST(Regex, ReplaceFirstReplacesOnlyTheFirstMatchWithLiteralText)
{
    const auto p = compiled("[0-9]+");
    EXPECT_EQ(p.replaceFirst("a1b22c", "<$0>"), "a<$0>b22c");
    EXPECT_EQ(p.replaceFirst("abc", "#"), "abc");
    EXPECT_EQ(compiled("^\\[string (.*)\\]:[0-9]+: ").replaceFirst("[string \"x\"]:3: [string \"y\"]:4: boom", ""),
              "boom");
}

TEST(Regex, EcmaScriptSyntax)
{
    EXPECT_TRUE(compiled("^\\u0041$", Syntax::EcmaScript).match("A"));
    EXPECT_TRUE(compiled("^\\u{3042}$", Syntax::EcmaScript).match("\xE3\x81\x82"));
    EXPECT_TRUE(compiled("^\\x{2}$", Syntax::EcmaScript).match("xx"));
    EXPECT_TRUE(compiled("^\\x{2}$").match("\x02")) << "PCRE reads \\x{…} as a code point";
    EXPECT_FALSE(compiled("a$", Syntax::EcmaScript).search("a\n"));
    EXPECT_TRUE(compiled("a$").search("a\n")) << "PCRE's $ also matches before a final newline";
    EXPECT_TRUE(compiled("^[^]$", Syntax::EcmaScript).match("\n"));
    EXPECT_FALSE(compiled("^.$", Syntax::EcmaScript).match("\r"));
    EXPECT_FALSE(compiled("^\\w$", Syntax::EcmaScript).match("\xC3\xA9"));
}

}
