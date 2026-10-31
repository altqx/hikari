#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

#include <optional>

namespace hikari::app::crash {

constexpr int kMaxFrames = 32;

struct Frame {
    QString module;
    quint64 offset = 0;
    // Named modules only: the load address and symbolic's debug id of the GNU build-id or PDB GUID and age.
    quint64 base = 0;
    QString debugId;
};

struct Record {
    QString type;      // a signal name, an SEH code's name or 0x… code, or kFreezeType
    QString mechanism; // "signal", "seh" or "watchdog"
    QString version;
    QList<Frame> frames;
    int seconds = 0;   // a freeze only: how long the window has not answered
    QList<Frame> renderFrames; // a freeze only: the window's render thread, when it has one
    bool freeze() const { return mechanism == QLatin1String("watchdog"); }
};

inline constexpr char kFreezeType[] = "APP_HANG";
// The type of a freeze event's second exception entry: the stack of the window's render thread.
inline constexpr char kFreezeRenderThreadType[] = "APP_HANG_RENDER_THREAD";

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
const QRegularExpression &debugIdPattern();

}
