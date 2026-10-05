// Y9: MKV extraction through the isolated media helper, against legacy Demux
// (Demux.cpp at 20d647c4) and the FFMS2 extension it calls
// (Thirdparty/Build/FFMS2/indexing_additional.cpp, the same file the
// hikari-ffms2 port builds): the subtitle tracks and their codecs, each
// track's packets in file order with their times and bytes, the codec-private
// data, the attachments byte for byte, progress, cancel and the helper's loss.

#include "hikari/backends/ffms_matroska.h"
#include "hikari/backends/media_protocol.h"
#include "mkv_fixture.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <limits>

using namespace hikari;
using namespace hikari::application;
namespace helper = hikari::backends::helper;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

std::string fixture(const char *kind)
{
    return std::string(HIKARI_MEDIA_FIXTURES) + "/mkv-" + kind + ".mkv";
}

std::string bytesText(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

struct Fixture : ::testing::Test {
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "matroska_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
    backends::FfmsMatroska port{QStringLiteral(HIKARI_MEDIA_HELPER)};

    std::expected<std::vector<MatroskaTrack>, MatroskaError> tracks(const std::string &path)
    {
        std::optional<std::expected<std::vector<MatroskaTrack>, MatroskaError>> result;
        port.subtitleTracks(path, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(MatroskaError{}));
    }
    std::expected<MatroskaSubtitles, MatroskaError> subtitles(const std::string &path, int track,
                                                              std::vector<std::pair<std::int64_t, std::int64_t>> *progress = nullptr)
    {
        std::optional<std::expected<MatroskaSubtitles, MatroskaError>> result;
        port.subtitles(path, track, [&](std::int64_t s, std::int64_t t) { if (progress) progress->push_back({s, t}); },
                       [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(MatroskaError{}));
    }
    std::expected<std::vector<MatroskaAttachment>, MatroskaError> attachments(const std::string &path)
    {
        std::optional<std::expected<std::vector<MatroskaAttachment>, MatroskaError>> result;
        port.attachments(path, [&](auto r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result.value_or(std::unexpected(MatroskaError{}));
    }
};

} // namespace

TEST_F(Fixture, SubtitleTracksInContainerOrder)
{
    // Every FFMS_TYPE_SUBTITLE track with its title, language and
    // FFMS_GetSubtitleFormat (Demux.cpp:68-76); the attachments that follow
    // the tracks are not listed.
    const auto t = tracks(fixture("subs"));
    ASSERT_TRUE(t) << t.error().text;
    std::string dump;
    for (const auto &x : *t)
        dump += std::to_string(x.track) + "|" + x.name + "|" + x.language + "|" + x.codec + "\n";
    // FFmpeg reads S_TEXT/SSA as AV_CODEC_ID_ASS, so FFMS2 names it "ass";
    // SubRip's decoder is "srt"; a track without a language is Matroska's
    // default "eng"; WebVTT has no decoder in the build, so no name.
    EXPECT_EQ(dump, "0|Signs|eng|ass\n"
                    "1||jpn|ass\n"
                    "2|Full|eng|srt\n"
                    "3||eng|text\n"
                    "4|Bitmap|eng|pgssub\n"
                    "5|Web|fre|\n");
}

TEST_F(Fixture, PacketsInFileOrderWithTheirMilliseconds)
{
    // FFMS_GetSubtitles reads every packet of the track in file order
    // (packets under 2 bytes skipped; a NUL ends the line), times in the
    // track's milliseconds; the codec-private data is FFMS2's extradata.
    const auto file = mkvfixture::subs();
    for (int track : {0, 1, 2, 3}) {
        SCOPED_TRACE(track);
        std::vector<std::pair<std::int64_t, std::int64_t>> progress;
        const auto read = subtitles(fixture("subs"), track, &progress);
        ASSERT_TRUE(read) << read.error().text;
        EXPECT_EQ(read->codecPrivate, file.tracks[std::size_t(track)].codecPrivate);
        std::vector<MatroskaPacket> expected;
        for (const auto &[t, p] : file.packets) {
            if (t != track || p.data.size() < 2)
                continue;
            expected.push_back({p.start, p.duration, std::string(p.data.c_str())});
        }
        ASSERT_EQ(read->packets.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(read->packets[i].start, expected[i].start) << i;
            EXPECT_EQ(read->packets[i].duration, expected[i].duration) << i;
            EXPECT_EQ(read->packets[i].line, expected[i].line) << i;
        }
        // One progress step per packet: its start and the stream's duration
        // in milliseconds, which FFmpeg leaves AV_NOPTS_VALUE for a
        // subtitle track even with a Segment duration, so legacy's percent
        // ((Start / Total) * 100, Demux.cpp:312) stayed 0.
        ASSERT_EQ(progress.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(progress[i].first, expected[i].start);
            EXPECT_EQ(progress[i].second, std::numeric_limits<std::int64_t>::min());
        }
    }
}

TEST_F(Fixture, AttachmentsAreByteIdentical)
{
    // Every FFMS_TYPE_ATTACHMENT stream in order with FFmpeg's filename and
    // mimetype metadata and its bytes (Demux.cpp:210-227, SaveFont 229-260).
    const auto file = mkvfixture::subs();
    const auto a = attachments(fixture("subs"));
    ASSERT_TRUE(a) << a.error().text;
    std::string dump;
    for (const auto &x : *a)
        dump += std::to_string(x.track) + "|" + (x.hasFilename ? "1" : "0") + "|" + x.filename + "|" + x.mimetype + "|" +
                std::to_string(x.data->size()) + "\n";
    // The picture is FFmpeg's attached picture (a video stream), not an
    // attachment; an empty name is "" with the key present.
    EXPECT_EQ(dump, "6|1|Fixture Sans.ttf|font/ttf|70000\n"
                    "7|1|dir/sub\\Nested.otf|application/vnd.ms-opentype|5000\n"
                    "8|1||font/otf|4000\n"
                    "9|1|..|application/x-truetype-font|3000\n"
                    "10|1|Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87.ttf|font/ttf|2000\n"
                    "11|1|Caf\xe9.ttf|font/ttf|1000\n"
                    "13|1|dup.ttf|font/ttf|1500\n"
                    "14|1|dup.ttf|application/x-truetype-font|1600\n");
    std::size_t matched = 0;
    for (const auto &x : *a) {
        for (const auto &f : file.attachments)
            if (f.filename == x.filename && f.mimetype == x.mimetype) {
                EXPECT_EQ(bytesText(*x.data), f.data) << x.filename;
                ++matched;
                break;
            }
    }
    EXPECT_EQ(matched, a->size());
}

TEST_F(Fixture, OneTrackAndNoTracks)
{
    const auto one = tracks(fixture("onesub"));
    ASSERT_TRUE(one);
    ASSERT_EQ(one->size(), 2u);
    EXPECT_EQ((*one)[0].codec, "pgssub");
    EXPECT_EQ((*one)[1].track, 1);
    EXPECT_EQ((*one)[1].name, "Polski");
    EXPECT_EQ((*one)[1].language, "pol");
    const auto none = tracks(fixture("nosubs"));
    ASSERT_TRUE(none);
    EXPECT_EQ(none->size(), 2u);
    const auto noAttachments = attachments(fixture("onesub"));
    ASSERT_TRUE(noAttachments);
    EXPECT_TRUE(noAttachments->empty());
}

TEST_F(Fixture, FilesFfms2CannotOpen)
{
    // Demux::Open: FFMS_CreateIndexer's failure, its text logged (Demux.cpp:44-47).
    const auto missing = tracks(fixture("missing"));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().failure, MatroskaFailure::CannotOpen);
    EXPECT_FALSE(missing.error().text.empty());
    const auto a = attachments(fixture("missing"));
    ASSERT_FALSE(a);
    EXPECT_EQ(a.error().failure, MatroskaFailure::CannotOpen);
    // A track that is not a subtitle track is refused.
    const auto attachment = subtitles(fixture("subs"), 6);
    ASSERT_FALSE(attachment);
    EXPECT_EQ(attachment.error().failure, MatroskaFailure::Failed);
    // The helper keeps serving.
    EXPECT_TRUE(tracks(fixture("subs")));
}

std::string outcomeOf(const std::expected<MatroskaSubtitles, MatroskaError> &r)
{
    return r ? "read" : "error " + std::to_string(int(r.error().failure));
}

const std::string kCancelled = "error " + std::to_string(int(MatroskaFailure::Cancelled));

TEST_F(Fixture, CancelReachesARunningReadAndTheHelperGoesOn)
{
    // The helper is up, so the request is sent at once and its Cancel goes
    // with it: the read stops at its first packet, onSubtitle returning
    // non-zero as legacy's WasCancelled() did (Demux.cpp:314), and the
    // helper ends the request Cancelled.
    ASSERT_TRUE(tracks(fixture("subs")));
    auto *host = port.helperHost();
    const auto session = host->session();
    std::vector<std::string> calls;
    std::vector<std::int64_t> progress;
    port.subtitles(fixture("subs"), 2, [&](std::int64_t start, std::int64_t) { progress.push_back(start); },
                   [&](auto r) { calls.push_back(outcomeOf(r)); });
    ASSERT_EQ(host->outstanding(), 1u); // in flight
    port.cancel();
    ASSERT_EQ(calls, std::vector<std::string>{kCancelled});
    // The helper's Terminal arrives and is dropped: the request resolved once.
    ASSERT_TRUE(waitFor([&] { return host->outstanding() == 0; }));
    EXPECT_EQ(calls.size(), 1u);
    EXPECT_EQ(progress.size(), 1u); // of the track's 4 packets
    // The same helper answers the next request.
    const auto read = subtitles(fixture("subs"), 2);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->packets.size(), 4u);
    EXPECT_EQ(port.helperHost(), host);
    EXPECT_EQ(host->session(), session);
    EXPECT_EQ(calls.size(), 1u);
}

TEST_F(Fixture, TheHelperEndsACancelledReadCancelled)
{
    // The helper's own answer to the cancel above, read off the protocol.
    helper::HelperHost host(QStringLiteral(HIKARI_MEDIA_HELPER), {}, backends::media::kProtocolVersion);
    host.start();
    ASSERT_TRUE(waitFor([&] { return host.state() == helper::HelperHost::State::Ready; }));
    std::vector<helper::Event> events;
    bool lost = false;
    const auto id = host.request(0,
                                 helper::Writer()
                                     .u8(static_cast<std::uint8_t>(backends::media::Command::Subtitles))
                                     .str(fixture("subs"))
                                     .i32(2)
                                     .take(),
                                 [&](std::expected<helper::Event, helper::HostError> e) {
                                     if (e)
                                         events.push_back(std::move(*e));
                                     else
                                         lost = true;
                                 });
    ASSERT_TRUE(id);
    host.cancel(*id);
    ASSERT_TRUE(waitFor([&] { return lost || (!events.empty() && events.back().kind == helper::Kind::Terminal); }));
    ASSERT_FALSE(lost);
    ASSERT_EQ(events.size(), 2u); // the first packet's progress, then the Terminal
    EXPECT_EQ(events[0].kind, helper::Kind::Progress);
    EXPECT_EQ(events[1].outcome, helper::Outcome::Cancelled);
}

TEST_F(Fixture, CancelWhileTheHelperStartsSendsNothing)
{
    // A cancel before the helper is up resolves the request at once, and the
    // request is never sent: nothing is outstanding when the helper is ready.
    std::vector<std::string> calls;
    port.subtitles(fixture("subs"), 0, {}, [&](auto r) { calls.push_back(outcomeOf(r)); });
    auto *host = port.helperHost();
    ASSERT_NE(host, nullptr);
    ASSERT_NE(host->state(), helper::HelperHost::State::Ready);
    port.cancel();
    ASSERT_EQ(calls, std::vector<std::string>{kCancelled});
    // Connected after the port's own, so this runs once the port has had
    // its turn at the ready signal.
    std::optional<std::size_t> outstandingWhenReady;
    QObject::connect(host, &helper::HelperHost::ready, [&] { outstandingWhenReady = host->outstanding(); });
    ASSERT_TRUE(waitFor([&] { return outstandingWhenReady.has_value(); }));
    EXPECT_EQ(*outstandingWhenReady, 0u);
    // The next request is answered, and the cancelled one stays resolved once.
    const auto read = subtitles(fixture("subs"), 2);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->packets.size(), 4u);
    EXPECT_EQ(calls.size(), 1u);
}

TEST_F(Fixture, HelperLossResolvesTheRequestAndTheNextStartsAHelper)
{
    ASSERT_TRUE(tracks(fixture("subs")));
    const auto session = port.helperHost()->session();
    std::optional<std::expected<std::vector<MatroskaAttachment>, MatroskaError>> pending;
    port.attachments(fixture("subs"), [&](auto r) { pending = std::move(r); });
    port.helperHost()->stop(); // the helper dies mid-request
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    ASSERT_FALSE(*pending);
    EXPECT_EQ(pending->error().failure, MatroskaFailure::HelperLost);
    const auto again = attachments(fixture("subs"));
    ASSERT_TRUE(again);
    EXPECT_EQ(again->size(), 8u);
    EXPECT_GT(port.helperHost()->session(), session);
}
