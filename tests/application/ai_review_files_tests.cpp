#include "hikari/application/ai_review.h"
#include "hikari/application/file_diff.h"

#include <gtest/gtest.h>

#include <map>

using namespace hikari::application;

namespace {

class StagedFiles : public ::testing::Test {
protected:
    std::map<std::string, std::string> m_disk{{"/p/notes.txt", "one\ntwo\n"}};
    bool m_writes = true;
    AiReview::Disk m_port{
        [this](const std::string &path) -> std::optional<std::string> {
            const auto it = m_disk.find(path);
            return it == m_disk.end() ? std::nullopt : std::optional(it->second);
        },
        [this](const std::string &path, std::string_view bytes) {
            if (m_writes)
                m_disk[path] = std::string(bytes);
            return m_writes;
        },
        [this](const std::string &path) { return m_disk.erase(path) == 1; }};
    AiReview m_review;

    std::uint64_t stage(const std::string &path, std::string after)
    {
        const auto it = m_disk.find(path);
        return m_review.stageFile("ai:t1", "AI", path, path.substr(3),
                                  it == m_disk.end() ? std::nullopt : std::optional(it->second), std::move(after));
    }
};

}

TEST_F(StagedFiles, AStagedFileWaitsAndTheDiskStaysUntilKept)
{
    const std::uint64_t id = stage("/p/notes.txt", "one\n2\n");
    EXPECT_EQ(m_disk["/p/notes.txt"], "one\ntwo\n");
    ASSERT_EQ(m_review.pendingFiles().size(), 1u);
    EXPECT_EQ(*m_review.staged("/p/notes.txt"), "one\n2\n");
    EXPECT_EQ(m_review.keepFile(id, m_port, false), AiReview::KeepFile::Kept);
    EXPECT_EQ(m_disk["/p/notes.txt"], "one\n2\n");
    EXPECT_TRUE(m_review.pendingFiles().empty());
    EXPECT_EQ(m_review.staged("/p/notes.txt"), nullptr);
}

TEST_F(StagedFiles, UndoDropsTheStagedCopyAndNeverTouchesTheDisk)
{
    const std::uint64_t id = stage("/p/new.txt", "fresh\n");
    m_writes = false;
    EXPECT_TRUE(m_review.undoFile(id));
    EXPECT_FALSE(m_disk.contains("/p/new.txt"));
    EXPECT_TRUE(m_review.pendingFiles().empty());
    EXPECT_FALSE(m_review.undoFile(id));
}

TEST_F(StagedFiles, ALaterWriteReplacesTheRowAndKeepsTheFirstBefore)
{
    stage("/p/notes.txt", "a\n");
    const std::uint64_t second = stage("/p/notes.txt", "b\n");
    ASSERT_EQ(m_review.pendingFiles().size(), 1u);
    EXPECT_EQ(m_review.pendingFiles().front()->id, second);
    EXPECT_EQ(m_review.pendingFiles().front()->before, std::optional<std::string>("one\ntwo\n"));
    EXPECT_EQ(*m_review.staged("/p/notes.txt"), "b\n");
}

TEST_F(StagedFiles, KeepAsksFirstWhenTheFileChangedOnDisk)
{
    const std::uint64_t id = stage("/p/notes.txt", "ai\n");
    m_disk["/p/notes.txt"] = "user\n";
    EXPECT_TRUE(m_review.changedOnDisk(*m_review.file(id), m_port));
    EXPECT_EQ(m_review.keepFile(id, m_port, false), AiReview::KeepFile::ChangedOnDisk);
    EXPECT_EQ(m_disk["/p/notes.txt"], "user\n");
    EXPECT_EQ(m_review.keepFile(id, m_port, true), AiReview::KeepFile::Kept);
    EXPECT_EQ(m_disk["/p/notes.txt"], "ai\n");
}

TEST_F(StagedFiles, AFailedWriteLeavesTheFileWaiting)
{
    const std::uint64_t id = stage("/p/notes.txt", "ai\n");
    m_writes = false;
    EXPECT_EQ(m_review.keepFile(id, m_port, false), AiReview::KeepFile::WriteFailed);
    EXPECT_EQ(m_review.pendingFiles().size(), 1u);
}

TEST_F(StagedFiles, KeepAllAndUndoAll)
{
    stage("/p/notes.txt", "x\n");
    stage("/p/b.txt", "b\n");
    m_disk["/p/notes.txt"] = "user\n";
    EXPECT_EQ(m_review.keepAllFiles(m_port, false), std::vector<std::string>{"/p/notes.txt"});
    EXPECT_EQ(m_disk["/p/b.txt"], "b\n");
    ASSERT_EQ(m_review.pendingFiles().size(), 1u);
    m_review.undoAllFiles();
    EXPECT_TRUE(m_review.pendingFiles().empty());
    EXPECT_EQ(m_disk["/p/notes.txt"], "user\n");
}

TEST_F(StagedFiles, FilesJoinTheCallersOpenTurn)
{
    const std::uint64_t turn = m_review.beginTurn("ai:t1", "Fix the notes");
    stage("/p/notes.txt", "x\n");
    EXPECT_EQ(m_review.pendingFiles().front()->turn, turn);
    m_review.stageFile("ai:t2", "Other", "/p/c.txt", "c.txt", std::nullopt, "c\n");
    EXPECT_NE(m_review.pendingFiles().back()->turn, turn);
}

TEST_F(StagedFiles, RestoringACheckpointDropsStagedFilesAndWritesKeptOnesBack)
{
    const std::uint64_t first = m_review.beginTurn("ai:t1", "One");
    m_review.keepFile(stage("/p/notes.txt", "kept\n"), m_port, false);
    stage("/p/new.txt", "new\n");
    m_review.keepFile(stage("/p/made.txt", "made\n"), m_port, false);
    const AiReview::CheckpointFiles preview = m_review.checkpointFiles(first, m_port);
    EXPECT_EQ(preview.staged, 1u);
    EXPECT_EQ(preview.kept, 2u);
    EXPECT_TRUE(preview.changedSince.empty());
    EXPECT_TRUE(m_review.restoreFiles(first, m_port, false).empty());
    EXPECT_EQ(m_disk["/p/notes.txt"], "one\ntwo\n");
    EXPECT_FALSE(m_disk.contains("/p/made.txt"));
    EXPECT_FALSE(m_disk.contains("/p/new.txt"));
    EXPECT_TRUE(m_review.pendingFiles().empty());
}

TEST_F(StagedFiles, RestoreLeavesAKeptFileTheUserChangedSinceUnlessForced)
{
    const std::uint64_t first = m_review.beginTurn("ai:t1", "One");
    m_review.keepFile(stage("/p/notes.txt", "kept\n"), m_port, false);
    m_disk["/p/notes.txt"] = "user\n";
    EXPECT_EQ(m_review.checkpointFiles(first, m_port).changedSince, std::vector<std::string>{"/p/notes.txt"});
    EXPECT_EQ(m_review.restoreFiles(first, m_port, false), std::vector<std::string>{"/p/notes.txt"});
    EXPECT_EQ(m_disk["/p/notes.txt"], "user\n");
    EXPECT_TRUE(m_review.restoreFiles(first, m_port, true).empty());
    EXPECT_EQ(m_disk["/p/notes.txt"], "one\ntwo\n");
}

TEST_F(StagedFiles, RestoringALaterTurnBringsBackTheEarlierStagedCopy)
{
    m_review.beginTurn("ai:t1", "One");
    stage("/p/notes.txt", "first\n");
    const std::uint64_t second = m_review.beginTurn("ai:t1", "Two");
    stage("/p/notes.txt", "second\n");
    m_review.restoreFiles(second, m_port, false);
    EXPECT_EQ(*m_review.staged("/p/notes.txt"), "first\n");
    EXPECT_EQ(m_disk["/p/notes.txt"], "one\ntwo\n");
}

TEST(FileDiff, CountsAndShowsChangedLinesWithContext)
{
    const FileDiff diff = fileDiff(std::string("a\nb\nc\nd\ne\nf\ng\nh\ni\n"), "a\nb\nc\nd\nE\nf\ng\nh\ni\nj\n", 1);
    ASSERT_TRUE(diff.text);
    EXPECT_EQ(diff.added, 2u);
    EXPECT_EQ(diff.removed, 1u);
    EXPECT_EQ(diff.lines, 10u);
    using K = FileDiffLine::Kind;
    std::vector<std::pair<K, std::string>> shown;
    for (const FileDiffLine &l : diff.shown)
        shown.emplace_back(l.kind, l.text);
    EXPECT_EQ(shown, (std::vector<std::pair<K, std::string>>{{K::Gap, ""},
                                                              {K::Same, "d"},
                                                              {K::Removed, "e"},
                                                              {K::Added, "E"},
                                                              {K::Same, "f"},
                                                              {K::Gap, ""},
                                                              {K::Same, "i"},
                                                              {K::Added, "j"}}));
}

TEST(FileDiff, ANewFileIsAllAdded)
{
    const FileDiff diff = fileDiff(std::nullopt, "x\r\ny\n");
    EXPECT_TRUE(diff.text);
    EXPECT_EQ(diff.added, 2u);
    EXPECT_EQ(diff.removed, 0u);
    EXPECT_EQ(diff.shown.front().text, "x");
}

TEST(FileDiff, BinaryAndLargeFilesShowSizesOnly)
{
    const FileDiff binary = fileDiff(std::string("ok"), std::string("\xff\xfe", 2));
    EXPECT_FALSE(binary.text);
    EXPECT_EQ(binary.afterBytes, 2u);
    const FileDiff large = fileDiff(std::nullopt, std::string(kFileDiffMaxBytes + 1, 'a'));
    EXPECT_FALSE(large.text);
    EXPECT_EQ(large.afterBytes, kFileDiffMaxBytes + 1);
}
