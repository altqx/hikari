#pragma once

class QObject;
class QString;

namespace hikari::app {

class Application;

enum class StartupMode {
    Run,                    // normal application lifetime
    ExitAfterWindowCreated, // tests: compose, create the main window, then quit
};

int run(int argc, char **argv, StartupMode mode);

// The path given on the command line (hikarisubApp.cpp:483-487, legacy
// OpenFiles with one file). openStartFile, before the main window is made,
// opens anything but a video as the editing target and returns false; for a
// video it returns true, and openStartVideo opens it once the window is up,
// as OpenFiles opens one video (V5: fullscreen with video.fullScreenOnStart).
bool openStartFile(Application &application, const QString &path);
void openStartVideo(QObject *mainWindow, const QString &path);

} // namespace hikari::app
