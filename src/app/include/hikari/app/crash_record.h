#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace hikari::app::crash {

constexpr int kMaxFrames = 32;

struct Frame {
    QString module;
    quint64 offset = 0;
};

struct Record {
    QString type;      // a signal name, an SEH code's name or 0x… code, or kFreezeType
    QString mechanism; // "signal", "seh" or "watchdog"
    QString version;
    QList<Frame> frames;
    int seconds = 0;   // a freeze only: how long the window has not answered
    bool freeze() const { return mechanism == QLatin1String("watchdog"); }
};

inline constexpr char kFreezeType[] = "APP_HANG";

// Writes recordPath when the process crashes; programFolder marks the program's own modules.
// A second call does nothing.
void install(const QString &recordPath, const QString &programFolder, const QString &version);
void uninstall();
bool installed();

// nullopt for anything that is not a valid record (a field outside its set, a module path).
std::optional<Record> read(const QString &path);
// Not for use inside a signal handler.
bool write(const QString &path, const Record &record);
// Names modules as the crash handler does (any other is "other"); needs install() first.
QList<Frame> framesOf(const quintptr *addresses, int count);

}
