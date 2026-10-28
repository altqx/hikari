// A2 on the native platform: open a copied fixture, edit one Line, save
// through the atomic writer, reopen. Real files, the platform's reader and
// writer; the writer's worker completions are marshalled to this thread as
// the application does.

#include "hikari/application/document_files.h"
#include "hikari/backends/platform_files.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace hikari;
using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

struct Reopen : ::testing::Test {
    fs::path dir;
    std::mutex mutex;
    std::vector<std::pair<PermitId, WriteOutcome>> reported;
    std::unique_ptr<FileReadPort> reader = backends::makeFileReader();
    std::unique_ptr<backends::PlatformFilePort> port = backends::makeFilePort([this](PermitId p, WriteOutcome o) {
        std::lock_guard lock(mutex);
        reported.emplace_back(p, o);
    });
    DocumentFiles *files = nullptr;
    WriteCoordinator writes{*port, [this](const WriteResult &r) { files->onWriteResult(r); }};
    DocumentFiles service{*reader, writes};

    void SetUp() override
    {
        files = &service;
        // Unique per run, so parallel test processes never share a directory.
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("hikari-a2-" + std::to_string(stamp) + "-" +
                                           ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir);
        fs::create_directories(dir);
        fs::copy_file(HIKARI_FIXTURE_DIR "/inputs/unknown-sections.ass", dir / "copy.ass");
    }
    void TearDown() override
    {
        port->waitIdle();
        fs::remove_all(dir);
    }
    // Waits for the writer, then delivers its reports on this thread.
    void drain()
    {
        port->waitIdle();
        std::vector<std::pair<PermitId, WriteOutcome>> batch;
        {
            std::lock_guard lock(mutex);
            batch.swap(reported);
        }
        for (const auto &[permit, outcome] : batch)
            writes.complete(permit, outcome);
    }
    DocumentId open(const fs::path &path)
    {
        auto staged = service.stageOpen({reinterpret_cast<const char *>(path.u8string().c_str())});
        EXPECT_TRUE(staged);
        return *service.activate(std::move(*staged));
    }
};

} // namespace

TEST_F(Reopen, CopiedFileOpenEditSaveReopen)
{
    const std::string original = slurp(dir / "copy.ass");
    const auto doc = open(dir / "copy.ass");
    EditSession &s = *service.session(doc);
    const auto first = s.document().lines().front()->id;
    ASSERT_TRUE(s.editDraftText(first, u8"Gate, edited"));
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc)));
    drain();
    EXPECT_EQ(service.lastSave(doc)->outcome, WriteOutcome::Written);
    EXPECT_FALSE(s.isDirty());

    // Only that Line's bytes changed (C03: unknown sections stay in place).
    std::string expected = original;
    expected.replace(expected.find(",,Gate"), 6, ",,Gate, edited");
    EXPECT_EQ(slurp(dir / "copy.ass"), expected);

    ASSERT_TRUE(service.close(doc));
    const auto again = open(dir / "copy.ass");
    EXPECT_EQ(service.session(again)->document().lines().front()->text, u8"Gate, edited");
    EXPECT_FALSE(service.session(again)->isDirty());
}

TEST_F(Reopen, ExternalEditOnDiskIsDetectedByContent)
{
    const auto doc = open(dir / "copy.ass");
    {
        // Same size, different bytes: a size or timestamp check could miss it.
        std::string bytes = slurp(dir / "copy.ass");
        bytes[bytes.find("Gate")] = 'g';
        std::ofstream(dir / "copy.ass", std::ios::binary | std::ios::trunc) << bytes;
    }
    ASSERT_TRUE(service.session(doc)->editDraftText(service.session(doc)->document().lines().front()->id, u8"x"));
    auto plan = service.prepareSave(doc);
    EXPECT_EQ(service.startSave(*plan).error(), SaveRefusal::ExternalChange);
    EXPECT_NE(slurp(dir / "copy.ass").find("gate"), std::string::npos); // untouched
}

#ifndef _WIN32
TEST_F(Reopen, FailedSaveAsKeepsWorkAndAssociation)
{
    const auto doc = open(dir / "copy.ass");
    ASSERT_TRUE(service.session(doc)->editDraftText(service.session(doc)->document().lines().front()->id, u8"x"));
    fs::create_directories(dir / "locked");
    ::chmod((dir / "locked").c_str(), 0500);
    if (::access((dir / "locked").c_str(), W_OK) == 0)
        GTEST_SKIP() << "running with privileges that ignore directory permissions";
    auto plan = service.prepareSave(doc, DestinationKey{(dir / "locked" / "out.ass").string()});
    ASSERT_TRUE(service.startSave(*plan));
    drain();
    ::chmod((dir / "locked").c_str(), 0700);
    EXPECT_EQ(service.lastSave(doc)->outcome, WriteOutcome::Failed);
    EXPECT_TRUE(service.session(doc)->isDirty());
    EXPECT_EQ(service.destination(doc)->value, (dir / "copy.ass").string());
    EXPECT_FALSE(fs::exists(dir / "locked" / "out.ass"));
}
#endif
