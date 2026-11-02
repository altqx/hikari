#include "hikari/app/application.h"
#include "audio_controller.h"
#include "docking.h"
#include "video_controller.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QDeadlineTimer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <Spix/QtQmlBot.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr std::int64_t kTargetUs = 10'000'000;
constexpr int kRounds = 3;

struct Observed {
    bool armed = false;
    bool reopened = false;
    std::optional<int> target;
    std::optional<int> shown;
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
    explicit SeekThenPlay(hikari::app::Application &application, QString video)
        : m_application(application), m_video(std::move(video))
    {
    }
    std::vector<std::string> results;

protected:
    template <typename Pred> bool waitFor(Pred pred)
    {
        for (int i = 0; i < 200; ++i) {
            if (pred(observed()))
                return true;
            wait(100ms);
            getStringProperty("mainWindow/playPause", "text");
        }
        return false;
    }

    std::string round(const char *name)
    {
        {
            std::lock_guard lock(g_mutex);
            g_observed = {};
            g_observed.armed = true;
        }
        QMetaObject::invokeMethod(&m_application, [this] { m_application.openVideo(m_video); },
                                  Qt::BlockingQueuedConnection);
        if (!waitFor([](const Observed &o) { return o.target && o.shown == o.target; }))
        {
            const Observed o = observed();
            return std::string(name) + ": the seek to frame " + std::to_string(o.target.value_or(-1))
                   + " was not shown; shown " + std::to_string(o.shown.value_or(-1));
        }
        mouseClick("mainWindow/playPause");
        const bool played = waitFor([](const Observed &o) { return o.playing.size() >= 5; });
        mouseClick("mainWindow/playPause");
        const Observed o = observed();
        if (!played)
            return std::string(name) + ": no playback";
        std::printf("%s: sought frame %d, first player frames", name, *o.target);
        for (const int f : o.playing)
            std::printf(" %d", f);
        std::printf("\n");
        return std::abs(o.playing.front() - *o.target) <= 1
                   ? std::string(name) + ": ok"
                   : std::string(name) + ": started at frame " + std::to_string(o.playing.front());
    }

    void executeTest() override
    {
        for (int i = 0; i < kRounds; ++i)
            results.push_back(round("without audio"));
        QMetaObject::invokeMethod(&m_application, [this] { m_application.audio().openAudio(m_video); },
                                  Qt::BlockingQueuedConnection);
        for (int i = 0; i < 200; ++i) {
            bool loaded = false;
            QMetaObject::invokeMethod(&m_application, [&] { loaded = m_application.audio().loaded(); },
                                      Qt::BlockingQueuedConnection);
            if (loaded)
                break;
            wait(100ms);
        }
        for (int i = 0; i < kRounds; ++i)
            results.push_back(round("with audio"));
        for (const auto &e : getErrors())
            std::printf("spix error: %s\n", e.c_str());
        quit();
    }

private:
    hikari::app::Application &m_application;
    QString m_video;
};

constexpr char kSubtitles[] = "[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 64\r\nPlayResY: 48\r\n\r\n"
                              "[Events]\r\n"
                              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
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
        auto &session = controller.session();
        bool seek = false;
        {
            std::lock_guard lock(g_mutex);
            const bool ready = session.state() == hikari::application::VideoSession::State::Ready;
            g_observed.reopened = g_observed.reopened || !ready;
            if (g_observed.armed && g_observed.reopened && ready) {
                g_observed.armed = false;
                g_observed.target = session.frameAtOrAfter(hikari::core::DocumentTime(kTargetUs));
                seek = true;
            }
        }
        if (seek)
            session.seekTo(hikari::core::DocumentTime(kTargetUs));
        std::lock_guard lock(g_mutex);
        if (const auto frame = session.lastFrame()) {
            if (frame->index >= 0)
                g_observed.shown = frame->index;
            else if (session.playing() && g_observed.playing.size() < 5)
                if (const auto at = session.currentFrame();
                    at && (g_observed.playing.empty() || g_observed.playing.back() != *at))
                    g_observed.playing.push_back(*at);
        }
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
    SeekThenPlay test(application, video);
    spix::QtQmlBot bot;
    bot.runTestServer(test);
    app.exec();
    int failures = 0;
    for (const auto &r : test.results) {
        const bool ok = r.ends_with(": ok");
        std::printf("%s: %s\n", ok ? "ok" : "FAILED", r.c_str());
        failures += ok ? 0 : 1;
    }
    return test.results.size() == 2 * kRounds && failures == 0 ? 0 : 1;
}
