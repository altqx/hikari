#include "hikari/backends/ffms_matroska.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/backends/media_protocol.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/matroska.h"
#include "mkv_fixture.h"

#include <QCoreApplication>
#include <QFile>
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

}

TEST_F(Fixture, SubtitleTracksInContainerOrder)
{
    const auto t = tracks(fixture("subs"));
    ASSERT_TRUE(t) << t.error().text;
    std::string dump;
    for (const auto &x : *t)
        dump += std::to_string(x.track) + "|" + x.name + "|" + x.language + "|" + x.codec + "\n";
    // FFmpeg names S_TEXT/SSA "ass"; Matroska's default language is "eng"; WebVTT has no decoder in the build.
    EXPECT_EQ(dump, "0|Signs|eng|ass\n"
                    "1||jpn|ass\n"
                    "2|Full|eng|srt\n"
                    "3||eng|text\n"
                    "4|Bitmap|eng|pgssub\n"
                    "5|Web|fre|\n");
}

TEST_F(Fixture, PacketsInFileOrderWithTheirMilliseconds)
{
    // FFMS2 skips packets under 2 bytes and ends a line at its first NUL.
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
        // FFmpeg leaves a subtitle stream's duration AV_NOPTS_VALUE, even with a Segment duration.
        ASSERT_EQ(progress.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(progress[i].first, expected[i].start);
            EXPECT_EQ(progress[i].second, std::numeric_limits<std::int64_t>::min());
        }
    }
}

TEST_F(Fixture, AttachmentsAreByteIdentical)
{
    const auto file = mkvfixture::subs();
    const auto a = attachments(fixture("subs"));
    ASSERT_TRUE(a) << a.error().text;
    std::string dump;
    for (const auto &x : *a)
        dump += std::to_string(x.track) + "|" + (x.hasFilename ? "1" : "0") + "|" + x.filename + "|" + x.mimetype + "|" +
                std::to_string(x.data->size()) + "\n";
    // FFmpeg makes the attached picture a video stream, not an attachment.
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
    const auto missing = tracks(fixture("missing"));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().failure, MatroskaFailure::CannotOpen);
    EXPECT_FALSE(missing.error().text.empty());
    const auto a = attachments(fixture("missing"));
    ASSERT_FALSE(a);
    EXPECT_EQ(a.error().failure, MatroskaFailure::CannotOpen);
    const auto attachment = subtitles(fixture("subs"), 6);
    ASSERT_FALSE(attachment);
    EXPECT_EQ(attachment.error().failure, MatroskaFailure::Failed);
    EXPECT_TRUE(tracks(fixture("subs")));
}

std::string outcomeOf(const std::expected<MatroskaSubtitles, MatroskaError> &r)
{
    return r ? "read" : "error " + std::to_string(int(r.error().failure));
}

const std::string kCancelled = "error " + std::to_string(int(MatroskaFailure::Cancelled));

TEST_F(Fixture, CancelReachesARunningReadAndTheHelperGoesOn)
{
    ASSERT_TRUE(tracks(fixture("subs")));
    auto *host = port.helperHost();
    const auto session = host->session();
    std::vector<std::string> calls;
    std::vector<std::int64_t> progress;
    port.subtitles(fixture("subs"), 2, [&](std::int64_t start, std::int64_t) { progress.push_back(start); },
                   [&](auto r) { calls.push_back(outcomeOf(r)); });
    ASSERT_EQ(host->outstanding(), 1u);
    port.cancel();
    ASSERT_EQ(calls, std::vector<std::string>{kCancelled});
    ASSERT_TRUE(waitFor([&] { return host->outstanding() == 0; }));
    EXPECT_EQ(calls.size(), 1u);
    EXPECT_EQ(progress.size(), 1u);
    const auto read = subtitles(fixture("subs"), 2);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->packets.size(), 4u);
    EXPECT_EQ(port.helperHost(), host);
    EXPECT_EQ(host->session(), session);
    EXPECT_EQ(calls.size(), 1u);
}

TEST_F(Fixture, TheHelperEndsACancelledReadCancelled)
{
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
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, helper::Kind::Progress);
    EXPECT_EQ(events[1].outcome, helper::Outcome::Cancelled);
}

TEST_F(Fixture, CancelWhileTheHelperStartsSendsNothing)
{
    std::vector<std::string> calls;
    port.subtitles(fixture("subs"), 0, {}, [&](auto r) { calls.push_back(outcomeOf(r)); });
    auto *host = port.helperHost();
    ASSERT_NE(host, nullptr);
    ASSERT_NE(host->state(), helper::HelperHost::State::Ready);
    port.cancel();
    ASSERT_EQ(calls, std::vector<std::string>{kCancelled});
    // Qt calls slots in connection order, so this runs after the port's own.
    std::optional<std::size_t> outstandingWhenReady;
    QObject::connect(host, &helper::HelperHost::ready, [&] { outstandingWhenReady = host->outstanding(); });
    ASSERT_TRUE(waitFor([&] { return outstandingWhenReady.has_value(); }));
    EXPECT_EQ(*outstandingWhenReady, 0u);
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
    port.helperHost()->stop();
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    ASSERT_FALSE(*pending);
    EXPECT_EQ(pending->error().failure, MatroskaFailure::HelperLost);
    const auto again = attachments(fixture("subs"));
    ASSERT_TRUE(again);
    EXPECT_EQ(again->size(), 8u);
    EXPECT_GT(port.helperHost()->session(), session);
}

TEST_F(Fixture, BlocksBeforeTheCuedClusterArriveAndTheirLineDraws)
{
    const auto file = mkvfixture::scanline();
    const auto read = subtitles(fixture("scanline"), 0);
    ASSERT_TRUE(read) << read.error().text;
    ASSERT_EQ(read->packets.size(), file.packets.size());
    for (std::size_t i = 0; i < file.packets.size(); ++i) {
        EXPECT_EQ(read->packets[i].start, file.packets[i].second.start) << i;
        EXPECT_EQ(read->packets[i].duration, file.packets[i].second.duration) << i;
        EXPECT_EQ(read->packets[i].line, file.packets[i].second.data) << i;
    }

    std::vector<std::u8string> lines;
    for (const auto &p : read->packets)
        lines.push_back(core::matroskaLine(p.start, p.duration, p.line, core::MatroskaCodec::Ass));
    const core::Document document = core::matroskaDocument(read->codecPrivate, lines, core::MatroskaCodec::Ass);
    core::AssSaveOptions options;
    options.renderer = true;
    QFile font(QStringLiteral(HIKARI_TEST_FONT));
    ASSERT_TRUE(font.open(QIODevice::ReadOnly));
    const QByteArray fontData = font.readAll();
    auto fontBytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(fontData.size()));
    std::memcpy(fontBytes->data(), fontData.constData(), fontBytes->size());
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(RenderSnapshot{core::encodeAss(document, options),
                                                {FontLease{"Titillium Web", fontBytes}}, "Titillium Web", false}));
    const auto frame = renderer.render(core::DocumentTime(2'000'000), 1920, 1080);
    ASSERT_TRUE(frame);
    ASSERT_FALSE(frame->empty);
    int inside = 0, outside = 0;
    for (int y = 200; y < 1080; ++y)
        for (int x = 0; x < 1920; ++x) {
            if (frame->pixels.data()[static_cast<std::size_t>(y) * frame->stride + x * 4 + 3] == 0)
                continue;
            if (x >= 720 && x < 1200 && y >= 975 && y < 1045)
                ++inside;
            else
                ++outside;
        }
    EXPECT_GT(inside, 5000);
    EXPECT_EQ(outside, 0);
}

TEST_F(Fixture, SubtitleTimesAreMillisecondsInEveryTimeBase)
{
    const struct {
        std::string path;
        const std::vector<mkvfixture::Expected> *ms;
    } cases[] = {
        {fixture("scale100us"), &mkvfixture::kScale100usMs},
        {fixture("scale10ms"), &mkvfixture::kScale10msMs},
        {std::string(HIKARI_MEDIA_FIXTURES) + "/subs-mp4.mp4", &mkvfixture::kMp4Ms},
        {std::string(HIKARI_MEDIA_FIXTURES) + "/subs-mov.mov", &mkvfixture::kMovMs},
        {std::string(HIKARI_MEDIA_FIXTURES) + "/subs-ts.ts", &mkvfixture::kTsMs},
    };
    for (const auto &c : cases) {
        SCOPED_TRACE(c.path);
        const auto t = tracks(c.path);
        ASSERT_TRUE(t) << t.error().text;
        ASSERT_EQ(t->size(), 1u);
        std::vector<std::pair<std::int64_t, std::int64_t>> progress;
        const auto read = subtitles(c.path, (*t)[0].track, &progress);
        ASSERT_TRUE(read) << read.error().text;
        ASSERT_EQ(read->packets.size(), c.ms->size());
        for (std::size_t i = 0; i < c.ms->size(); ++i) {
            EXPECT_EQ(read->packets[i].start, (*c.ms)[i].start) << i;
            EXPECT_EQ(read->packets[i].duration, (*c.ms)[i].duration) << i;
        }
        ASSERT_EQ(progress.size(), c.ms->size());
        for (std::size_t i = 0; i < c.ms->size(); ++i)
            EXPECT_EQ(progress[i].first, (*c.ms)[i].start) << i;
    }
}

TEST_F(Fixture, SubtitleTimesAtATenthOfAMillisecondMakeTheirLines)
{
    const auto read = subtitles(fixture("scale100us"), 0);
    ASSERT_TRUE(read) << read.error().text;
    ASSERT_EQ(read->packets.size(), 2u);
    EXPECT_EQ(read->packets[0].line, mkvfixture::kTimeBaseAssLine1);
    EXPECT_EQ(core::matroskaLine(read->packets[0].start, read->packets[0].duration, read->packets[0].line,
                                 core::MatroskaCodec::Ass),
              u8"Dialogue: 0,0:00:01.24,0:00:01.74,Default,,0,0,0,,one");
    EXPECT_EQ(core::matroskaLine(read->packets[1].start, read->packets[1].duration, read->packets[1].line,
                                 core::MatroskaCodec::Ass),
              u8"Dialogue: 2,0:00:02.47,0:00:03.47,Default,,0,0,0,,two");
}
