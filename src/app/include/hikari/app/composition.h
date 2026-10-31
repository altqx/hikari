#pragma once

class QObject;
class QString;

namespace hikari::app {

class Application;

enum class StartupMode {
    Run,
    ExitAfterWindowCreated,
};

int run(int argc, char **argv, StartupMode mode);

// Opens anything but a video before the main window exists. Returns true for a video, which
// openStartVideo opens once the window is up.
bool openStartFile(Application &application, const QString &path);
void openStartVideo(QObject *mainWindow, const QString &path);

}
