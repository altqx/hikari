#include "hikari/application/document_files.h"

#include <gtest/gtest.h>

#include <cstring>
#include <map>

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
    void finish(WriteOutcome outcome)
    {
        const auto &w = writer.started.back();
        if (outcome == WriteOutcome::Written || outcome == WriteOutcome::DurabilityUncertain)
            disk.files[w.destination.value] = w.bytes;
        writes.complete(w.permit, outcome);
    }
};

}

TEST_F(Files, OpenEditSaveReopen)
{
    const auto doc = open("/a.ass");
    EditSession &s = *service.session(doc);
    EXPECT_FALSE(s.isDirty());
    ASSERT_TRUE(s.editDraftText(line(doc, 0), u8"edited"));
    auto plan = service.prepareSave(doc);
    ASSERT_TRUE(plan);
    EXPECT_FALSE(s.draftLine());
    ASSERT_TRUE(service.startSave(*plan));
    EXPECT_TRUE(s.isDirty());
    finish(WriteOutcome::Written);
    EXPECT_FALSE(s.isDirty());
    EXPECT_EQ(service.lastSave(doc)->outcome, WriteOutcome::Written);

    std::string expected = kAss;
    expected.replace(expected.find(",one"), 4, ",edited");
    expected.replace(expected.find("Title: t\n"), 9, "Title: t\nActive Line: 0\n");
    EXPECT_EQ(text(disk.files["/a.ass"]), expected);
    ASSERT_TRUE(service.close(doc));
    const auto again = open("/a.ass");
    EXPECT_EQ(service.session(again)->document().lines()[0]->text, u8"edited");
}

TEST_F(Files, FailedOpensKeepExistingWork)
{
    const auto doc = open("/a.ass");
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"work"));
    EXPECT_EQ(service.stageOpen({"/missing.ass"}).error().reason, OpenError::NotFound);
    EXPECT_EQ(service.stageOpen({"/denied.ass"}).error().reason, OpenError::AccessDenied);
    disk.files["/empty.ass"] = {};
    EXPECT_EQ(service.stageOpen({"/empty.ass"}).error().reason, OpenError::InvalidFormat);
    EXPECT_EQ(service.session(doc)->draftText(), u8"work");
}

TEST_F(Files, LyricsOpenAsUntitledImports)
{
    disk.files["/song.lrc"] = bytesOf("[00:01.00]<00:01.00>Twin<00:01.50>kle<00:02.00>\n");
    auto staged = service.stageOpen({"/song.lrc"});
    ASSERT_TRUE(staged);
    EXPECT_TRUE(staged->imported);
    const auto doc = service.activate(std::move(*staged));
    ASSERT_TRUE(doc);
    EXPECT_TRUE(service.destination(*doc)->value.empty());
    EXPECT_TRUE(service.session(*doc)->isDirty());
    ASSERT_EQ(service.session(*doc)->document().lines().size(), 1u);
    EXPECT_EQ(service.session(*doc)->document().lines()[0]->text, u8"{\\k50}Twin{\\k50}kle");
    EXPECT_EQ(service.prepareSave(*doc).error(), SaveRefusal::NoDestination);
    ASSERT_TRUE(service.prepareSave(*doc, DestinationKey{"/song.ass"}));
}

TEST_F(Files, MalformedLyricsSayWhereAndChangeNothing)
{
    const auto doc = open("/a.ass");
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"work"));
    disk.files["/bad.ttml"] = bytesOf("<tt>\n<body>\n<p>no time</p>\n</body>\n</tt>\n");
    const auto failed = service.stageOpen({"/bad.ttml"});
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().reason, OpenError::MalformedLyrics);
    EXPECT_EQ(failed.error().lyrics, (core::LyricsProblem{core::LyricsProblem::Kind::MissingTime, 3, u8"no time"}));
    EXPECT_EQ(service.session(doc)->draftText(), u8"work");
}

TEST_F(Files, ReloadIsStagedAndRejectedWhenStale)
{
    const auto doc = open("/a.ass");
    std::string changed = kAss;
    changed.replace(changed.find(",two"), 4, ",TWO");
    disk.files["/a.ass"] = bytesOf(changed);

    auto cancelled = service.stageReload(doc);
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(service.session(doc)->document().lines()[1]->text, u8"two");

    auto staged = service.stageReload(doc);
    ASSERT_TRUE(staged);
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"newer"));
    ASSERT_TRUE(service.session(doc)->commitDraft());
    EXPECT_EQ(service.activate(std::move(*staged)).error(), OpenError::StaleTarget);
    EXPECT_EQ(service.session(doc)->document().lines()[0]->text, u8"newer");

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
        EXPECT_EQ(s.document().lines()[0]->text, u8"unsaved");
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
    disk.files.erase("/a.ass");
    EXPECT_EQ(service.startSave(*plan).error(), SaveRefusal::ExternalChange);
    ASSERT_TRUE(service.startSave(*plan, true));
    finish(WriteOutcome::Written);
    EXPECT_FALSE(service.session(doc)->isDirty());
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 1), u8"next"));
    EXPECT_TRUE(service.startSave(*service.prepareSave(doc)));
}

TEST_F(Files, AnAcceptedExternalChangeIsWrittenOver)
{
    const auto doc = open("/a.ass");
    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"mine"));
    disk.files["/a.ass"] = bytesOf(kAss + "Comment: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,theirs\n");
    EXPECT_EQ(service.startSave(*service.prepareSave(doc)).error(), SaveRefusal::ExternalChange);
    ASSERT_TRUE(service.acceptExternalChange(doc));
    disk.files["/a.ass"] = bytesOf(kAss + "Comment: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,again\n");
    EXPECT_EQ(service.startSave(*service.prepareSave(doc)).error(), SaveRefusal::ExternalChange);
    ASSERT_TRUE(service.acceptExternalChange(doc));
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc)));
    finish(WriteOutcome::Written);
    EXPECT_FALSE(service.session(doc)->isDirty());
    EXPECT_FALSE(service.acceptExternalChange(service.createNew()));
    disk.files.erase("/a.ass");
    EXPECT_FALSE(service.acceptExternalChange(doc));
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
    EXPECT_TRUE(s.isDirty());
    EXPECT_EQ(s.document().lines()[1]->text, u8"later");
    ASSERT_TRUE(s.undo());
    EXPECT_FALSE(s.isDirty());
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
    auto staged = service.stageReload(doc);
    ASSERT_TRUE(staged);
    ASSERT_TRUE(service.activate(std::move(*staged)));
    const auto content = service.session(doc)->contentId();
    finish(WriteOutcome::Written);
    EXPECT_EQ(service.session(doc)->contentId(), content);
    EXPECT_FALSE(service.lastSave(doc));

    ASSERT_TRUE(service.session(doc)->editDraftText(line(doc, 0), u8"y"));
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc), true));
    ASSERT_TRUE(service.close(doc));
    finish(WriteOutcome::Written);
    EXPECT_FALSE(service.session(doc));
}

TEST_F(Files, ReaderDispatchFollowsTheLegacyLoader)
{
    disk.files["/cues.ass"] = bytesOf("1\n00:00:01,000 --> 00:00:02,000\nHello\n");
    disk.files["/events.txt"] = bytesOf("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n");
    EXPECT_EQ(service.session(open("/cues.ass"))->document().format(), core::SubtitleFormat::Srt);
    EXPECT_EQ(service.session(open("/events.txt"))->document().format(), core::SubtitleFormat::Ass);
}

TEST_F(Files, SaveWritesTheActiveLineIntoTheFileAndTheDocument)
{
    const auto doc = open("/a.ass");
    EditSession &s = *service.session(doc);
    s.setSelection(Selection{line(doc, 1), {line(doc, 1)}, line(doc, 1), {}});
    const auto steps = s.historySize();
    const auto revision = s.revision();
    const auto content = s.contentId();

    auto failed = service.prepareSave(doc);
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->activeLine, std::optional<std::u8string>(u8"1"));
    ASSERT_TRUE(service.startSave(*failed));
    EXPECT_FALSE(s.document().scriptInfo(kActiveLineKey));
    finish(WriteOutcome::Failed);
    EXPECT_FALSE(s.document().scriptInfo(kActiveLineKey));
    EXPECT_EQ(text(disk.files["/a.ass"]), kAss);

    auto plan = service.prepareSave(doc);
    ASSERT_TRUE(plan);
    ASSERT_TRUE(service.startSave(*plan));
    finish(WriteOutcome::Written);
    std::string expected = kAss;
    expected.replace(expected.find("Title: t\n"), 9, "Title: t\nActive Line: 1\n");
    EXPECT_EQ(text(disk.files["/a.ass"]), expected);
    EXPECT_EQ(s.document().scriptInfo(kActiveLineKey), std::optional<std::u8string>(u8"1"));
    EXPECT_EQ(s.historySize(), steps);
    EXPECT_EQ(s.revision(), revision);
    EXPECT_EQ(s.contentId(), content);
    EXPECT_FALSE(s.isDirty());

    s.setSelection(Selection{line(doc, 0), {line(doc, 0)}, line(doc, 0), {}});
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc)));
    finish(WriteOutcome::Written);
    expected.replace(expected.find("Active Line: 1"), 14, "Active Line: 0");
    EXPECT_EQ(text(disk.files["/a.ass"]), expected);
    EXPECT_EQ(s.document().scriptInfo(kActiveLineKey), std::optional<std::u8string>(u8"0"));
    EXPECT_FALSE(s.isDirty());

    disk.files["/c.ass"] = bytesOf("[Script Info]\nActive Line: 1\nTitle: t\n[Events]\n"
                                   "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n");
    const auto c = open("/c.ass");
    ASSERT_TRUE(service.startSave(*service.prepareSave(c)));
    finish(WriteOutcome::Written);
    EXPECT_EQ(text(disk.files["/c.ass"]).substr(0, 39), "[Script Info]\nActive Line: 0\nTitle: t\n[");
}

TEST_F(Files, OnlyAssSavesWriteTheActiveLine)
{
    disk.files["/a.srt"] = bytesOf("1\r\n00:00:01,000 --> 00:00:02,000\r\none\r\n\r\n");
    const auto doc = open("/a.srt");
    auto plan = service.prepareSave(doc);
    ASSERT_TRUE(plan);
    EXPECT_FALSE(plan->activeLine);
    EXPECT_EQ(text(plan->bytes).find("Active Line"), std::string::npos);
}

TEST_F(Files, SaveWritesTheLayoutIntoTheHikariSection)
{
    std::map<std::uint64_t, std::u8string> layouts;
    service.setLayoutSource([&](DocumentId d) -> std::optional<std::u8string> {
        const auto it = layouts.find(d.value);
        return it == layouts.end() ? std::nullopt : std::optional(it->second);
    });
    const auto doc = open("/a.ass");
    EditSession &s = *service.session(doc);
    layouts[doc.value] = u8"QUJD";
    EXPECT_FALSE(s.isDirty());
    const auto steps = s.historySize();
    const auto content = s.contentId();

    auto failed = service.prepareSave(doc);
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->layout, std::optional<std::u8string>(u8"QUJD"));
    ASSERT_TRUE(service.startSave(*failed));
    finish(WriteOutcome::Failed);
    EXPECT_FALSE(s.document().hikariProperty(kLayoutKey));

    ASSERT_TRUE(service.startSave(*service.prepareSave(doc)));
    finish(WriteOutcome::Written);
    std::string expected = kAss;
    expected.replace(expected.find("Title: t\n"), 9, "Title: t\nActive Line: 0\n");
    EXPECT_EQ(text(disk.files["/a.ass"]), expected + "\n[Hikari]\nLayout=QUJD\n");
    EXPECT_EQ(s.document().hikariProperty(kLayoutKey), std::optional<std::u8string>(u8"QUJD"));
    EXPECT_EQ(s.historySize(), steps);
    EXPECT_EQ(s.contentId(), content);
    EXPECT_FALSE(s.isDirty());

    layouts[doc.value] = u8"TEFTVA==";
    ASSERT_TRUE(service.startSave(*service.prepareSave(doc)));
    finish(WriteOutcome::Written);
    EXPECT_EQ(text(disk.files["/a.ass"]), expected + "\n[Hikari]\nLayout=TEFTVA==\n");
    layouts.erase(doc.value);
    auto kept = service.prepareSave(doc);
    ASSERT_TRUE(kept);
    EXPECT_FALSE(kept->layout);
    EXPECT_EQ(text(kept->bytes), expected + "\n[Hikari]\nLayout=TEFTVA==\n");

    ASSERT_TRUE(service.close(doc));
    EXPECT_EQ(service.session(open("/a.ass"))->document().hikariProperty(kLayoutKey),
              std::optional<std::u8string>(u8"TEFTVA=="));
}

TEST_F(Files, OnlyAssSavesWriteTheLayout)
{
    bool asked = false;
    service.setLayoutSource([&](DocumentId) -> std::optional<std::u8string> {
        asked = true;
        return u8"QUJD";
    });
    disk.files["/a.srt"] = bytesOf("1\r\n00:00:01,000 --> 00:00:02,000\r\none\r\n\r\n");
    const auto doc = open("/a.srt");
    auto plan = service.prepareSave(doc);
    ASSERT_TRUE(plan);
    EXPECT_FALSE(asked);
    EXPECT_FALSE(plan->layout);
    EXPECT_EQ(text(plan->bytes), "1\r\n00:00:01,000 --> 00:00:02,000\r\none\r\n\r\n");
}
