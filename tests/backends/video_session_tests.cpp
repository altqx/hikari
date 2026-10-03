// I1: the video session over the real media helper and libass: Line seeks
// and frame steps decode the frames they name (barcode identity), with the
// overlay rendered at each frame's start.
#include "hikari/application/video_session.h"
#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/libass_renderer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <gtest/gtest.h>

#include <cstring>
#include <functional>

using namespace hikari;
using namespace hikari::application;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 20'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

int barcode(const IndexedFrame &f)
{
    const QImage image(reinterpret_cast<const uchar *>(f.bgra.data()), f.width, f.height, f.stride,
                       QImage::Format_RGB32);
    int value = 0;
    for (int by = 0; by < 4; ++by)
        for (int bx = 0; bx < 4; ++bx) {
            const QRgb p = image.pixel((2 * bx + 1) * image.width() / 8, (2 * by + 1) * image.height() / 8);
            if ((qRed(p) + qGreen(p) + qBlue(p)) / 3 > 128)
                value |= 1 << (by * 4 + bx);
        }
    return value;
}

struct RecordingPresenter : PresenterPort {
    std::vector<int> shown;
    std::vector<bool> overlays;
    void present(Presentation p, Done done) override
    {
        shown.push_back(barcode(*p.frame));
        overlays.push_back(p.overlay && !p.overlay->empty);
        done({p.generation, PresentOutcome::Accepted, PresentStage::Rendered, {}});
    }
};

std::vector<std::byte> bytes(const char *text)
{
    std::vector<std::byte> out(std::strlen(text));
    std::memcpy(out.data(), text, out.size());
    return out;
}

// A box over the bottom of the frame from 1.0 s to 2.0 s.
constexpr char kBox[] =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 240\n\n[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n"
    "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an1\\pos(0,240)\\p1}m 0 0 l 320 0 320 40 0 40{\\p0}\n";

class VideoSessionMedia : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "video_session_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
};

TEST_F(VideoSessionMedia, SeeksAndStepsShowTheNamedFrames)
{
    backends::FfmsIndexedSource source(QStringLiteral(HIKARI_MEDIA_HELPER));
    backends::LibassRenderer renderer;
    RecordingPresenter presenter;
    VideoSession video(source, renderer);
    video.setPresenter(&presenter);
    // A box from 1.0 s to 2.0 s.
    video.setSubtitles(bytes(kBox));
    video.open(HIKARI_MEDIA_FIXTURES "/cfr.mkv");
    ASSERT_TRUE(waitFor([&] { return video.state() == VideoSession::State::Ready; }));
    ASSERT_TRUE(waitFor([&] { return presenter.shown.size() == 1; }));
    EXPECT_EQ(presenter.shown[0], 0);
    video.seekTo(core::DocumentTime(1'000'000)); // 24000/1001: frame 24 starts at 1.001 s
    ASSERT_TRUE(waitFor([&] { return presenter.shown.size() == 2; }));
    EXPECT_EQ(presenter.shown[1], 24);
    ASSERT_TRUE(video.step(1));
    ASSERT_TRUE(waitFor([&] { return presenter.shown.size() == 3; }));
    EXPECT_EQ(presenter.shown[2], 25);
    EXPECT_EQ(presenter.overlays, (std::vector<bool>{false, true, true}));
    video.showFrame(47);
    ASSERT_TRUE(waitFor([&] { return presenter.shown.size() == 4; }));
    EXPECT_EQ(presenter.shown[3], 47);
    EXPECT_FALSE(video.step(1));
}

TEST_F(VideoSessionMedia, AnUnreadableFileFails)
{
    backends::FfmsIndexedSource source(QStringLiteral(HIKARI_MEDIA_HELPER));
    backends::LibassRenderer renderer;
    VideoSession video(source, renderer);
    video.open(HIKARI_MEDIA_FIXTURES "/no-such-file.mkv");
    ASSERT_TRUE(waitFor([&] { return video.state() == VideoSession::State::Failed; }));
    EXPECT_TRUE(video.error().has_value());
}

} // namespace
