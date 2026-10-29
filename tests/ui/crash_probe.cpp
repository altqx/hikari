#include "hikari/app/crash_record.h"
#include "hikari/app/hang_watchdog.h"

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QTimer>

#include <cstdio>

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace {

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void crashHere(volatile int *at)
{
    *at = 42;
}

volatile int g_after = 0;

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void freezeHere(unsigned long ms)
{
    QThread::msleep(ms);
    g_after = 1; // no tail call: this frame stays on the stack
}

}

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    const rlimit noCore{0, 0};
    setrlimit(RLIMIT_CORE, &noCore);
#endif
    QCoreApplication app(argc, argv);
    hikari::app::crash::install(QString::fromLocal8Bit(argv[1]), QCoreApplication::applicationDirPath(),
                                QStringLiteral("0.1.0-test.1"));
    if (std::strcmp(argv[2], "hang") == 0 || std::strcmp(argv[2], "hang-kill") == 0) {
        const QString record = QString::fromLocal8Bit(argv[1]);
        hikari::app::HangWatchdog watchdog({record, QStringLiteral("0.1.0-test.1"), 300, 50});
        if (std::strcmp(argv[2], "hang-kill") == 0) {
            QTimer::singleShot(100, [] { freezeHere(10 * 60 * 1000); });
            return app.exec();
        }
        QObject::connect(&watchdog, &hikari::app::HangWatchdog::recovered, &app,
                         [&](const hikari::app::crash::Record &r) {
                             QStringList modules;
                             for (const auto &frame : r.frames)
                                 modules << frame.module;
                             std::printf("recovered %d %s\n", r.seconds, qPrintable(modules.join(u',')));
                             std::fflush(stdout);
                             app.exit(QFile::exists(record) ? 3 : 0);
                         });
        QTimer::singleShot(100, [] { freezeHere(1500); });
        QTimer::singleShot(20000, [&] { app.exit(4); });
        return app.exec();
    }
    if (std::strcmp(argv[2], "abort") == 0)
        std::abort();
    crashHere(static_cast<volatile int *>(std::getenv("HIKARI_CRASH_PROBE_NEVER_SET") ? &argc : nullptr));
    return 0;
}
