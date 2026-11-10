#include "hikari/application/compile_cache.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

namespace fs = std::filesystem;
using hikari::application::CompileCache;
using hikari::application::CompiledSource;

namespace {

class CompileCacheTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::random_device rd;
        folder = fs::temp_directory_path() / ("hikari-compile-cache-" + std::to_string(rd()));
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
    std::size_t entries() const
    {
        std::error_code ec;
        return static_cast<std::size_t>(std::distance(fs::directory_iterator(folder, ec), fs::directory_iterator()));
    }

    fs::path folder;
    const CompiledSource compiled{"return 1\nerror 'x'\n", {{1, 0}, {2, 14.5}}};
};

TEST_F(CompileCacheTest, AnEntryAnswersForItsOwnSourceAndCompiler)
{
    const CompileCache cache(folder, CompileCache::compilerId("compiler 1"));
    EXPECT_FALSE(cache.find("a.moon", "x = 1"));
    cache.store("a.moon", "x = 1", compiled);
    EXPECT_EQ(cache.find("a.moon", "x = 1"), compiled);
    EXPECT_FALSE(cache.find("a.moon", "x = 2")) << "a changed source misses";
    EXPECT_FALSE(cache.find("b.moon", "x = 1"));
    EXPECT_FALSE(CompileCache(folder, CompileCache::compilerId("compiler 2")).find("a.moon", "x = 1"))
        << "a new compiler misses";
    EXPECT_EQ(entries(), 1u);
}

TEST_F(CompileCacheTest, ACompilerIsKnownByItsWholeText)
{
    EXPECT_EQ(CompileCache::compilerId("abc"), CompileCache::compilerId("abc"));
    EXPECT_NE(CompileCache::compilerId("abc"), CompileCache::compilerId("abd"));
    EXPECT_NE(CompileCache::compilerId("abc"), CompileCache::compilerId("abc "));
}

TEST_F(CompileCacheTest, ARecompiledFileReplacesItsEntry)
{
    const CompileCache cache(folder, CompileCache::compilerId("compiler"));
    cache.store("a.moon", "x = 1", compiled);
    const CompiledSource again{"return 2\n", {{1, 0}}};
    cache.store("a.moon", "x = 2", again);
    EXPECT_EQ(cache.find("a.moon", "x = 2"), again);
    EXPECT_FALSE(cache.find("a.moon", "x = 1"));
    EXPECT_EQ(entries(), 1u);
}

TEST_F(CompileCacheTest, ADamagedEntryMisses)
{
    const CompileCache cache(folder, CompileCache::compilerId("compiler"));
    cache.store("a.moon", "x = 1", compiled);
    ASSERT_EQ(entries(), 1u);
    const fs::path file = fs::directory_iterator(folder)->path();
    std::string bytes;
    {
        std::ifstream in(file, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), {});
    }
    for (const std::size_t keep : {bytes.size() - 1, bytes.size() / 2, std::size_t{3}}) {
        std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes.substr(0, keep);
        EXPECT_FALSE(cache.find("a.moon", "x = 1")) << keep;
    }
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes << "extra";
    EXPECT_FALSE(cache.find("a.moon", "x = 1"));
}

TEST_F(CompileCacheTest, AFolderThatCantBeWrittenLeavesNothingBehind)
{
    fs::create_directories(folder);
    std::ofstream(folder / "blocked") << "a file, not a folder";
    const CompileCache cache(folder / "blocked", CompileCache::compilerId("compiler"));
    cache.store("a.moon", "x = 1", compiled);
    EXPECT_FALSE(cache.find("a.moon", "x = 1"));
    EXPECT_EQ(entries(), 1u);
}

}
