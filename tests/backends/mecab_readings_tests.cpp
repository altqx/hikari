#include "hikari/backends/mecab_readings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <system_error>
#include <thread>

using namespace hikari::application;
using hikari::backends::MecabReadings;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<const MecabReadings> unidic()
{
    static const std::shared_ptr<const MecabReadings> readings = [] {
        std::string error;
        std::shared_ptr<const MecabReadings> r = MecabReadings::open(fs::path(HIKARI_UNIDIC_DIR), &error);
        EXPECT_TRUE(r) << error;
        return r;
    }();
    return readings;
}

std::u16string suggested(std::u16string_view text)
{
    std::u16string out;
    for (const auto &run : suggestReadings(text, MecabReadings::analyzer(unidic()))) {
        if (!out.empty())
            out += u' ';
        out += run.base + u'|' + run.reading();
    }
    return out;
}

}

TEST(MecabReadings, WordsWithTheirKanaForm)
{
    ASSERT_TRUE(unidic());
    const auto words = unidic()->words(u"永遠に続く");
    ASSERT_EQ(words.size(), 3u);
    EXPECT_EQ(words[0].surface, u"永遠");
    EXPECT_EQ(words[0].reading, u"エイエン");
    EXPECT_EQ(words[1].surface, u"に");
    EXPECT_EQ(words[1].reading, u"ニ");
    EXPECT_EQ(words[2].surface, u"続く");
    EXPECT_EQ(words[2].reading, u"ツヅク");
    EXPECT_TRUE(unidic()->words(u"").empty());
}

TEST(MecabReadings, LyricLines)
{
    ASSERT_TRUE(unidic());
    EXPECT_EQ(suggested(u"君の涙を抱きしめて"), u"君|きみ 涙|なみだ 抱|だ");
    EXPECT_EQ(suggested(u"生まれ変わってもまた会おう"), u"生|う 変|か 会|あ");
    EXPECT_EQ(suggested(u"二人で歩いた何処までも"), u"二人|ふたり 歩|ある 何処|どこ");
    EXPECT_EQ(suggested(u"一人きりの帰り道"), u"一人|ひとり 帰|かえ 道|みち");
    EXPECT_EQ(suggested(u"煌めく星が降る夜に"), u"煌|きら 星|ほし 降|ふ 夜|よる");
    EXPECT_EQ(suggested(u"{\\k20}永遠{\\k30}に\\N続く{\\k10}夢"), u"永遠|えいえん 続|つづ 夢|ゆめ");
}

TEST(MecabReadings, AFolderWithASpaceAndNonAsciiCharacters)
{
    const fs::path folder = fs::current_path() / u8"ふりがな dictionary ü";
    std::error_code ec;
    fs::remove_all(folder, ec);
    fs::create_directories(folder);
    for (const auto &entry : fs::directory_iterator(fs::path(HIKARI_UNIDIC_DIR))) {
        const fs::path target = folder / entry.path().filename();
        fs::create_hard_link(entry.path(), target, ec);
        if (ec)
            fs::copy_file(entry.path(), target);
    }
    {
        std::string error;
        const auto readings = MecabReadings::open(folder, &error);
        ASSERT_TRUE(readings) << error;
        const auto words = readings->words(u"夢");
        ASSERT_EQ(words.size(), 1u);
        EXPECT_EQ(words[0].reading, u"ユメ");
    }
    fs::remove_all(folder, ec);
}

TEST(MecabReadings, AFolderThatIsNoDictionary)
{
    std::string error;
    EXPECT_FALSE(MecabReadings::open(fs::current_path() / "no-such-dictionary", &error));
    EXPECT_FALSE(error.empty());
}

TEST(MecabReadings, ReadingsFromTwoThreads)
{
    ASSERT_TRUE(unidic());
    std::u16string a, b;
    std::thread first([&] {
        for (int i = 0; i < 50; ++i)
            a = suggested(u"桜舞い散る春の日に");
    });
    std::thread second([&] {
        for (int i = 0; i < 50; ++i)
            b = suggested(u"刹那の光を追いかけて");
    });
    first.join();
    second.join();
    EXPECT_EQ(a, u"桜|さくら 舞|ま 散|ち 春|はる 日|ひ");
    EXPECT_EQ(b, u"刹那|せつな 光|ひかり 追|お");
}
