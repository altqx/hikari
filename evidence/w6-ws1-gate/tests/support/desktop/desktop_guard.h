#pragma once

// 38e: the desktop every test of the application runs against. No test may
// open a real browser, file manager or editor, so:
//  - url_recorder.cpp, linked into every UI suite and every test executable
//    that builds the application, takes file, http and https URLs from
//    QDesktopServices::openUrl (and so from QML's Qt.openUrlExternally) with
//    QDesktopServices::setUrlHandler and records them;
//  - desktop_routes.cpp, linked where the application is (hikari_backends),
//    replaces backends::desktop's routes: a URL reaches openUrl only for a
//    scheme recorded above, a file to show in its folder and a program to
//    start detached are recorded and never reach the desktop;
//  - a URL of any other scheme is refused before the desktop's handler and
//    reported; the executable then exits with kUnhandledExitCode, so the
//    test run fails (desktop_handoff_tests.cpp checks that it does).
// Each executable starts with these in place (Q_COREAPP_STARTUP_FUNCTION);
// the guard's own test is the only one that replaces the reporter.

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace hikari::test::desktop {

// The schemes the recording handlers take.
QStringList recordedSchemes();

// The exit code of an executable a URL of another scheme was refused in.
constexpr int kUnhandledExitCode = 70;

struct Started {
    QString program;
    QStringList arguments;
    bool operator==(const Started &) const = default;
};

// What the tests would have handed the desktop, in order, since clear().
QList<QUrl> opened();
QStringList revealed();
QList<Started> started();
// URLs refused for their scheme (the run fails at exit when not empty).
QList<QUrl> refused();
void clear();

// What a detached start answers from now on (true: it started). clear()
// sets it back to true.
void setStartResult(bool started);

// Recording, for desktop_routes.cpp.
void recordRevealed(const QString &path);
bool recordStarted(const QString &program, const QStringList &arguments);
// A URL of a scheme no recording handler takes: refused and reported.
void refuse(const QUrl &url);
// Whether a recording handler took `url`'s scheme just now (the handlers are
// still installed).
bool recordingHandlerTook(const QUrl &url, qsizetype openedBefore);

// The guard's own test: what refuse() does after recording, in place of
// reporting; returns the one it replaces (null: the default report).
std::function<void(const QUrl &url)> replaceRefusalReport(std::function<void(const QUrl &url)> report);

} // namespace hikari::test::desktop
