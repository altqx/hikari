// A2: open, save and reopen composed from staged loading and acknowledged
// writes. Fake ports: the "disk" is a map, and each test decides when and
// how a write finishes.

#include "hikari/application/document_files.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace hikari;
using namespace hikari::application;

namespace {

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

const std::string kAss = "[Script Info]\nTitle: t\n[Events]\n"
                         "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                         "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                         "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n";

struct Disk : FileReadPort {
    std::map<std::string, std::vector<std::byte>> files;
    std::expected<std::vector<std::byte>, ReadError> read(const DestinationKey &d) override
    {
        if (d.value == "/denied.ass")
            return std::unexpected(ReadError::AccessDenied);
        const auto it = files.find(d.value);
        if (it == files.end())
            return std::unexpected(ReadError::NotFound);
        return it->second;
    }
};

struct Writer : FilePort {
    struct Started {
        PermitId permit;
        DestinationKey destination;
        std::vector<std::byte> bytes;
    };
    std::vector<Started> started;
    void startWrite(PermitId permit, const DestinationKey &destination, std::vector<std::byte> bytes) override
    {
        started.push_back({permit, destination, std::move(bytes)});
    }
    void requestCancel(PermitId) override {}
};

class Files : public ::testing::Test {
protected:
    Disk disk;
    Writer writer;
    DocumentFiles *files = nullptr;
    WriteCoordinator writes{writer, [this](const WriteResult &r) { files->onWriteResult(r); }};
    DocumentFiles service{disk, writes};

    void SetUp() override
    {
        files = &service;
        disk.files["/a.ass"] = bytesOf(kAss);
    }
    DocumentId open(const std::string &path)
    {
        auto staged = service.stageOpen({path});
        EXPECT_TRUE(staged);
        auto id = service.activate(std::move(*staged));
        EXPECT_TRUE(id);
        return *id;
    }
    core::LineId line(DocumentId doc, std::size_t i) { return service.session(doc)->document().lines().at(i)->id; }
    // Finishes the latest write the way the adapter would, updating the disk.
    void finish(WriteOutcome outcome)
    {
        const auto &w = writer.started.back();
        if (outcome == WriteOutcome::Written || outcome == WriteOutcome::DurabilityUncertain)
            disk.files[w.destination.value] = w.bytes;
        writes.complete(w.permit, outcome);
    }
};

} // namespace

TEST_F(Files, OpenEditSaveReopen)
{
    const auto doc = open("/a.ass");
    EditSession &s = *service.session(doc);
    EXPECT_FALSE(s.isDirty());
    // A pending draft is committed by Save (commit-then-save).
    ASSERT_TRUE(s.editDraftText(line(doc, 0), u8"edited"));
    auto plan = service.prepareSave(doc);
    ASSERT_TRUE(plan);
    EXPECT_FALSE(s.draftLine());
    ASSERT_TRUE(service.startSave(*plan));
    EXPECT_TRUE(s.isDirty()); // not saved until the write is acknowledged
    finish(WriteOutcome::Written);
    EXPECT_FALSE(s.isDirty());
    EXPECT_EQ(service.lastSave(doc)->outcome, WriteOutcome::Written);

    std::string expected = kAss;
    expected.replace(expected.find(",one"), 4, ",edited");
    EXPECT_EQ(text(disk.files["/a.ass"]), expected); // every other byte unchanged
    ASSERT_TRUE(service.close(doc));
    const auto again = open("/a.ass");
    EXPECT_EQ(service.session(again)->document().lines()[0]->text, u8"edited");
}

TEST_F(Files, FailedOpensKeepExistingWork)
{
    const auto doc = open("/a.ass");
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"work"));
    EXPECT_EQ(service.stageOpen({"/missing.ass"}).error(), OpenError::NotFound);
    EXPECT_EQ(service.stageOpen({"/denied.ass"}).error(), OpenError::AccessDenied);
    disk.files["/empty.ass"] = {};
    EXPECT_EQ(service.stageOpen({"/empty.ass"}).error(), OpenError::InvalidFormat);
    EXPECT_EQ(service.session(doc)->draftText(), u8"work");
}

TEST_F(Files, ReloadIsStagedAndRejectedWhenStale)
{
    const auto doc = open("/a.ass");
    std::string changed = kAss;
    changed.replace(changed.find(",two"), 4, ",TWO");
    disk.files["/a.ass"] = bytesOf(changed);

    // Staged, then cancelled (never activated): nothing changes.
    auto cancelled = service.stageReload(doc);
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(service.session(doc)->document().lines()[1]->text, u8"two");

    // Staged, then the Document is edited: activation is refused.
    auto staged = service.stageReload(doc);
    ASSERT_TRUE(staged);
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"newer"));
    ASSERT_TRUE(service.session(doc)->commitDraft());
    EXPECT_EQ(service.activate(std::move(*staged)).error(), OpenError::StaleTarget);
    EXPECT_EQ(service.session(doc)->document().lines()[0]->text, u8"newer");

    // A current staged reload replaces the content and starts a new generation.
    auto fresh = service.stageReload(doc);
    ASSERT_TRUE(fresh);
    const auto generation = service.generation(doc);
    ASSERT_TRUE(service.activate(std::move(*fresh)));
    EXPECT_EQ(service.generation(doc), generation + 1);
    EXPECT_EQ(service.session(doc)->document().lines()[1]->text, u8"TWO");
    EXPECT_FALSE(service.session(doc)->isDirty());
}

TEST_F(Files, StalePlansAreRejected)
{
    const auto doc = open("/a.ass");
    EditSession &s = *service.session(doc);
    ASSERT_TRUE(s.editDraftText(line(doc, 0), u8"x"));
    auto plan = service.prepareSave(doc);
    ASSERT_TRUE(plan);
    ASSERT_TRUE(s.editDraftText(line(doc, 0), u8"y"));
    ASSERT_TRUE(s.commitDraft());
    EXPECT_EQ(service.startSave(*plan).error(), SaveRefusal::StalePlan);
    EXPECT_TRUE(writer.started.empty());
}

TEST_F(Files, FailedAndCancelledWritesLeaveWorkUnsaved)
{
    const auto doc = open("/a.ass");
    EditSession &s = *service.session(doc);
    for (const auto outcome : {WriteOutcome::Failed, WriteOutcome::Cancelled}) {
        ASSERT_TRUE(s.editDraftText(line(doc, 0), u8"unsaved"));
        auto plan = service.prepareSave(doc);
        ASSERT_TRUE(service.startSave(*plan));
        finish(outcome);
        EXPECT_TRUE(s.isDirty());
        EXPECT_EQ(service.lastSave(doc)->outcome, outcome);
        EXPECT_EQ(text(disk.files["/a.ass"]), kAss);
        EXPECT_EQ(s.document().lines()[0]->text, u8"unsaved"); // the work itself is kept
    }
}

TEST_F(Files, ExternalChangesNeedAnExplicitOverwrite)
{
    const auto doc = open("/a.ass");
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"mine"));
    disk.files["/a.ass"] = bytesOf(kAss + "Comment: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,theirs\n");
    auto plan = service.prepareSave(doc);
    ASSERT_TRUE(plan);
    EXPECT_EQ(service.startSave(*plan).error(), SaveRefusal::ExternalChange);
    EXPECT_TRUE(writer.started.empty());
    // A deleted file is an external change too.
    disk.files.erase("/a.ass");
    EXPECT_EQ(service.startSave(*plan).error(), SaveRefusal::ExternalChange);
    // The user chose to overwrite.
    ASSERT_TRUE(service.startSave(*plan, true));
    finish(WriteOutcome::Written);
    EXPECT_FALSE(service.session(doc)->isDirty());
    // The next save compares against what was just written, not the old load.
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 1), u8"next"));
    EXPECT_TRUE(service.startSave(*service.prepareSave(doc)));
}

TEST_F(Files, EditsDuringAWriteStayUnsaved)
{
    const auto doc = open("/a.ass");
    EditSession &s = *service.session(doc);
    ASSERT_TRUE(s.editDraftText(line(doc, 0), u8"saved"));
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc)));
    ASSERT_TRUE(s.editDraftText(line(doc, 1), u8"later"));
    ASSERT_TRUE(s.commitDraft());
    finish(WriteOutcome::Written);
    EXPECT_TRUE(s.isDirty()); // only the written snapshot became saved
    EXPECT_EQ(s.document().lines()[1]->text, u8"later");
    ASSERT_TRUE(s.undo());
    EXPECT_FALSE(s.isDirty()); // back at exactly the written content
}

TEST_F(Files, SaveAsMovesTheAssociationOnlyAfterSuccess)
{
    const auto doc = open("/a.ass");
    auto failed = service.prepareSave(doc, DestinationKey{"/b.ass"});
    ASSERT_TRUE(service.startSave(*failed));
    finish(WriteOutcome::Failed);
    EXPECT_EQ(service.destination(doc)->value, "/a.ass");
    EXPECT_EQ(writes.association(doc)->value, "/a.ass");

    auto plan = service.prepareSave(doc, DestinationKey{"/b.ass"});
    ASSERT_TRUE(service.startSave(*plan));
    EXPECT_EQ(service.destination(doc)->value, "/a.ass");
    finish(WriteOutcome::Written);
    EXPECT_EQ(service.destination(doc)->value, "/b.ass");
    EXPECT_EQ(writes.association(doc)->value, "/b.ass");
}

TEST_F(Files, SaveAsOntoAnotherOpenDocumentIsBlocked)
{
    disk.files["/b.ass"] = bytesOf(kAss);
    const auto a = open("/a.ass");
    open("/b.ass");
    auto plan = service.prepareSave(a, DestinationKey{"/b.ass"});
    ASSERT_TRUE(plan);
    EXPECT_EQ(service.startSave(*plan).error(), SaveRefusal::Collision);
}

TEST_F(Files, LateResultsDoNotPublish)
{
    const auto doc = open("/a.ass");
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"x"));
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc)));
    // The Document is reloaded before the write reports.
    auto staged = service.stageReload(doc);
    ASSERT_TRUE(staged);
    ASSERT_TRUE(service.activate(std::move(*staged)));
    const auto content = service.session(doc)->contentId();
    finish(WriteOutcome::Written);
    EXPECT_EQ(service.session(doc)->contentId(), content);
    EXPECT_FALSE(service.lastSave(doc)); // the reload's lifetime has no save yet

    // A write that reports after its Document closed changes nothing.
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"y"));
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc), true));
    ASSERT_TRUE(service.close(doc));
    finish(WriteOutcome::Written);
    EXPECT_FALSE(service.session(doc));
}

TEST_F(Files, ReaderDispatchFollowsTheLegacyLoader)
{
    // An .ass file holding SRT falls back to the SRT reader; a .txt holding
    // ASS events reloads as ASS.
    disk.files["/cues.ass"] = bytesOf("1\n00:00:01,000 --> 00:00:02,000\nHello\n");
    disk.files["/events.txt"] = bytesOf("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n");
    EXPECT_EQ(service.session(open("/cues.ass"))->document().format(), core::SubtitleFormat::Srt);
    EXPECT_EQ(service.session(open("/events.txt"))->document().format(), core::SubtitleFormat::Ass);
}
