// P10 (#200): the status bar's fields against legacy HikariSubFrame and
// VideoBox at 20d647c4 (HikariSubFrame.cpp:150-155 sets up the nine fields):
// field fixtures without media (no Document, an ASS Document, an SRT one)
// and with media (an open video: scale, zoom, duration, FPS, resolution,
// aspect ratio, the file name), legacy's formats (SubsTime::raw(SRT),
// getfloat, "%d x %d", "x : y", the truncated percentages), the resolution
// warning's colour as legacy's SetSubsResolution and SetVideoResolution leave
// it, the tooltips, and the first field's progress text
// (HikariSubFrame::ProgressTitle / ProgressParcentProgress / ProgressEnd).

#include "shell_controller.h"
#include "status_bar_controller.h"

#include "hikari/application/workspace.h"

#include <QtTest>

using hikari::ui::ShellController;
using hikari::ui::StatusBarController;
using Inputs = StatusBarController::Inputs;
using F = StatusBarController::Field;

namespace {

int g_document = 0; // stands for a Document's identity

Inputs assDocument(int width, int height)
{
    Inputs in;
    in.document = true;
    in.documentKey = &g_document;
    in.ass = true;
    in.script = {width, height};
    return in;
}

// cfr.mkv as the shell sees it: 320x240, 24000/1001, its last frame at 1960 ms.
Inputs withVideo(Inputs in)
{
    in.video = true;
    in.clientWidth = 480;
    in.frameWidth = 320;
    in.frameHeight = 240;
    in.zoomPercent = 1.f;
    in.durationMs = 1960;
    in.fps = 24000.f / 1001.f;
    in.aspect = {4, 3};
    in.videoPath = QStringLiteral("/media/episode 01.mkv");
    return in;
}

QStringList texts(const StatusBarController::Fields &f)
{
    return QStringList(f.text.begin(), f.text.end());
}

} // namespace

class StatusBarTests : public QObject {
    Q_OBJECT

private slots:
    void withoutMediaOnlyTheSubtitlesResolutionShows()
    {
        // Nothing open: every field empty (OnPageChanged's empties).
        QCOMPARE(texts(StatusBarController::fieldsFor({})), QStringList(9, QString()));
        // An ASS Document: "%d x %d" of GetASSRes (SetSubsResolution).
        QStringList expected(9, QString());
        expected[F::SubtitlesResolution] = QStringLiteral("640 x 480");
        QCOMPARE(texts(StatusBarController::fieldsFor(assDocument(640, 480))), expected);
        // Another format: no subtitles resolution (subsFormat != ASS).
        Inputs srt = assDocument(640, 480);
        srt.ass = false;
        QCOMPARE(texts(StatusBarController::fieldsFor(srt)), QStringList(9, QString()));
    }

    void withMediaEveryVideoFieldShows()
    {
        const auto f = StatusBarController::fieldsFor(withVideo(assDocument(1280, 720)));
        QCOMPARE(f.text[F::HelpText], QString()); // the shell's statusText
        QCOMPARE(f.text[F::VideoScale], QStringLiteral("150%"));
        QCOMPARE(f.text[F::VideoZoom], QStringLiteral("100%"));
        QCOMPARE(f.text[F::VideoDuration], QStringLiteral("00:00:01,960"));
        QCOMPARE(f.text[F::FramesPerSecond], QStringLiteral("23.976 FPS"));
        QCOMPARE(f.text[F::VideoResolution], QStringLiteral("320 x 240"));
        QCOMPARE(f.text[F::AspectRatio], QStringLiteral("4 : 3"));
        QCOMPARE(f.text[F::SubtitlesResolution], QStringLiteral("1280 x 720"));
        QCOMPARE(f.text[F::VideoName], QStringLiteral("episode 01.mkv"));
        // A video without a Document (the rewrite can have none): no
        // subtitles resolution.
        const auto bare = StatusBarController::fieldsFor(withVideo({}));
        QCOMPARE(bare.text[F::SubtitlesResolution], QString());
        QCOMPARE(bare.text[F::VideoName], QStringLiteral("episode 01.mkv"));
    }

    void legacyFormats()
    {
        Inputs in = withVideo(assDocument(1280, 720));
        // (int)((w / (float)m_Width) * 100) and (int)(m_ZoomPercent * 100): truncated.
        in.clientWidth = 333;
        in.zoomPercent = 2.4999f;
        auto f = StatusBarController::fieldsFor(in);
        QCOMPARE(f.text[F::VideoScale], QStringLiteral("104%"));
        QCOMPARE(f.text[F::VideoZoom], QStringLiteral("249%"));
        // getfloat: "%5.3f" without trailing zeros or the point, nor the padding.
        in.fps = 25.f;
        QCOMPARE(StatusBarController::fieldsFor(in).text[F::FramesPerSecond], QStringLiteral("25 FPS"));
        in.fps = 29.97f;
        QCOMPARE(StatusBarController::fieldsFor(in).text[F::FramesPerSecond], QStringLiteral("29.97 FPS"));
        in.fps = 0.f;
        QCOMPARE(StatusBarController::fieldsFor(in).text[F::FramesPerSecond], QStringLiteral("0 FPS"));
        // SubsTime::raw(SRT) of GetDuration().
        in.durationMs = 5'025'678;
        QCOMPARE(StatusBarController::fieldsFor(in).text[F::VideoDuration], QStringLiteral("01:23:45,678"));
        // The video's view not open yet: no scale, zoom or resolution.
        in.frameWidth = 0;
        f = StatusBarController::fieldsFor(in);
        QCOMPARE(f.text[F::VideoScale], QString());
        QCOMPARE(f.text[F::VideoZoom], QString());
        QCOMPARE(f.text[F::VideoResolution], QString());
        QCOMPARE(f.text[F::VideoDuration], QStringLiteral("01:23:45,678"));
    }

    void theAspectRatioIsLegacysReducedPair()
    {
        using hikari::application::visual::sourceAspect;
        // ProviderFFMS2.cpp:364-380: common factors from 10 down to 2.
        QCOMPARE(sourceAspect({320, 240, 0, 1}), (hikari::application::visual::AspectPair{4, 3}));
        QCOMPARE(sourceAspect({1920, 1080, 1, 1}), (hikari::application::visual::AspectPair{16, 9}));
        // The SAR stretches the width, truncated: 320 * 32/27 = 379.26.
        QCOMPARE(sourceAspect({320, 240, 32, 27}), (hikari::application::visual::AspectPair{379, 240}));
        QCOMPARE(sourceAspect({720, 480, 8, 9}), (hikari::application::visual::AspectPair{4, 3}));
        QCOMPARE(sourceAspect({}), (hikari::application::visual::AspectPair{0, 0}));
    }

    void theResolutionWarningFollowsLegacysSetters()
    {
        const Inputs none;
        const Inputs ass = assDocument(1280, 720);
        const Inputs assVideo = withVideo(ass);
        // SetVideoResolution: an ASS Document whose resolution is not the video's.
        QVERIFY(!StatusBarController::nextMismatch(none, ass, false));
        QVERIFY(StatusBarController::nextMismatch(ass, assVideo, false));
        // Matching resolutions: no warning; a warning goes.
        const Inputs matching = withVideo(assDocument(320, 240));
        QVERIFY(!StatusBarController::nextMismatch(assDocument(320, 240), matching, true));
        // SetSubsResolution: Script properties change the resolution.
        QVERIFY(!StatusBarController::nextMismatch(assVideo, matching, true));
        QVERIFY(StatusBarController::nextMismatch(matching, assVideo, false));
        // Nothing legacy reacted to (an edit, a seek) keeps the colour.
        QVERIFY(StatusBarController::nextMismatch(assVideo, assVideo, true));
        QVERIFY(!StatusBarController::nextMismatch(assVideo, assVideo, false));
        // The view's size and zoom are not setters of the colour.
        Inputs zoomed = assVideo;
        zoomed.clientWidth = 900;
        zoomed.zoomPercent = 2.f;
        QVERIFY(StatusBarController::nextMismatch(assVideo, zoomed, true));

        // A non-ASS Document: SetVideoResolution compares GetASSRes's
        // resolution whatever the format (HikariSubFrame.cpp:1481-1484), so
        // opening a video warns (field 5; field 7 is empty) ...
        Inputs srt = ass;
        srt.ass = false;
        const Inputs srtVideo = withVideo(srt);
        QVERIFY(StatusBarController::nextMismatch(srt, srtVideo, false));
        // ... until the next SetSubsResolution: another Document shown ...
        Inputs other = srtVideo;
        int another = 0;
        other.documentKey = &another;
        QVERIFY(!StatusBarController::nextMismatch(srtVideo, other, true));
        // ... or the Document converted (to ASS: compared again).
        Inputs converted = srtVideo;
        converted.ass = true;
        QVERIFY(StatusBarController::nextMismatch(srtVideo, converted, true));
        Inputs convertedMatching = withVideo(assDocument(320, 240));
        Inputs srtMatchingVideo = convertedMatching;
        srtMatchingVideo.ass = false;
        QVERIFY(!StatusBarController::nextMismatch(srtMatchingVideo, convertedMatching, true));

        // Another Document shown (OnPageChanged's SetSubsResolution).
        Inputs secondAss = assVideo;
        secondAss.documentKey = &another;
        QVERIFY(StatusBarController::nextMismatch(srtVideo, secondAss, false));
        // The video closed (the rewrite's "Unload video", as a tab without
        // video): no warning.
        QVERIFY(!StatusBarController::nextMismatch(assVideo, ass, true));
        // Another video opened: compared again.
        Inputs nextVideo = matching;
        nextVideo.videoPath = QStringLiteral("/media/episode 02.mkv");
        QVERIFY(!StatusBarController::nextMismatch(assVideo, nextVideo, true));
        // No Document: nothing to warn about.
        QVERIFY(!StatusBarController::nextMismatch(none, withVideo({}), false));
    }

    void tooltipsAreLegacys()
    {
        // HikariSubFrame.cpp:153-154; the first field has none.
        QCOMPARE(StatusBarController::tooltips(),
                 (QStringList{QString(), QStringLiteral("Video scale"), QStringLiteral("Video zoom"),
                              QStringLiteral("Video duration"), QStringLiteral("Frames per second"),
                              QStringLiteral("Video resolution"), QStringLiteral("Video aspect ratio"),
                              QStringLiteral("Subtitles resolution"), QStringLiteral("Video file name")}));
    }

    void progressShowsInTheFirstField()
    {
        hikari::application::Workspace workspace;
        ShellController shell(workspace);
        qint64 now = 1000;
        shell.setProgressClock([&now] { return now; });
        QSignalSpy changed(&shell, &ShellController::statusTextChanged);
        // ProgressSetup + ProgressTitle: 0% at once.
        shell.startProgress(QStringLiteral("Loading external fonts"));
        QCOMPARE(shell.statusText(), QStringLiteral("Loading external fonts 0%. Time elapsed: 0:00:00.00"));
        // ProgressParcentProgress: the percentage and SubsTime::raw() (ASS) of the time since.
        now += 61'239;
        shell.setProgress(42);
        QCOMPARE(shell.statusText(), QStringLiteral("Loading external fonts 42%. Time elapsed: 0:01:01.23"));
        // ProgressEnd empties the field.
        shell.endProgress();
        QCOMPARE(shell.statusText(), QString());
        QCOMPARE(changed.count(), 3);
        // A start without a title shows nothing until a percentage.
        shell.setStatusText(QStringLiteral("Autosave"));
        shell.startProgress(QString());
        QCOMPARE(shell.statusText(), QStringLiteral("Autosave"));
        now += 5;
        shell.setProgress(7);
        QCOMPARE(shell.statusText(), QStringLiteral("Loading external fonts 7%. Time elapsed: 0:00:00.00"));
    }
};

QTEST_MAIN(StatusBarTests)
#include "status_bar_tests.moc"
