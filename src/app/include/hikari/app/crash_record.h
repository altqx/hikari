#pragma once

// AN1 (#404): a native crash leaves a small record that the next start sends
// as $exception (Analytics). A signal handler on Linux and an unhandled
// exception filter (SEH) on Windows write it: the signal or exception code,
// the version, and up to 32 frames as module and offset. A module is named
// only when it is the program's own (in its folder) or the system's (/usr,
// /lib; the Windows folder), by file name; any other is "other", so no path
// and no name of the user's own files is written.
// CR1 (#452): a freeze the hang watchdog saw is a record of the same form,
// type APP_HANG, mechanism "watchdog", with how long the window did not
// answer (`seconds`).

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
    QString type;      // SIGSEGV, …, or the SEH code's name (EXCEPTION_ACCESS_VIOLATION) or 0x… code, or APP_HANG
    QString mechanism; // "signal", "seh" or "watchdog"
    QString version;
    QList<Frame> frames;
    int seconds = 0;   // a freeze: how long the window did not answer (so far)
    bool freeze() const { return mechanism == QLatin1String("watchdog"); }
};

inline constexpr char kFreezeType[] = "APP_HANG";

// Installs the handlers, which write `recordPath` when the process crashes.
// `programFolder` names the program's own modules. Once per process; a
// second call changes nothing.
void install(const QString &recordPath, const QString &programFolder, const QString &version);
// Removes them (sending turned off).
void uninstall();
bool installed();

// The record at `path`, when it is one. Anything that does not read as one
// (a field outside its set, a module name that could be a path) is none.
std::optional<Record> read(const QString &path);
// Writes `record` to `path` in the form read() takes (outside a handler).
bool write(const QString &path, const Record &record);
// The frames of return addresses taken outside a handler (the hang
// watchdog's), named as the handler names them: the program's own and the
// system's modules by file name, any other "other". install() first.
QList<Frame> framesOf(const quintptr *addresses, int count);

} // namespace hikari::app::crash
