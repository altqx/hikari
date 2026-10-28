// R1-pcre2: wxRegEx as wxWidgets 3.3.3 runs it over PCRE2 10.47.

#include "hikari/core/legacy_regex.h"

#include <gtest/gtest.h>

using namespace hikari::core;
using R = LegacyRegex;

namespace {

std::u16string replaced(std::u16string_view pattern, int flags, std::u16string text, std::u16string_view with,
                        std::size_t max = 0, int *count = nullptr)
{
    const LegacyRegex re(pattern, flags);
    const int n = re.replace(text, with, max);
    if (count)
        *count = n;
    return text;
}

} // namespace

TEST(LegacyRegex, PcreSyntaxWithWxOptions)
{
    // PCRE syntax std::regex ECMAScript lacks: lookbehind, possessive, \h.
    EXPECT_TRUE(R(u"(?<=a)b", R::Advanced).matches(u"ab"));
    EXPECT_FALSE(R(u"a++a", R::Advanced).matches(u"aaa"));
    EXPECT_TRUE(R(u"\\h", R::Advanced).matches(u"a b"));
    // DOTALL by default: '.' matches a newline; Newline mode makes ^/$ per line instead.
    EXPECT_TRUE(R(u"a.b", R::Advanced).matches(u"a\nb"));
    EXPECT_FALSE(R(u"a.b", R::Advanced | R::Newline).matches(u"a\nb"));
    EXPECT_TRUE(R(u"^b", R::Advanced | R::Newline).matches(u"a\nb"));
    // $ also matches before a final newline (PCRE default).
    EXPECT_TRUE(R(u"a$", R::Advanced).matches(u"a\n"));
    // Case folding is Unicode-aware with UTF.
    EXPECT_TRUE(R(u"ż", R::Advanced | R::IgnoreCase).matches(u"Ż"));
    // ALT_BSUX: A is "A".
    EXPECT_TRUE(R(u"\\u0041", R::Advanced).matches(u"A"));
    // Invalid patterns report PCRE2's message.
    const R bad(u"(", R::Advanced);
    EXPECT_FALSE(bad.isValid());
    EXPECT_FALSE(bad.errorMessage().empty());
}

TEST(LegacyRegex, TclMetasyntaxConversions)
{
    // \m \M \y \Y word boundaries in advanced syntax.
    EXPECT_EQ(R(u"\\mab\\M", R::Advanced).converted(), u"[[:<:]]ab[[:>:]]");
    EXPECT_TRUE(R(u"\\yab\\y", R::Advanced).matches(u"x ab y"));
    EXPECT_FALSE(R(u"\\yab\\y", R::Advanced).matches(u"xaby"));
    // Directors: ***= literal, ***: advanced.
    EXPECT_TRUE(R(u"***=a.b", R::Extended).matches(u"xa.by"));
    EXPECT_FALSE(R(u"***=a.b", R::Extended).matches(u"axb"));
    // Embedded options: (?i) as PCRE, (?q) literal, (?c) case-sensitive.
    EXPECT_TRUE(R(u"(?i)AB", R::Advanced).matches(u"ab"));
    EXPECT_EQ(R(u"(?q)a.b", R::Advanced).converted(), u"\\Qa.b");
    EXPECT_FALSE(R(u"(?c)ab", R::Advanced | R::IgnoreCase).matches(u"AB"));
    // A non-option group is left alone.
    EXPECT_EQ(R(u"(?:ab)", R::Advanced).converted(), u"(?:ab)");
    // Basic syntax becomes extended.
    EXPECT_EQ(LegacyRegex::convertFromBasic(u"\\(a\\)*+?|{}"), u"(a)*\\+\\?\\|\\{\\}");
    EXPECT_EQ(LegacyRegex::convertFromBasic(u"*a^$"), u"\\*a\\^$");
    EXPECT_EQ(LegacyRegex::convertFromBasic(u"\\<a\\>"), u"[[:<:]]a[[:>:]]");
}

TEST(LegacyRegex, MatchOffsetsAreUtf16)
{
    const R re(u"(b)(x)?", R::Advanced);
    ASSERT_TRUE(re.matches(u"\U0001F600ab"));
    EXPECT_EQ(re.match(0), (std::pair<std::size_t, std::size_t>{3, 1})); // the emoji is two units
    EXPECT_EQ(re.match(1), (std::pair<std::size_t, std::size_t>{3, 1}));
    EXPECT_EQ(re.match(2), (std::pair<std::size_t, std::size_t>{std::u16string::npos, 0})); // unset group
    EXPECT_EQ(re.matchCount(), 3u);
    EXPECT_FALSE(re.match(3));
}

TEST(LegacyRegex, ReplaceLikeWxRegEx)
{
    int n = 0;
    // \1 and & back-references; ^ only matches the first time (NOTBOL afterwards).
    EXPECT_EQ(replaced(u"(a)(b)", R::Advanced, u"abab", u"\\2\\1&", 0, &n), u"baabbaab");
    EXPECT_EQ(n, 2);
    EXPECT_EQ(replaced(u"^a", R::Advanced, u"aaa", u"x", 0, &n), u"xaa");
    EXPECT_EQ(n, 1);
    // maxMatches, an invalid back reference eaten, a trailing backslash kept, \c as c.
    EXPECT_EQ(replaced(u"a", R::Advanced, u"aaa", u"x", 2), u"xxa");
    EXPECT_EQ(replaced(u"a", R::Advanced, u"a", u"[\\5]"), u"[]");
    EXPECT_EQ(replaced(u"a", R::Advanced, u"a", u"x\\"), u"x\\");
    EXPECT_EQ(replaced(u"a", R::Advanced, u"a", u"\\n"), u"n");
    // Each match is searched in the rest of the text, so lookbehind cannot see before it.
    EXPECT_EQ(replaced(u"(?<=a)a", R::Advanced, u"aaa", u"x"), u"axa");
    // R3-hang-crash-loss: an empty match is replaced once and the search moves on
    // (legacy repeated it forever).
    EXPECT_EQ(replaced(u"x*", R::Advanced, u"ab", u"-", 0, &n), u"-a-b-");
    EXPECT_EQ(n, 3);
    EXPECT_EQ(replaced(u"x*", R::Advanced, u"\U0001F600", u"-"), u"-\U0001F600-");
    // An invalid expression replaces nothing.
    std::u16string text = u"abc";
    EXPECT_EQ(R(u"(", R::Advanced).replace(text, u"x"), -1);
    EXPECT_EQ(text, u"abc");
}

TEST(LegacyRegex, MatchErrorsAreReportedAndMatchNothing)
{
    // Catastrophic backtracking reaches PCRE2's match limit: wx_regexec logs
    // the error and Matches returns false; matchError() gives the text.
    const LegacyRegex re(u"(a+)+$", R::Advanced);
    ASSERT_TRUE(re.isValid());
    const std::u16string text = std::u16string(40, u'a') + u"b";
    EXPECT_FALSE(re.matches(text));
    EXPECT_FALSE(re.matchError().empty());
    std::u16string copy = text;
    EXPECT_EQ(re.replace(copy, u"x"), 0);
    EXPECT_EQ(copy, text);
    EXPECT_FALSE(re.matchError().empty());
    // A plain miss is no error.
    EXPECT_FALSE(re.matches(u"b"));
    EXPECT_TRUE(re.matchError().empty());
}
