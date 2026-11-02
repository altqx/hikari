#include "hikari/app/application.h"
#include "audio_controller.h"
#include "docking.h"
#include "video_controller.h"

#include <QFile>
#include <QGuiApplication>
#include <QDeadlineTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <Spix/QtQmlBot.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr std::int64_t kTargetUs = 10'000'000;
constexpr int kRepeats = 2;

enum class Source { Direct, Grid, AudioView };

const char *nameOf(Source source)
{
    switch (source) {
    case Source::Direct: return "a direct seek";
    case Source::Grid: return "a Grid double click";
    case Source::AudioView: return "an audio view middle click";
    }
    return "";
}

struct Observed {
    bool reopened = false;
    bool ready = false;
    bool recording = false;
    std::vector<int> playing;
};

std::mutex g_mutex;
Observed g_observed;

Observed observed()
{
    std::lock_guard lock(g_mutex);
    return g_observed;
}

class SeekThenPlay : public spix::TestServer {
public:
    SeekThenPlay(hikari::app::Application &application, QQuickWindow &window, QString video)
        : m_application(application), m_window(window), m_video(std::move(video))
    {
    }
    std::vector<std::string> results;

protected:
    template <typename Pred> bool waitFor(Pred pred)
    {
        for (int i = 0; i < 200; ++i) {
            if (pred(observed()))
                return true;
            std::this_thread::sleep_for(100ms);
        }
        return false;
    }

    void clickPlayPause()
    {
        mouseClick("mainWindow/playPause");
        getStringProperty("mainWindow/playPause", "text");
    }

    template <typename F> void onGui(F f)
    {
        QMetaObject::invokeMethod(&m_application, std::move(f), Qt::BlockingQueuedConnection);
    }

    QQuickItem *item(const char *name) const
    {
        if (auto *found = m_window.findChild<QQuickItem *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *found = w->findChild<QQuickItem *>(QLatin1String(name)))
                return found;
        return nullptr;
    }

    QPoint gridRow(int row)
    {
        auto *grid = item("editingGrid");
        for (int y = 0; grid && y < int(grid->height()); ++y) {
            int r = -1;
            QMetaObject::invokeMethod(grid, "rowAt", Q_RETURN_ARG(int, r), Q_ARG(qreal, qreal(y)));
            if (r == row)
                return grid->mapToScene(QPointF(grid->width() / 2, y + 3)).toPoint();
        }
        return {-1, -1};
    }

    int shownFrame()
    {
        int index = -1;
        onGui([&] {
            const auto &session = m_application.video().session();
            if (const auto frame = session.lastFrame(); frame && !session.playing())
                index = frame->index;
        });
        return index;
    }

    bool shows(int frame)
    {
        for (int i = 0; i < 200; ++i) {
            if (shownFrame() == frame)
                return true;
            std::this_thread::sleep_for(100ms);
        }
        return false;
    }

    bool audioViewShows(std::int64_t ms)
    {
        for (int i = 0; i < 200; ++i) {
            bool shows = false;
            onGui([&] {
                auto *display = item("audioDisplay");
                const float x = m_application.audio().view().xAtMs(ms);
                shows = m_application.audio().ready() && display && display->isVisible()
                        && m_application.audio().view().xAtMs(ms + 1000) > x && x >= 0 && x < display->width();
            });
            if (shows)
                return true;
            std::this_thread::sleep_for(100ms);
        }
        return false;
    }

    std::optional<int> seek(Source source)
    {
        std::optional<int> target;
        onGui([&] {
            auto &session = m_application.video().session();
            switch (source) {
            case Source::Direct:
                session.seekTo(hikari::core::DocumentTime(kTargetUs));
                target = session.frameAtOrAfter(hikari::core::DocumentTime(kTargetUs));
                break;
            case Source::Grid:
                QTest::mouseDClick(item("editingGrid")->window(), Qt::LeftButton, Qt::NoModifier, gridRow(1));
                target = session.frameAtOrAfter(hikari::core::DocumentTime(kTargetUs));
                break;
            case Source::AudioView: {
                auto *display = item("audioDisplay");
                const auto &view = m_application.audio().view();
                const int x = static_cast<int>(view.xAtMs(kTargetUs / 1000));
                if (!display || x < 0 || x >= display->width())
                    return;
                QTest::mouseClick(display->window(), Qt::MiddleButton, Qt::NoModifier,
                                  display->mapToScene(QPointF(x, display->height() / 2)).toPoint());
                target = session.frameAtOrAfter(hikari::core::DocumentTime(std::int64_t{view.msAtX(x)} * 1000));
                break;
            }
            }
        });
        return target;
    }

    std::string round(Source source, bool audio)
    {
        const std::string name = std::string(nameOf(source)) + (audio ? ", with audio" : ", without audio");
        {
            std::lock_guard lock(g_mutex);
            g_observed = {};
        }
        if (source == Source::AudioView)
            onGui([this] {
                QTest::mouseClick(item("editingGrid")->window(), Qt::LeftButton, Qt::NoModifier, gridRow(1));
            });
        onGui([this] { m_application.openVideo(m_video); });
        if (!waitFor([](const Observed &o) { return o.ready; }))
            return name + ": the video did not load";
        if (source == Source::AudioView && !audioViewShows(kTargetUs / 1000))
            return name + ": the audio view does not show 10 s";
        const auto target = seek(source);
        if (!target || std::abs(*target - 240) > 1)
            return name + ": the seek could not be made";
        if (!shows(*target)) {
            std::string why;
            onGui([&] {
                const auto &session = m_application.video().session();
                const auto frame = session.lastFrame();
                why = "; last " + (frame ? std::to_string(frame->index) : std::string("none")) + ", playing "
                      + std::to_string(session.playing()) + ", requested "
                      + std::to_string(session.requestedFrame().value_or(-1));
            });
            return name + ": the seek to frame " + std::to_string(*target) + " was not shown" + why;
        }
        {
            std::lock_guard lock(g_mutex);
            g_observed.recording = true;
        }
        clickPlayPause();
        const bool played = waitFor([](const Observed &o) { return o.playing.size() >= 5; });
        clickPlayPause();
        const Observed o = observed();
        if (!played)
            return name + ": no playback";
        std::printf("%s: sought frame %d, first player frames", name.c_str(), *target);
        for (const int f : o.playing)
            std::printf(" %d", f);
        std::printf("\n");
        return std::abs(o.playing.front() - *target) <= 1
                   ? name + ": ok"
                   : name + ": started at frame " + std::to_string(o.playing.front());
    }

    void executeTest() override
    {
        for (const Source source : {Source::Direct, Source::Grid})
            for (int i = 0; i < kRepeats; ++i)
                results.push_back(round(source, false));
        onGui([this] { m_application.audio().openAudio(m_video); });
        for (int i = 0; i < 200; ++i) {
            bool loaded = false;
            onGui([&] { loaded = m_application.audio().ready(); });
            if (loaded)
                break;
            std::this_thread::sleep_for(100ms);
        }
        for (const Source source : {Source::Direct, Source::Grid, Source::AudioView})
            for (int i = 0; i < kRepeats; ++i)
                results.push_back(round(source, true));
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        quit();
    }

private:
    hikari::app::Application &m_application;
    QQuickWindow &m_window;
    QString m_video;
};

constexpr char kSubtitles[] = "[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 64\r\nPlayResY: 48\r\n\r\n"
                              "[Events]\r\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                              "Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,first\r\n"
                              "Dialogue: 0,0:00:10.00,0:00:12.00,Default,,0,0,0,,sign\r\n";

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString subtitles = dir.filePath(QStringLiteral("ep1.ass"));
    const QString video = dir.filePath(QStringLiteral("ep1.mkv"));
    QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/longaudio.mkv"), video);
    {
        QFile f(subtitles);
        if (!f.open(QIODevice::WriteOnly) || f.write(kSubtitles) <= 0)
            return 2;
    }
    hikari::app::Application::Options options;
    options.mediaHelper = QStringLiteral(HIKARI_MEDIA_HELPER);
    options.playbackAudio = false;
    hikari::app::Application application(options);
    auto &controller = application.video();
    QObject::connect(&controller, &hikari::ui::VideoController::changed, [&controller] {
        const auto &session = controller.session();
        const bool ready = session.state() == hikari::application::VideoSession::State::Ready;
        std::lock_guard lock(g_mutex);
        g_observed.reopened = g_observed.reopened || !ready;
        g_observed.ready = g_observed.reopened && ready;
        const auto frame = session.lastFrame();
        if (!frame)
            return;
        if (frame->index < 0 && g_observed.recording && session.playing() && g_observed.playing.size() < 5)
            if (const auto at = session.currentFrame();
                at && (g_observed.playing.empty() || g_observed.playing.back() != *at))
                g_observed.playing.push_back(*at);
    });
    if (!application.openFile(subtitles))
        return 2;
    QQmlApplicationEngine engine;
    hikari::ui::attachDocking(engine);
    engine.setInitialProperties(application.qmlProperties());
    engine.loadFromModule("Hikari.Ui", "Main");
    if (engine.rootObjects().isEmpty())
        return 2;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->requestActivate();
    QDeadlineTimer deadline(10'000);
    while (!(window->isExposed() && window->isActive()) && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    SeekThenPlay test(application, *window, video);
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();
    int failures = 0;
    for (const auto &r : test.results) {
        const bool ok = r.ends_with(": ok");
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", r.c_str());
        failures += ok ? 0 : 1;
    }
    return test.results.size() == 5 * kRepeats && failures == 0 ? 0 : 1;
}
