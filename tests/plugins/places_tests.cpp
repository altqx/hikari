#include "hikari/plugins/places.h"

#include <gtest/gtest.h>

#include <fstream>
#include <random>

using hikari::plugins::pathInside;
namespace fs = std::filesystem;

namespace {

class PluginPlaces : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root = fs::temp_directory_path() / ("hikari-places-" + std::to_string(std::random_device{}()));
        ASSERT_TRUE(fs::create_directories(m_root / "data/notes"));
        ASSERT_TRUE(fs::create_directories(m_root / "outside"));
        std::ofstream(m_root / "outside/secret.txt") << "secret";
        std::ofstream(m_root / "data/notes/today.txt") << "today";
        m_data = fs::canonical(m_root / "data");
    }
    void TearDown() override { fs::remove_all(m_root); }

    bool link(const fs::path &target, const fs::path &at)
    {
        std::error_code error;
        if (fs::is_directory(target))
            fs::create_directory_symlink(target, at, error);
        else
            fs::create_symlink(target, at, error);
        return !error;
    }

    fs::path m_root;
    fs::path m_data;
};

}

TEST_F(PluginPlaces, NamedPathsStayInsideTheFolder)
{
    EXPECT_EQ(pathInside(m_data, "").value(), m_data);
    EXPECT_EQ(pathInside(m_data, "notes/today.txt").value(), m_data / "notes" / "today.txt");
    EXPECT_EQ(pathInside(m_data, "notes/new/later.txt").value(), m_data / "notes" / "new" / "later.txt");
    EXPECT_EQ(pathInside(m_data, "\xE3\x83\x8E\xE3\x83\xBC\xE3\x83\x88.txt").value(), m_data / fs::path(u8"\u30CE\u30FC\u30C8.txt"));
}

TEST_F(PluginPlaces, PathsThatClimbOutOrAreAbsoluteAreRefused)
{
    for (const std::string_view bad : {"..", "../outside/secret.txt", "notes/../../outside/secret.txt", "./notes", "notes/./today.txt",
                                       "/etc/passwd", "notes//today.txt", "notes/", "C:/Windows", "C:", "notes\\..\\..\\outside",
                                       "NUL", "con.txt", "notes/trailing.", "notes/trailing ", "a\nb", "what?"})
        EXPECT_FALSE(pathInside(m_data, bad).has_value()) << bad;
    EXPECT_FALSE(pathInside(m_data, std::string(1025, 'a')).has_value());
    EXPECT_FALSE(pathInside(m_data, "\xC3\x28").has_value());
    EXPECT_EQ(pathInside(m_data, "../outside/secret.txt").error(),
              "\xE2\x80\x9C../outside/secret.txt\xE2\x80\x9D has a part HikariSub refuses: \xE2\x80\x9C..\xE2\x80\x9D");
}

TEST_F(PluginPlaces, LinksThatLeadOutsideAreRefused)
{
    if (!link(m_root / "outside", m_data / "door") || !link(m_root / "outside/secret.txt", m_data / "key.txt"))
        GTEST_SKIP() << "this system can't make symbolic links here";
    EXPECT_EQ(pathInside(m_data, "door/secret.txt").error(),
              "\xE2\x80\x9C" "door/secret.txt\xE2\x80\x9D leads outside the folder");
    EXPECT_FALSE(pathInside(m_data, "door").has_value());
    EXPECT_FALSE(pathInside(m_data, "door/new.txt").has_value());
    EXPECT_FALSE(pathInside(m_data, "key.txt").has_value());
    ASSERT_TRUE(link(m_data / "notes", m_data / "shortcut"));
    EXPECT_EQ(pathInside(m_data, "shortcut/today.txt").value(), m_data / "notes" / "today.txt");
}

TEST_F(PluginPlaces, AMissingFolderIsRefused)
{
    EXPECT_FALSE(pathInside(m_root / "nowhere", "a.txt").has_value());
}
