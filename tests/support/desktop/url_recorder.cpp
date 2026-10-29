#include "desktop_guard.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace hikari::test::desktop {

namespace {

struct State {
    QMutex mutex;
    QList<QUrl> opened;
    QStringList revealed;
    QList<Started> started;
    QList<QUrl> refused;
    QList<QUrl> everRefused;
    bool startResult = true;
    std::function<void(const QUrl &)> report;
};

State &state()
{
    static State s;
    return s;
}

class Recorder : public QObject {
    Q_OBJECT
public slots:
    void open(const QUrl &url)
    {
        QMutexLocker lock(&state().mutex);
        state().opened.append(url);
    }
};

Recorder *recorder = nullptr;

void failTheRunOnARefusal()
{
    for (const QString &scheme : recordedSchemes())
        QDesktopServices::unsetUrlHandler(scheme);
    delete std::exchange(recorder, nullptr);
    QList<QUrl> refused;
    {
        QMutexLocker lock(&state().mutex);
        refused = state().everRefused;
    }
    if (refused.isEmpty())
        return;
    for (const QUrl &url : refused)
        std::fprintf(stderr, "FAIL: desktop guard: the test tried to open %s, a scheme no test handler records\n",
                     url.toString().toLocal8Bit().constData());
    std::fflush(stderr);
    std::fflush(stdout);
    std::_Exit(kUnhandledExitCode);
}

void installTheRecordingHandlers()
{
    recorder = new Recorder;
    for (const QString &scheme : recordedSchemes())
        QDesktopServices::setUrlHandler(scheme, recorder, "open");
    qAddPostRoutine(failTheRunOnARefusal);
}

}

QStringList recordedSchemes()
{
    return {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")};
}

QList<QUrl> opened()
{
    QMutexLocker lock(&state().mutex);
    return state().opened;
}

QStringList revealed()
{
    QMutexLocker lock(&state().mutex);
    return state().revealed;
}

QList<Started> started()
{
    QMutexLocker lock(&state().mutex);
    return state().started;
}

QList<QUrl> refused()
{
    QMutexLocker lock(&state().mutex);
    return state().refused;
}

void clear()
{
    QMutexLocker lock(&state().mutex);
    state().opened.clear();
    state().revealed.clear();
    state().started.clear();
    state().refused.clear();
    state().startResult = true;
}

void setStartResult(bool started)
{
    QMutexLocker lock(&state().mutex);
    state().startResult = started;
}

void recordRevealed(const QString &path)
{
    QMutexLocker lock(&state().mutex);
    state().revealed.append(path);
}

bool recordStarted(const QString &program, const QStringList &arguments)
{
    QMutexLocker lock(&state().mutex);
    state().started.append({program, arguments});
    return state().startResult;
}

void refuse(const QUrl &url)
{
    std::function<void(const QUrl &)> report;
    {
        QMutexLocker lock(&state().mutex);
        state().refused.append(url);
        report = state().report;
        if (!report)
            state().everRefused.append(url);
    }
    if (report)
        report(url);
    else
        qCritical("desktop guard: %s was not opened: no test handler records its scheme, so it would have reached "
                  "the desktop; the run fails at exit",
                  qPrintable(url.toString()));
}

bool recordingHandlerTook(const QUrl &url, qsizetype openedBefore)
{
    QMutexLocker lock(&state().mutex);
    return state().opened.size() > openedBefore && state().opened.last() == url;
}

std::function<void(const QUrl &url)> replaceRefusalReport(std::function<void(const QUrl &url)> report)
{
    QMutexLocker lock(&state().mutex);
    return std::exchange(state().report, std::move(report));
}

}

namespace {

void installTheDesktopRecorder()
{
    hikari::test::desktop::installTheRecordingHandlers();
}

}

Q_COREAPP_STARTUP_FUNCTION(installTheDesktopRecorder)

#include "url_recorder.moc"
