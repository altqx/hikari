#include "hikari/plugins/archive.h"

#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>

using namespace hikari::plugins;
namespace fs = std::filesystem;
using Kind = UnpackProblem::Kind;

namespace {

struct Entry {
    std::string path;
    std::string data;
    char type = '0';
    unsigned mode = 0644;
    std::string prefix;
};

void octal(std::string &header, std::size_t at, std::size_t size, std::uint64_t value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%0*llo", int(size - 1), static_cast<unsigned long long>(value));
    header.replace(at, size - 1, text);
}

std::string headerFor(const Entry &e)
{
    std::string h(512, '\0');
    h.replace(0, std::min<std::size_t>(e.path.size(), 100), e.path.substr(0, 100));
    octal(h, 100, 8, e.mode);
    octal(h, 108, 8, 0);
    octal(h, 116, 8, 0);
    octal(h, 124, 12, e.data.size());
    octal(h, 136, 12, 0);
    h[156] = e.type;
    h.replace(257, 6, std::string("ustar\0", 6));
    h.replace(263, 2, "00");
    h.replace(345, e.prefix.size(), e.prefix);
    h.replace(148, 8, "        ");
    unsigned sum = 0;
    for (unsigned char c : h)
        sum += c;
    char text[8];
    std::snprintf(text, sizeof text, "%06o", sum);
    h.replace(148, 7, std::string(text, 6) + '\0');
    return h;
}

std::string tar(const std::vector<Entry> &entries, bool end = true)
{
    std::string out;
    for (const Entry &e : entries) {
        out += headerFor(e);
        out += e.data;
        out.append((512 - e.data.size() % 512) % 512, '\0');
    }
    if (end)
        out.append(1024, '\0');
    return out;
}

std::string gzip(const std::string &data)
{
    z_stream s{};
    deflateInit2(&s, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::string out(deflateBound(&s, uLong(data.size())) + 64, '\0');
    s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    s.avail_in = uInt(data.size());
    s.next_out = reinterpret_cast<Bytef *>(out.data());
    s.avail_out = uInt(out.size());
    deflate(&s, Z_FINISH);
    out.resize(s.total_out);
    deflateEnd(&s);
    return out;
}

std::string pax(const std::string &key, const std::string &value)
{
    std::string record = " " + key + "=" + value + "\n";
    std::size_t length = record.size() + 1;
    while (std::to_string(length).size() + record.size() != length)
        ++length;
    return std::to_string(length) + record;
}

class ArchiveTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root = fs::temp_directory_path() / ("hikari-archive-" + std::to_string(std::random_device{}()));
        fs::create_directories(m_root / "into");
    }
    void TearDown() override { fs::remove_all(m_root); }

    std::expected<void, UnpackProblem> unpack(const std::string &bytes, UnpackLimits limits = {1 << 20, 100})
    {
        fs::remove_all(m_root / "into");
        fs::create_directories(m_root / "into");
        std::ofstream(m_root / "a.tar.gz", std::ios::binary) << bytes;
        return unpackTarGz(m_root / "a.tar.gz", m_root / "into", limits);
    }
    Kind failure(const std::string &bytes, UnpackLimits limits = {1 << 20, 100})
    {
        const auto result = unpack(bytes, limits);
        EXPECT_FALSE(result);
        return result ? Kind::Write : result.error().kind;
    }
    std::string read(const std::string &path) const
    {
        std::ifstream in(m_root / "into" / path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }
    void expectNothingOutside() const
    {
        std::vector<std::string> names;
        for (const auto &entry : fs::directory_iterator(m_root))
            names.push_back(entry.path().filename().string());
        std::ranges::sort(names);
        EXPECT_EQ(names, (std::vector<std::string>{"a.tar.gz", "into"}));
    }

    fs::path m_root;
};

TEST_F(ArchiveTest, FilesFoldersAndLongNamesUnpack)
{
    const std::string deep = std::string(60, 'd') + "/" + std::string(60, 'e') + ".txt";
    ASSERT_TRUE(unpack(gzip(tar({{"./bin/", "", '5', 0755},
                                 {"bin/tool", "#!/bin/sh\n", '0', 0755},
                                 {"lib/readme.txt", "hello"},
                                 {"tail.txt", "in ustar", '0', 0644, "nested/prefix"},
                                 {"././@LongLink", deep + '\0', 'L'},
                                 {"cut", "long via gnu"},
                                 {"PaxHeaders/x", pax("path", "pax/name.txt"), 'x'},
                                 {"ignored", "long via pax"}}))));
    EXPECT_EQ(read("bin/tool"), "#!/bin/sh\n");
    EXPECT_EQ(read("lib/readme.txt"), "hello");
    EXPECT_EQ(read("nested/prefix/tail.txt"), "in ustar");
    EXPECT_EQ(read(deep), "long via gnu");
    EXPECT_EQ(read("pax/name.txt"), "long via pax");
#ifndef _WIN32
    EXPECT_NE(fs::status(m_root / "into/bin/tool").permissions() & fs::perms::owner_exec, fs::perms::none);
    EXPECT_EQ(fs::status(m_root / "into/lib/readme.txt").permissions() & fs::perms::owner_exec, fs::perms::none);
#endif
}

TEST_F(ArchiveTest, PathsThatClimbOutOrAreAbsoluteAreRefused)
{
    for (const std::string path : {"../evil", "a/../../evil", "/etc/evil", "a//b", "C:evil", "a\\..\\evil", "con/x",
                                   "trailing.", "a/./b"}) {
        EXPECT_EQ(failure(gzip(tar({{path, "x"}}))), Kind::BadPath) << path;
        expectNothingOutside();
    }
    EXPECT_EQ(failure(gzip(tar({{"PaxHeaders/x", pax("path", "../../evil"), 'x'}, {"fine", "x"}}))), Kind::BadPath);
    EXPECT_EQ(failure(gzip(tar({{"././@LongLink", std::string("/abs/evil") + '\0', 'L'}, {"fine", "x"}}))), Kind::BadPath);
    expectNothingOutside();
}

TEST_F(ArchiveTest, LinksAndSpecialFilesAreRefused)
{
    for (const char type : {'1', '2', '3', '4', '6'})
        EXPECT_EQ(failure(gzip(tar({{"link", "", type}}))), Kind::Link) << type;
    Entry outward{"out", "", '2'};
    EXPECT_EQ(failure(gzip(tar({outward, {"out/evil", "x"}}))), Kind::Link);
    expectNothingOutside();
}

TEST_F(ArchiveTest, RepeatedOrClashingPathsAreRefused)
{
    EXPECT_EQ(failure(gzip(tar({{"Tool", "a"}, {"tool", "b"}}))), Kind::BadPath);
    EXPECT_EQ(failure(gzip(tar({{"tool", "a"}, {"tool/x", "b"}}))), Kind::BadPath);
    EXPECT_EQ(failure(gzip(tar({{"tool/x", "a"}, {"tool", "b"}}))), Kind::BadPath);
    EXPECT_EQ(failure(gzip(tar({{"tool", "a"}, {"tool", "", '5'}}))), Kind::BadPath);
}

TEST_F(ArchiveTest, TooMuchIsRefusedBeforeItIsWritten)
{
    EXPECT_EQ(failure(gzip(tar({{"big", std::string(2000, 'x')}})), {1000, 10}), Kind::TooLarge);
    EXPECT_FALSE(fs::exists(m_root / "into/big"));
    EXPECT_EQ(failure(gzip(tar({{"a", std::string(600, 'x')}, {"b", std::string(600, 'x')}})), {1000, 10}), Kind::TooLarge);
    EXPECT_EQ(failure(gzip(tar({{"a", "1"}, {"b", "2"}, {"c", "3"}})), {1000, 2}), Kind::TooLarge);
    std::string base256 = tar({{"huge", ""}});
    base256[124] = char(0x80);
    EXPECT_NE(failure(gzip(base256)), Kind::Write);
}

TEST_F(ArchiveTest, CutOrBrokenInputIsRefused)
{
    std::mt19937 random(7);
    std::string noise(5000, '\0');
    for (char &c : noise)
        c = char(random() % 256);
    const std::string whole = gzip(tar({{"a", noise}, {"b", "y"}}));
    EXPECT_EQ(failure(whole.substr(0, whole.size() / 2)), Kind::Truncated);
    EXPECT_EQ(failure(gzip(tar({{"a", std::string(5000, 'x')}}, false).substr(0, 2048))), Kind::Truncated);
    EXPECT_EQ(failure("not gzip at all"), Kind::NotAnArchive);
    EXPECT_EQ(failure(gzip(std::string(1024, 'z'))), Kind::NotAnArchive);
    EXPECT_EQ(failure(gzip("")), Kind::NotAnArchive);
    std::string badSum = tar({{"a", "x"}});
    badSum[0] = 'b';
    EXPECT_EQ(failure(gzip(badSum)), Kind::NotAnArchive);
}

TEST_F(ArchiveTest, MangledArchivesNeverWriteOutsideTheTarget)
{
    const std::string clean = tar({{"bin/", "", '5', 0755}, {"bin/tool", std::string(3000, 't'), '0', 0755},
                                   {"PaxHeaders/x", pax("path", "doc/a.txt"), 'x'}, {"x", "doc"}, {"lib/b", "b"}});
    std::mt19937 random(1226);
    for (int round = 0; round < 400; ++round) {
        std::string mangled = clean;
        const int flips = 1 + int(random() % 8);
        for (int i = 0; i < flips; ++i)
            mangled[random() % mangled.size()] = char(random() % 256);
        if (round % 5 == 0)
            mangled.resize(random() % mangled.size());
        (void)unpack(gzip(mangled), {1 << 20, 100});
        expectNothingOutside();
        for (const auto &entry : fs::recursive_directory_iterator(m_root / "into"))
            ASSERT_FALSE(entry.is_symlink()) << entry.path();
    }
}

}
