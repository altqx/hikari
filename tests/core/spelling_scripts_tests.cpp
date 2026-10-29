#include "hikari/core/spelling_scripts.h"

#include <gtest/gtest.h>

using namespace hikari::core::spelling;

TEST(SpellingScripts, WordScriptsComeFromTheLetters)
{
    EXPECT_EQ(wordScripts(u"Hello"), Scripts(Script::Latin));
    EXPECT_EQ(wordScripts(u"gęś"), Scripts(Script::Latin));
    EXPECT_EQ(wordScripts(u"Đặng"), Scripts(Script::Latin));
    EXPECT_EQ(wordScripts(u"don't"), Scripts(Script::Latin));
    EXPECT_EQ(wordScripts(u"안녕하세요"), Scripts(Script::Hangul));
    EXPECT_EQ(wordScripts(u"வணக்கம்"), Scripts(Script::Tamil));
    EXPECT_EQ(wordScripts(u"สวัสดี"), Scripts(Script::Thai));
    EXPECT_EQ(wordScripts(u"привет"), Scripts(Script::Cyrillic));
    EXPECT_EQ(wordScripts(u"γειά"), Scripts(Script::Greek));
    EXPECT_EQ(wordScripts(u"שלום"), Scripts(Script::Hebrew));
    EXPECT_EQ(wordScripts(u"नमस्ते"), Scripts(Script::Devanagari));
    EXPECT_EQ(wordScripts(u"こんにちは"), Scripts(Script::Kana));
    EXPECT_EQ(wordScripts(u"世界"), Scripts(Script::Han));
    EXPECT_EQ(wordScripts(u"\U00020000"), Scripts(Script::Han));
    EXPECT_EQ(wordScripts(u"Seoul서울"), Scripts(Script::Latin) | Script::Hangul);
    EXPECT_EQ(wordScripts(u"é"), Scripts(Script::Latin));
    EXPECT_TRUE(wordScripts(u"123").empty());
    EXPECT_EQ(wordScripts(u"١wrold"), Scripts(Script::Latin));
    EXPECT_EQ(wordScripts(u"१wrold"), Scripts(Script::Latin));
    EXPECT_TRUE(wordScripts(u"١٢٣۴").empty());
    EXPECT_TRUE(wordScripts(u"௧௨௰๕").empty());
    EXPECT_EQ(wordScripts(u"ك١"), Scripts(Script::Arabic));
    EXPECT_TRUE(wordScripts(u"́").empty());
    EXPECT_TRUE(wordScripts(u"Ꭰ").empty());
    EXPECT_TRUE(wordScripts(std::u16string(1, char16_t(0xD800))).empty());
}

TEST(SpellingScripts, DictionaryScriptsComeFromTheSymbol)
{
    EXPECT_EQ(dictionaryScripts(u"en_US"), Scripts(Script::Latin));
    EXPECT_EQ(dictionaryScripts(u"pl"), Scripts(Script::Latin));
    EXPECT_EQ(dictionaryScripts(u"pl_PL"), Scripts(Script::Latin));
    EXPECT_EQ(dictionaryScripts(u"de_DE_frami"), Scripts(Script::Latin));
    EXPECT_EQ(dictionaryScripts(u"ko_KR"), Scripts(Script::Hangul));
    EXPECT_EQ(dictionaryScripts(u"ta"), Scripts(Script::Tamil));
    EXPECT_EQ(dictionaryScripts(u"ta_IN"), Scripts(Script::Tamil));
    EXPECT_EQ(dictionaryScripts(u"th_TH"), Scripts(Script::Thai));
    EXPECT_EQ(dictionaryScripts(u"ru_RU"), Scripts(Script::Cyrillic));
    EXPECT_EQ(dictionaryScripts(u"he_IL"), Scripts(Script::Hebrew));
    EXPECT_EQ(dictionaryScripts(u"ja"), Scripts(Script::Kana) | Script::Han);
    EXPECT_EQ(dictionaryScripts(u"EN-gb"), Scripts(Script::Latin));
    EXPECT_EQ(dictionaryScripts(u"sr"), Scripts(Script::Cyrillic));
    EXPECT_EQ(dictionaryScripts(u"sr_Latn_RS"), Scripts(Script::Latin));
    EXPECT_EQ(dictionaryScripts(u"sr-Latn"), Scripts(Script::Latin));
    EXPECT_TRUE(dictionaryScripts(u"qq").empty());
    EXPECT_TRUE(dictionaryScripts(u"custom-words").empty());
    EXPECT_TRUE(dictionaryScripts(u"").empty());
}

TEST(SpellingScripts, DominantScriptIsMostLetters)
{
    Script s = Script::Latin;
    EXPECT_TRUE(dominantScript(u"'-ابتثa", s));
    EXPECT_EQ(s, Script::Arabic);
    EXPECT_TRUE(dominantScript(u"esianrtolcdugmphbyfvkwz'", s));
    EXPECT_EQ(s, Script::Latin);
    EXPECT_FALSE(dominantScript(u"'-0123", s));
}
