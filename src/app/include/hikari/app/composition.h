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

// On Linux, picks PulseAudio unless QT_AUDIO_BACKEND is set: when a playing stream's sink goes away,
// Qt 6.12's PipeWire backend keeps retrying the gone sink and plays silence, while PulseAudio moves the
// stream to the new default. Call before Qt starts.
void preferPulseAudioBackend();

// Opens anything but a video before the main window exists. Returns true for a video, which
// openStartVideo opens once the window is up.
bool openStartFile(Application &application, const QString &path);
void openStartVideo(QObject *mainWindow, const QString &path);

}
