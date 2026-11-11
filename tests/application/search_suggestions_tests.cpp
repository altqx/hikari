#include "hikari/application/search_suggestions.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace hikari::application::ai;

namespace {

std::string googleSample()
{
    std::ifstream in(HIKARI_SEARCH_SUGGESTIONS_FIXTURE, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string withChips(const std::string &chips)
{
    return "<style>.chip{}</style><div class=\"container\"><div class=\"carousel\">" + chips + "</div></div>";
}

}

TEST(SearchSuggestions, GooglesSnippetGivesItsChipsAndBothLogosAsSent)
{
    const auto parts = parseSearchSuggestions(googleSample());
    ASSERT_TRUE(parts);
    ASSERT_EQ(parts->chips.size(), 1u);
    EXPECT_EQ(parts->chips[0].text, "current Google stock price");
    EXPECT_EQ(parts->chips[0].url.rfind("https://vertexaisearch.cloud.google.com/grounding-api-redirect/AUBnsYugjN8g", 0),
              0u);
    EXPECT_TRUE(parts->lightLogo.starts_with("<svg class=\"logo-light\""));
    EXPECT_TRUE(parts->lightLogo.ends_with("</svg>"));
    EXPECT_NE(parts->lightLogo.find("#4285F4"), std::string::npos);
    EXPECT_TRUE(parts->darkLogo.starts_with("<svg class=\"logo-dark\""));
    EXPECT_NE(parts->darkLogo.find("<circle"), std::string::npos);
}

TEST(SearchSuggestions, EntitiesInTextAndLinksAreDecodedAndInnerTagsDropped)
{
    const auto parts = parseSearchSuggestions(withChips(
        "<a class=\"chip\" href=\"https://www.google.com/search?q=a&amp;b\">Tom &amp; Jerry <b>&#39;s</b>&#x21;</a>"));
    ASSERT_TRUE(parts);
    ASSERT_EQ(parts->chips.size(), 1u);
    EXPECT_EQ(parts->chips[0].text, "Tom & Jerry 's!");
    EXPECT_EQ(parts->chips[0].url, "https://www.google.com/search?q=a&b");
}

TEST(SearchSuggestions, OnlyHttpsChipsCountAndNoMoreThanFiveAreKept)
{
    std::string chips = "<a class=\"chip\" href=\"javascript:alert(1)\">bad</a><a href=\"https://x.test\">not a chip</a>";
    for (int i = 1; i <= 7; ++i)
        chips += "<a class='chip' href='https://www.google.com/search?q=" + std::to_string(i) + "'>q" +
                 std::to_string(i) + "</a>";
    const auto parts = parseSearchSuggestions(withChips(chips));
    ASSERT_TRUE(parts);
    ASSERT_EQ(parts->chips.size(), 5u);
    EXPECT_EQ(parts->chips.front().text, "q1");
    EXPECT_EQ(parts->chips.back().text, "q5");
    EXPECT_EQ(parts->lightLogo, "");
}

TEST(SearchSuggestions, ASnippetWithoutChipsGivesNothing)
{
    EXPECT_FALSE(parseSearchSuggestions(""));
    EXPECT_FALSE(parseSearchSuggestions("<div>chips</div>"));
    EXPECT_FALSE(parseSearchSuggestions(withChips("<a class=\"chip\" href=\"https://www.google.com/search?q=x\"> </a>")));
}
