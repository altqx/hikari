// V2 playback follow-ups through the composition and the real QML (user
// decisions 2026-10-14, items 30-32 and 35-37):
// - the docked and the fullscreen seek bar dragged seek at most every 50 ms
//   and once more where they are let go (V2-seek-bar-drag-throttle);
// - while playing, the features that read the video's frame read the
//   player's (legacy FrameShownAt(VideoBox::Tell())): the Line editor's time
//   difference, inserting Lines at the video's time, the visual tools' time
//   and warning, Set FPS from video, splitting at the video time, Shift
//   times to the video, the audio mark from the video and a tab's video
//   position;
// - a player time past the last frame's start is the last frame;
// - while playing, the jumps (L, ;, Up, Down, the keyframes) go from the
//   player's frame and play on, next frame does nothing, and the seek bar
//   shows a seek or a release at once.
// A fake general player plays: each of its frames is put at a chosen time,
// so the frame the features must read is known.

#include "hikari/app/application.h"
#include "hikari/application/general_player.h"
#include "audio_controller.h"
#include "docking.h"
#include "line_editor_controller.h"
#include "shift_times_controller.h"
#include "video_controller.h"
#include "video_fullscreen_controller.h"
#include "visual_tools_controller.h"

#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

// Opens and seeks when told to, as the session test's player does.
struct FakePlayer : application::GeneralPlayerPort {
    std::vector<std::string> calls;
    Opened pendingOpen;
    Seeked pendingSeek;
    std::int64_t soughtUs = -1;
    void open(const std::string &path, Opened done) override
    {
        calls.push_back("open " + path);
        pendingOpen = std::move(done);
    }
    void seek(std::int64_t us, Seeked done) override
    {
        calls.push_back("seek " + std::to_string(us));
        soughtUs = us;
        pendingSeek = std::move(done);
    }
    void play() override { calls.push_back("play"); }
    void pause() override { calls.push_back("pause"); }
    void stop() override { calls.push_back("stop"); }
    bool selectAudioTrack(int) override { return true; }
    bool selectSubtitleTrack(int) override { return true; }
    application::PlaybackState playbackState() const override { return application::PlaybackState::Stopped; }
    application::MediaStatus mediaStatus() const override { return application::MediaStatus::Loaded; }
    double bufferProgress() const override { return 1; }
    application::PlayerClock clock() const override { return {}; }
    application::MediaDescription description() const override { return {}; }
    std::uint64_t generation() const override { return 1; }
    void opened()
    {
        if (auto done = std::exchange(pendingOpen, {}))
            done(application::MediaDescription{});
    }
    void delivered()
    {
        if (auto done = std::exchange(pendingSeek, {}))
            done(application::SeekResult{1, soughtUs, soughtUs, {}});
    }
    int seeks() const
    {
        return static_cast<int>(std::count_if(calls.begin(), calls.end(),
                                              [](const std::string &c) { return c.starts_with("seek"); }));
    }
};

application::IndexedFrame playerFrame()
{
    application::IndexedFrame f;
    f.index = -1;
    f.width = 4;
    f.height = 2;
    f.stride = 16;
    f.bgra.assign(32, std::byte{0x80});
    return f;
}

QQuickItem *findItem(QQuickItem *from, const QString &name)
{
    if (from->objectName() == name)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *found = findItem(child, name))
            return found;
    return nullptr;
}

} // namespace

class PlayingFrameTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    FakePlayer *player = nullptr;

    application::VideoSession &session() { return application->video().session(); }
    application::EditSession *edit()
    {
        const auto target = application->workspace().editingTarget();
        return target ? application->files().session(*target) : nullptr;
    }
    std::vector<const core::LineRecord *> lines() { return edit()->document().lines(); }
    static std::int64_t ms(core::DocumentTime t) { return t.microseconds() / 1000; }
    std::int64_t startMs(int frame) { return ms(*session().frameStart(frame)); }

    // Two Lines (0-1 s, 1-2 s) beside the 48-frame 24 fps fixture, open.
    bool open()
    {
        const QString video = dir.filePath(QStringLiteral("clip.mkv"));
        if (!QFile::exists(video) && !QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), video))
            return false;
        const QString subtitles = dir.filePath(QStringLiteral("clip.ass"));
        QFile f(subtitles);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 240\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n");
        f.close();
        if (!application->openFile(subtitles))
            return false;
        application->video().openVideo(video);
        return QTest::qWaitFor([&] { return session().shownFrame().value_or(-1) >= 0; }, 20000);
    }
    // Plays through the fake player from the shown frame.
    bool play()
    {
        session().setGeneralPlayer(player);
        if (!application->video().play())
            return false;
        player->opened();
        player->delivered();
        return session().playing();
    }
    // The player's frame at `frame`'s start plus 1 ms; the requested frame
    // stays where playback started.
    void playerAt(int frame) { session().generalFrame(playerFrame(), (startMs(frame) + 1) * 1000); }
    void undo()
    {
        QVERIFY(edit()->undo());
        application->editor().reloadFromSession();
    }
    void click(qulonglong id, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        application->clickLine(id, static_cast<int>(modifiers.toInt()));
    }

    // Drags `bar` from `from` to `to` (fractions of its width) in `steps`
    // moves `pauseMs` apart; returns the moves the bar reported, and in
    // `held` its frame just before it was let go (where it is let go: the
    // release is at the last move's point).
    int drag(QQuickWindow *w, QQuickItem *bar, qreal from, qreal to, int steps, int pauseMs, int &held)
    {
        QSignalSpy moved(bar, SIGNAL(moved()));
        const qreal y = bar->height() / 2;
        auto at = [&](qreal f) { return bar->mapToScene(QPointF(bar->width() * f, y)).toPoint(); };
        QTest::mouseMove(w, at(from));
        QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, at(from));
        for (int i = 1; i <= steps; ++i) {
            QTest::mouseMove(w, at(from + (to - from) * i / steps));
            QTest::qWait(pauseMs);
        }
        held = qRound(bar->property("value").toReal());
        QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, at(to));
        return static_cast<int>(moved.count());
    }
    // A fast drag over the bar: many moves, at most a seek per 50 ms, and
    // the last seek exactly where it was let go.
    void checkDrag(QQuickWindow *w, QQuickItem *bar, qreal from, qreal to)
    {
        auto &video = application->video();
        QSignalSpy sought(&video, &ui::VideoController::seekBarSought);
        QElapsedTimer clock;
        clock.start();
        int released = -1;
        const int moves = drag(w, bar, from, to, 60, 4, released);
        const qint64 elapsed = clock.elapsed();
        qInfo("%d moves, %lld seeks in %lld ms", moves, static_cast<long long>(sought.count()),
              static_cast<long long>(elapsed));
        QVERIFY2(moves >= 30, "the bar followed the pointer");
        QVERIFY(sought.count() >= 2); // during the drag, not only on release
        QVERIFY(sought.count() <= elapsed / 50 + 3);
        QVERIFY(sought.count() < moves / 3);
        QCOMPARE(sought.last().first().toInt(), released);
        QCOMPARE(session().requestedFrame().value_or(-1), released);
        QVERIFY(std::abs(released - qRound(to * (session().frameCount() - 1))) <= 4);
        // Nothing waits once let go, and the bar follows the frame again.
        const auto count = sought.count();
        QTest::qWait(120);
        QCOMPARE(sought.count(), count);
        QTRY_COMPARE(qRound(bar->property("value").toReal()), released);
    }

private slots:
    void init()
    {
        QVERIFY(dir.isValid());
        app::Application::Options options;
        options.playbackAudio = false; // never a desktop's speakers in tests
        application = new app::Application(options);
        player = new FakePlayer;
        engine = new QQmlApplicationEngine;
        ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(open());
    }
    void cleanup()
    {
        if (application)
            session().setGeneralPlayer(&application->generalPlayer());
        delete engine;
        delete application;
        delete player;
        engine = nullptr;
        application = nullptr;
        player = nullptr;
    }

    // Item 30: the docked seek bar, paused and playing.
    void seekBarDragSeeksAtMostEvery50Ms()
    {
        auto *bar = findItem(window->contentItem(), QStringLiteral("videoSlider"));
        QVERIFY(bar);
        QTRY_VERIFY(bar->isVisible() && bar->isEnabled());
        QVERIFY(bar->width() >= 120);
        checkDrag(window, bar, 0.1, 0.8);
        // The frame let go on is the one shown.
        const int released = session().requestedFrame().value_or(-1);
        QTRY_COMPARE_WITH_TIMEOUT(session().shownFrame().value_or(-1), released, 10000);
        // Playing: the player is sought as often, and goes on from the frame
        // let go on.
        QVERIFY(play());
        playerAt(5);
        const int before = player->seeks();
        checkDrag(window, bar, 0.9, 0.2);
        QVERIFY(session().playing());
        QVERIFY(player->seeks() - before <= 22);
        QCOMPARE(player->calls.back(), "seek " + std::to_string(startMs(*session().requestedFrame()) * 1000));
    }

    // Item 30: the fullscreen seek bar the same way.
    void fullscreenSeekBarDragSeeksAtMostEvery50Ms()
    {
        auto &fs = application->videoFullscreen();
        fs.setShowToolbar(true);
        QVERIFY(fs.toggle(0));
        auto *fsWindow = qobject_cast<QQuickWindow *>(
            engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("videoFullscreen")));
        QVERIFY(fsWindow);
        QTRY_VERIFY(fsWindow->isExposed());
        QTest::mouseMove(fsWindow, QPoint(fsWindow->width() / 2, fsWindow->height() - 3));
        auto *bar = findItem(fsWindow->contentItem(), QStringLiteral("fullscreenSlider"));
        QVERIFY(bar);
        QTRY_VERIFY(bar->isVisible() && bar->isEnabled());
        checkDrag(fsWindow, bar, 0.15, 0.75);
        QVERIFY(fs.leave());
    }

    // Items 35-37 (user decisions 2026-10-14): while playing, a jump goes
    // from the player's frame and plays on, and the seek bar shows it at
    // once, as it shows the frame a drag lets go on at once; next frame does
    // nothing. Before, the bar kept the last texts' frame for up to 100 ms
    // (a jump from where playback started was not shown at all).
    void theBarShowsASeekWhilePlayingAtOnce()
    {
        auto &video = application->video();
        auto *bar = findItem(window->contentItem(), QStringLiteral("videoSlider"));
        QVERIFY(bar);
        auto value = [&] { return qRound(bar->property("value").toReal()); };
        QVERIFY(video.showFrameAt(2));
        QTRY_COMPARE(session().shownFrame().value_or(-1), 2);
        QVERIFY(play());
        playerAt(10);
        QTRY_COMPARE(value(), 10);
        // L: 5 s on from frame 10 is past the end (2 s): the last frame.
        const int last = session().frameCount() - 1;
        QVERIFY(video.seekBy(5000));
        QVERIFY(video.playing());
        QCOMPARE(session().requestedFrame(), std::optional(last));
        QCOMPARE(player->calls.back(), "seek " + std::to_string(startMs(last) * 1000));
        QCOMPARE(value(), last); // at once
        player->delivered();
        playerAt(last);
        // ; from there: 5 s back is the first frame.
        QVERIFY(video.seekBy(-5000));
        QCOMPARE(player->calls.back(), std::string("seek 0"));
        QCOMPARE(value(), 0);
        player->delivered();
        playerAt(20);
        QTRY_COMPARE(value(), 20);
        // Next frame does nothing while playing.
        const auto calls = player->calls.size();
        QVERIFY(!video.stepFrames(1));
        QCOMPARE(session().requestedFrame(), std::optional(0));
        QCOMPARE(player->calls.size(), calls);
        // A drag let go: the bar shows the frame let go on at once.
        int released = -1;
        drag(window, bar, 0.85, 0.35, 30, 4, released);
        QCOMPARE(session().requestedFrame(), std::optional(released));
        QCOMPARE(value(), released);
        QVERIFY(session().playing());
    }

    // Item 31: while playing, every reader of the video's frame reads the
    // player's frame (30 here, playback having started from 2), where they
    // read -1 (none) before.
    void featuresReadThePlayersFrameWhilePlaying()
    {
        auto &video = application->video();
        const auto timebase = session().legacyTimebase();
        const auto first = lines()[0]->id, second = lines()[1]->id;
        click(second.value);
        QVERIFY(video.showFrameAt(2));
        QTRY_COMPARE(session().shownFrame().value_or(-1), 2);
        QVERIFY(play());
        // The visual tools: their time, and the warning outside the active
        // Line (1-2 s) at frame 10, none inside it at frame 30. (Within one
        // play, a player frame older than the one shown is dropped.)
        auto &tools = application->visualTools();
        tools.selectFamily(1); // Position
        playerAt(10);
        QCOMPARE(session().shownFrame(), std::optional(-1)); // a player frame is shown
        QCOMPARE(session().reportedFrame(), std::optional(10));
        QCOMPARE(tools.videoTimeMs(), startMs(10));
        QVERIFY(!tools.warning().isEmpty());
        playerAt(30);
        QCOMPARE(session().reportedFrame(), std::optional(30));
        QVERIFY(session().requestedFrame() != std::optional(30));
        const std::int64_t at = startMs(30);
        QCOMPARE(tools.videoTimeMs(), at);
        QVERIFY(tools.warning().isEmpty());

        // The Line editor's time from video (EDITBOX_START_DIFFERENCE).
        auto &editor = application->editor();
        QVERIFY(editor.insertTimeDifference(false, 0, 0));
        QCOMPARE(editor.text(), QStringLiteral("%1second").arg(at - 1000));
        editor.discard();
        QCOMPARE(editor.text(), QStringLiteral("second"));

        // The audio mark from the video (GLOBAL_SET_AUDIO_MARK_FROM_VIDEO).
        auto &audio = application->audio();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audio.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        playerAt(30);
        application->setAudioFromVideo(true);
        QVERIFY(audio.hasMark());
        QCOMPARE(audio.markMs(), timebase.msAt(30));

        // Shift times to the video (the active Line's start to frame 30's
        // start time).
        auto map = application->shiftTimesSettings().settingsMap();
        map[QStringLiteral("moveToVideoTime")] = true;
        map[QStringLiteral("fromStartTime")] = true;
        map[QStringLiteral("byFrames")] = false;
        map[QStringLiteral("timeMs")] = 0;
        map[QStringLiteral("whichLines")] = 1;
        map[QStringLiteral("whichTimes")] = 0;
        application->shiftTimesSettings().setSettingsMap(map);
        playerAt(30);
        QCOMPARE(application->shiftTimes(), QString());
        const int added = (timebase.startTimeFor(30) - 1000) / 10 * 10;
        QVERIFY(added > 0);
        QCOMPARE(ms(lines()[1]->start.value), 1000 + added);
        undo();

        // Set FPS from video: the second selected Line's start moves to the
        // video's time, the first stays.
        click(first.value);
        click(second.value, Qt::ShiftModifier);
        playerAt(30);
        QVERIFY(application->setFpsFromVideo());
        QCOMPARE(ms(lines()[0]->start.value), 0);
        QCOMPARE(ms(lines()[1]->start.value), at);
        undo();

        // Split at the video time.
        click(second.value);
        playerAt(30);
        const auto count = lines().size();
        QVERIFY(application->splitLines(QStringLiteral("videoTime")));
        QCOMPARE(lines().size(), count + 1);
        QCOMPARE(ms(lines()[1]->end.value), timebase.endTimeFor(timebase.frameAt(static_cast<int>(at))) / 10 * 10);
        undo();

        // Insert Lines timed to the video: "frame" (frame 30 to frame 31)
        // and "video" (frame 30, 4 s).
        click(second.value);
        playerAt(30);
        QVERIFY(application->insertLine(false, QStringLiteral("frame")));
        QCOMPARE(lines().size(), count + 1);
        QCOMPARE(ms(lines()[2]->start.value), at / 10 * 10); // ZEROIT
        QCOMPARE(ms(lines()[2]->end.value), startMs(31) / 10 * 10);
        playerAt(30);
        QVERIFY(application->insertLine(false, QStringLiteral("video")));
        bool found = false;
        for (const auto *l : lines())
            found = found || (ms(l->start.value) == at / 10 * 10 && ms(l->end.value) == (at + 4000) / 10 * 10);
        QVERIFY(found);

        // The tab's video position: a new tab, then back, shows frame 30.
        playerAt(30);
        application->addPage();
        QVERIFY(!video.hasVideo());
        application->changeTab(-1);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(session().shownFrame().value_or(-1), 30, 10000);
    }

    // Item 32: a player time past the last frame's start is the last frame:
    // the panel's status, the times field and the bar show it, and Pause
    // shows it; before, they fell back to the frame playback started from.
    void thePanelReportsTheLastFrameAtTheEnd()
    {
        auto &video = application->video();
        QVERIFY(video.showFrameAt(3));
        QTRY_COMPARE(session().shownFrame().value_or(-1), 3);
        QVERIFY(play());
        const int last = session().frameCount() - 1;
        session().generalFrame(playerFrame(), (startMs(last) + 500) * 1000);
        QCOMPARE(video.frame(), last);
        QCOMPARE(session().reportedFrame(), std::optional(last));
        auto *bar = findItem(window->contentItem(), QStringLiteral("videoSlider"));
        QTRY_COMPARE(qRound(bar->property("value").toReal()), last);
        QVERIFY(video.status().startsWith(QStringLiteral("Frame %1 of %2").arg(last).arg(last + 1)));
        QVERIFY(video.times().contains(QStringLiteral(";  %1;  ").arg(last)));
        QVERIFY(video.pause());
        QTRY_COMPARE_WITH_TIMEOUT(session().shownFrame().value_or(-1), last, 10000);
        QCOMPARE(video.frame(), last);
    }
};

QTEST_MAIN(PlayingFrameTests)
#include "playing_frame_tests.moc"
