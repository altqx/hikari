// Y9: MKV extraction's application rules against legacy Demux (Demux.cpp at
// 20d647c4), SubsGrid::OnMkvSubs and its menu check (SubsGrid.cpp:289,
// 879-881, 1134-1194) and FontCollector::CopyMKVFonts/CopyMKVFontsFromTab
// (FontCollector.cpp:182, 549, 913-982), over a port that answers like the
// media helper. The helper itself is tested in tests/backends/matroska_tests.cpp.

#include "hikari/application/matroska.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <set>

using namespace hikari;
using namespace hikari::application;

namespace {

std::u8string u8(std::string_view s)
{
    return std::u8string(s.begin(), s.end());
}

std::shared_ptr<const std::vector<std::byte>> data(std::string_view s)
{
    auto out = std::make_shared<std::vector<std::byte>>(s.size());
    std::memcpy(out->data(), s.data(), s.size());
    return out;
}

// Answers when the test says so; cancel() resolves the running request
// Cancelled at once, as backends::FfmsMatroska does.
class FakePort final : public MatroskaPort {
public:
    Tracks tracks;
    Progress progress;
    Read read;
    Attachments attachmentsDone;
    std::vector<std::string> calls;
    int cancels = 0;

    void subtitleTracks(const std::string &path, Tracks done) override
    {
        calls.push_back("tracks " + path);
        tracks = std::move(done);
    }
    void subtitles(const std::string &path, int track, Progress p, Read done) override
    {
        calls.push_back("subtitles " + path + " " + std::to_string(track));
        progress = std::move(p);
        read = std::move(done);
    }
    void attachments(const std::string &path, Attachments done) override
    {
        calls.push_back("attachments " + path);
        attachmentsDone = std::move(done);
    }
    void cancel() override
    {
        ++cancels;
        const MatroskaError cancelled{MatroskaFailure::Cancelled, {}};
        if (auto t = std::exchange(tracks, nullptr))
            t(std::unexpected(cancelled));
        if (auto r = std::exchange(read, nullptr))
            r(std::unexpected(cancelled));
    }
};

struct Recorder {
    std::vector<std::string> events;
    std::vector<std::u8string> labels;
    std::vector<int> percents;
    std::optional<MatroskaLoaded> loaded;
    std::optional<MatroskaFailure> ended;

    MatroskaSubtitleLoad::Hooks hooks()
    {
        MatroskaSubtitleLoad::Hooks h;
        h.cannotOpen = [this](const std::string &e) { events.push_back("cannotOpen " + e); };
        h.noTracks = [this] { events.push_back("noTracks"); };
        h.chooseTrack = [this](const std::vector<std::u8string> &l) {
            labels = l;
            events.push_back("choose");
        };
        h.reading = [this] { events.push_back("reading"); };
        h.progress = [this](int p) { percents.push_back(p); };
        h.loaded = [this](MatroskaLoaded l) {
            events.push_back("loaded");
            loaded = std::move(l);
        };
        h.ended = [this](MatroskaFailure f) {
            events.push_back("ended " + std::to_string(int(f)));
            ended = f;
        };
        return h;
    }
};

std::vector<MatroskaTrack> fixtureTracks()
{
    // What the helper reads from tests/support/media's "subs" fixture.
    return {{0, "Signs", "eng", "ass"}, {1, "", "jpn", "ass"},       {2, "Full", "eng", "srt"},
            {3, "", "eng", "text"},     {4, "Bitmap", "eng", "pgssub"}, {5, "Web", "fre", ""}};
}

std::string ended(MatroskaFailure f)
{
    return "ended " + std::to_string(int(f));
}

} // namespace

TEST(MatroskaTracks, OfferedTracksAndTheirLabels)
{
    // Demux.cpp:72-81: ass, ssa, subrip, srt and text tracks, "%i " then the
    // name, " (", the language, ", ", the codec and ")".
    auto tracks = fixtureTracks();
    tracks.push_back({7, "Sub", "pol", "subrip"});
    tracks.push_back({8, "X", "eng", "ssa"});
    tracks.push_back({9, "\xff", "\xfe", "text"}); // not UTF-8: empty
    const auto offered = offeredTracks(tracks);
    std::vector<std::u8string> labels;
    for (const auto &o : offered)
        labels.push_back(o.label);
    EXPECT_EQ(labels, (std::vector<std::u8string>{u8"0 Signs (eng, ass)", u8"1  (jpn, ass)", u8"2 Full (eng, srt)",
                                                  u8"3  (eng, text)", u8"7 Sub (pol, subrip)", u8"8 X (eng, ssa)",
                                                  u8"9  (, text)"}));
    EXPECT_EQ(offered[2].track, 2);
    EXPECT_EQ(offered[2].codec, u8"srt");
}

TEST(MatroskaFonts, FontAttachmentsAndTheirNames)
{
    // GetFontList (Demux.cpp:210-227) with AttachmentName (188-208): the four
    // font MIME types; UTF-8 names, Latin-1 when they are not; the last path
    // component; "." and ".." and empty names become attachment_<track> with
    // .otf for the OpenType types.
    const std::vector<MatroskaAttachment> attachments{
        {6, true, "Fixture Sans.ttf", "font/ttf", data("a")},
        {7, true, "dir/sub\\Nested.otf", "application/vnd.ms-opentype", data("b")},
        {8, true, "", "font/otf", data("c")},
        {9, true, "..", "application/x-truetype-font", data("d")},
        {10, true, "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87.ttf", "font/ttf", data("e")},
        {11, true, "Caf\xe9.ttf", "font/ttf", data("f")},
        {12, true, "cover.png", "image/png", data("g")},
        {13, true, "dup.ttf", "font/ttf", data("h")},
        {14, true, "dup.ttf", "application/x-truetype-font", data("i")},
        {15, false, "", "application/vnd.ms-opentype", data("j")},
        {16, true, "dir/", "font/ttf", data("k")},
        {17, true, ".", "FONT/TTF", data("l")},
    };
    const auto fonts = matroskaFonts(attachments);
    std::vector<std::u16string> names;
    std::vector<int> tracks;
    for (const auto &f : fonts) {
        names.push_back(f.name);
        tracks.push_back(f.track);
    }
    EXPECT_EQ(names, (std::vector<std::u16string>{u"Fixture Sans.ttf", u"Nested.otf", u"attachment_8.otf",
                                                  u"attachment_9.ttf", u"Zażółć.ttf", u"Café.ttf", u"dup.ttf", u"dup.ttf",
                                                  u"attachment_15.otf", u"attachment_16.ttf"}));
    EXPECT_EQ(tracks, (std::vector<int>{6, 7, 8, 9, 10, 11, 13, 14, 15, 16}));
    EXPECT_EQ(fonts[1].data, attachments[1].data) << "the attachment's bytes, as they are";
}

TEST(MatroskaChecks, MenuAndCollectorChecks)
{
    // GRID_SUBS_FROM_MKV: VideoName ends with ".mkv" or ".ogm" in any case
    // (Y9-mkv-case; legacy's EndsWith was case-sensitive).
    EXPECT_TRUE(subtitlesFromMkvEnabled(u"a.mkv"));
    EXPECT_TRUE(subtitlesFromMkvEnabled(u"a.ogm"));
    EXPECT_TRUE(subtitlesFromMkvEnabled(u"a.MKV"));
    EXPECT_TRUE(subtitlesFromMkvEnabled(u"a.Ogm"));
    EXPECT_FALSE(subtitlesFromMkvEnabled(u"a.mkv.mp4"));
    EXPECT_FALSE(subtitlesFromMkvEnabled(u"a.mp4"));
    EXPECT_FALSE(subtitlesFromMkvEnabled(u""));
    // The collector's checkbox: VideoPath.Lower().EndsWith(".mkv").
    EXPECT_TRUE(fontsFromMkvEnabled(u"/v/A.MKV"));
    EXPECT_FALSE(fontsFromMkvEnabled(u"/v/a.ogm"));
    // CopyMKVFontsFromTab: AfterLast('.').Lower() == "mkv"; without a dot,
    // the whole path.
    EXPECT_TRUE(isMkvExtension(u"/v/a.Mkv"));
    EXPECT_TRUE(isMkvExtension(u"mkv"));
    EXPECT_FALSE(isMkvExtension(u""));
    EXPECT_FALSE(isMkvExtension(u"/v/a.mkv.mp4"));
}

TEST(MatroskaChecks, SubtitlePathBesideTheVideo)
{
    // OnMkvSubs: mkvpath.BeforeLast('.') + "." + ext (SubsGrid.cpp:1157, 1168).
    EXPECT_EQ(matroskaSubtitlePath("/v/ep 01.mkv", core::SubtitleFormat::Ass), "/v/ep 01.ass");
    EXPECT_EQ(matroskaSubtitlePath("/v/ep.01.mkv", core::SubtitleFormat::Srt), "/v/ep.01.srt");
    EXPECT_EQ(matroskaSubtitlePath("noext", core::SubtitleFormat::Ass), ".ass");
}

TEST(MatroskaSubtitleLoad, SeveralTracksAskThenReadTheChosenOne)
{
    FakePort port;
    Recorder r;
    MatroskaSubtitleLoad load(port, r.hooks());
    load.start("/v/ep.mkv");
    EXPECT_EQ(load.state(), MatroskaSubtitleLoad::State::Listing);
    port.tracks(fixtureTracks());
    EXPECT_EQ(load.state(), MatroskaSubtitleLoad::State::Choosing);
    EXPECT_EQ(r.labels, (std::vector<std::u8string>{u8"0 Signs (eng, ass)", u8"1  (jpn, ass)", u8"2 Full (eng, srt)",
                                                    u8"3  (eng, text)"}));
    load.choose(2);
    EXPECT_EQ(port.calls.back(), "subtitles /v/ep.mkv 2");
    // (Start / Total) * 100 as int; FFmpeg's AV_NOPTS_VALUE total gives 0.
    // A zero total (legacy's int of infinity, undefined) shows 0.
    port.progress(500, 2000);
    port.progress(1999, 2000);
    port.progress(1001, std::numeric_limits<std::int64_t>::min());
    port.progress(5, 0);
    EXPECT_EQ(r.percents, (std::vector<int>{25, 99, 0, 0}));
    MatroskaSubtitles read;
    read.packets = {{1001, 1499, "Line one\nLine two"}, {0, 0, "At zero"}};
    port.read(std::move(read));
    EXPECT_EQ(r.events, (std::vector<std::string>{"choose", "reading", "loaded"}));
    ASSERT_TRUE(r.loaded);
    EXPECT_EQ(r.loaded->track, 2);
    EXPECT_EQ(r.loaded->label, u8"2 Full (eng, srt)");
    EXPECT_EQ(r.loaded->document.format(), core::SubtitleFormat::Srt);
    EXPECT_EQ(r.loaded->subtitlePath, "/v/ep.srt");
    ASSERT_EQ(r.loaded->document.lines().size(), 2u);
    EXPECT_EQ(r.loaded->document.lines()[0]->text, u8"Line one\\NLine two");
    EXPECT_EQ(load.state(), MatroskaSubtitleLoad::State::Idle);
}

TEST(MatroskaSubtitleLoad, OneTrackIsReadWithoutAsking)
{
    // Demux.cpp:92-94; an ASS track's private data and packets.
    FakePort port;
    Recorder r;
    MatroskaSubtitleLoad load(port, r.hooks());
    load.start("/v/one.mkv");
    port.tracks(std::vector<MatroskaTrack>{{0, "", "eng", "pgssub"}, {1, "Signs", "eng", "ass"}});
    EXPECT_EQ(port.calls.back(), "subtitles /v/one.mkv 1");
    MatroskaSubtitles read;
    read.codecPrivate = "[Script Info]\r\nTitle: t\r\n";
    read.packets = {{1234, 2000, "0,0,Default,,0,0,0,,Hello"}};
    port.read(std::move(read));
    EXPECT_EQ(r.events, (std::vector<std::string>{"reading", "loaded"}));
    EXPECT_EQ(r.loaded->subtitlePath, "/v/one.ass");
    const auto out = core::encodeAss(r.loaded->document);
    const std::string text(reinterpret_cast<const char *>(out.data()), out.size());
    EXPECT_NE(text.find("Title: t\r\nYCbCr Matrix: TV.601\r\n"), std::string::npos) << text;
    EXPECT_NE(text.find("Dialogue: 0,0:00:01.23,0:00:03.23,Default,,0,0,0,,Hello\r\n"), std::string::npos) << text;
}

TEST(MatroskaSubtitleLoad, NoTracksAndFilesThatCannotBeOpened)
{
    FakePort port;
    Recorder r;
    MatroskaSubtitleLoad load(port, r.hooks());
    // "The file does not contain any subtitle tracks." (Demux.cpp:85-89).
    load.start("/v/none.mkv");
    port.tracks(std::vector<MatroskaTrack>{{0, "", "eng", "pgssub"}, {1, "", "eng", ""}});
    EXPECT_EQ(r.events, (std::vector<std::string>{"noTracks", ended(MatroskaFailure::Failed)}));
    // Demux::Open: only logged ("Indexing error occurred: %s").
    r.events.clear();
    load.start("/v/bad.mkv");
    port.tracks(std::unexpected(MatroskaError{MatroskaFailure::CannotOpen, "Can't open '/v/bad.mkv'"}));
    EXPECT_EQ(r.events, (std::vector<std::string>{"cannotOpen Can't open '/v/bad.mkv'", ended(MatroskaFailure::CannotOpen)}));
    // The helper lost while listing.
    r.events.clear();
    load.start("/v/lost.mkv");
    port.tracks(std::unexpected(MatroskaError{MatroskaFailure::HelperLost, {}}));
    EXPECT_EQ(r.events, (std::vector<std::string>{ended(MatroskaFailure::HelperLost)}));
    EXPECT_FALSE(r.loaded);
}

TEST(MatroskaSubtitleLoad, CancelAndLossLoadNothing)
{
    FakePort port;
    Recorder r;
    MatroskaSubtitleLoad load(port, r.hooks());
    // The chooser's Cancel (Demux.cpp:100-103): nothing is read.
    load.start("/v/ep.mkv");
    port.tracks(fixtureTracks());
    load.cancel();
    EXPECT_EQ(port.cancels, 0);
    EXPECT_EQ(r.events, (std::vector<std::string>{"choose", ended(MatroskaFailure::Cancelled)}));
    load.choose(0);
    EXPECT_EQ(port.calls.size(), 1u) << "a choice after Cancel reads nothing";
    // The progress dialog's Cancel (Demux.cpp:122-125): ended once.
    r.events.clear();
    load.start("/v/ep.mkv");
    port.tracks(fixtureTracks());
    load.choose(0);
    auto late = port.read; // the helper's answer, still on its way
    load.cancel();
    EXPECT_EQ(port.cancels, 1);
    EXPECT_EQ(r.events, (std::vector<std::string>{"choose", "reading", ended(MatroskaFailure::Cancelled)}));
    late(MatroskaSubtitles{});
    EXPECT_FALSE(r.loaded) << "a late answer is dropped";
    // Cancel while listing.
    r.events.clear();
    load.start("/v/ep.mkv");
    load.cancel();
    EXPECT_EQ(r.events, (std::vector<std::string>{ended(MatroskaFailure::Cancelled)}));
    // The helper lost while reading.
    r.events.clear();
    load.start("/v/ep.mkv");
    port.tracks(std::vector<MatroskaTrack>{{2, "", "eng", "srt"}});
    port.read(std::unexpected(MatroskaError{MatroskaFailure::HelperLost, {}}));
    EXPECT_EQ(r.events, (std::vector<std::string>{"reading", ended(MatroskaFailure::HelperLost)}));
    EXPECT_FALSE(r.loaded);
    // A new start drops the previous run's answers.
    r.events.clear();
    load.start("/v/a.mkv");
    auto first = port.tracks;
    load.start("/v/b.mkv");
    first(fixtureTracks());
    EXPECT_TRUE(r.events.empty());
}

namespace {

class MemoryOutput final : public CollectorOutput {
public:
    bool openOk = true, failFolder = false;
    std::set<std::u16string> failing;
    std::vector<std::pair<std::u16string, std::string>> written;
    int opens = 0;
    bool committed = false, discarded = false;
    std::atomic<bool> *cancelAfterPut = nullptr;
    bool open() override
    {
        ++opens;
        return openOk;
    }
    bool folderFailed() const override { return failFolder; }
    bool put(const std::u16string &name, const std::vector<std::byte> &bytes) override
    {
        if (failing.contains(name))
            return false;
        written.push_back({name, std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size())});
        if (cancelAfterPut)
            cancelAfterPut->store(true);
        return true;
    }
    bool label(const std::string &) override { return true; }
    bool unlabel() override { return true; }
    bool commit() override
    {
        committed = true;
        return true;
    }
    void discard() override { discarded = true; }
};

MatroskaFontTab tab(std::u16string video, MatroskaFontTab::Status status, std::vector<MatroskaFont> fonts = {})
{
    return {std::move(video), status, std::move(fonts)};
}

} // namespace

TEST(MatroskaFontsSave, EveryTabIntoOneOutput)
{
    // CopyMKVFonts (FontCollector.cpp:913-929): one output for every tab,
    // opened at the first tab with fonts (MakeDirectory), closed at the end.
    MemoryOutput output;
    output.failing = {u"bad.ttf"};
    const std::vector<MatroskaFontTab> tabs{
        tab(u"/v/a.mp4", MatroskaFontTab::Status::NotMkv),
        tab(u"/v/b.mkv", MatroskaFontTab::Status::NoFonts),
        tab(u"/v/c.mkv", MatroskaFontTab::Status::Fonts, {{6, u"one.ttf", data("1")}, {7, u"bad.ttf", data("2")}}),
        tab(u"/v/d.mkv", MatroskaFontTab::Status::CannotOpen),
        tab(u"/v/e.mkv", MatroskaFontTab::Status::Fonts, {{3, u"one.ttf", data("3")}}),
    };
    const auto saved = saveMatroskaFonts(tabs, output);
    ASSERT_EQ(saved.size(), 5u);
    EXPECT_TRUE(saved[0].saved.empty());
    EXPECT_EQ(saved[2].saved, (std::vector<bool>{true, false}));
    EXPECT_EQ(saved[4].saved, (std::vector<bool>{true}));
    EXPECT_EQ(output.opens, 1);
    EXPECT_TRUE(output.committed);
    // The bytes as they are; a later font of the same name is written again.
    EXPECT_EQ(output.written, (std::vector<std::pair<std::u16string, std::string>>{{u"one.ttf", "1"}, {u"one.ttf", "3"}}));
}

TEST(MatroskaFontsSave, NothingToWriteOpensNothing)
{
    MemoryOutput output;
    const auto saved = saveMatroskaFonts({tab(u"/v/b.mkv", MatroskaFontTab::Status::NoFonts)}, output);
    EXPECT_EQ(output.opens, 0);
    EXPECT_FALSE(output.committed);
    EXPECT_FALSE(saved[0].outputFailed);
}

TEST(MatroskaFontsSave, AnOutputThatCannotBeOpenedSkipsTheTab)
{
    // MakeDirectory fails: "Cannot create folder." and that tab returns;
    // the next tab with fonts tries again (FontCollector.cpp:953-958, 1000-1015).
    MemoryOutput output;
    output.openOk = false;
    output.failFolder = true;
    const std::vector<MatroskaFontTab> tabs{
        tab(u"/v/c.mkv", MatroskaFontTab::Status::Fonts, {{6, u"one.ttf", data("1")}}),
        tab(u"/v/e.mkv", MatroskaFontTab::Status::Fonts, {{3, u"two.ttf", data("3")}}),
    };
    const auto saved = saveMatroskaFonts(tabs, output);
    EXPECT_EQ(output.opens, 2);
    EXPECT_TRUE(saved[0].outputFailed);
    EXPECT_TRUE(saved[0].cannotCreateFolder);
    EXPECT_TRUE(saved[1].outputFailed);
    EXPECT_TRUE(output.written.empty());
    EXPECT_FALSE(output.committed);
}

TEST(MatroskaFontsSave, CancelDiscardsTheOutput)
{
    MemoryOutput output;
    std::atomic<bool> cancel = false;
    output.cancelAfterPut = &cancel;
    const auto saved = saveMatroskaFonts(
        {tab(u"/v/c.mkv", MatroskaFontTab::Status::Fonts, {{6, u"one.ttf", data("1")}, {7, u"two.ttf", data("2")}})},
        output, &cancel);
    EXPECT_TRUE(saved[0].cancelled);
    EXPECT_EQ(saved[0].saved, (std::vector<bool>{true}));
    EXPECT_TRUE(output.discarded);
    EXPECT_FALSE(output.committed);
}
