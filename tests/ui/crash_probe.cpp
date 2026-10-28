// AN1 (#404): a program that installs the crash handler and crashes, for
// analytics_tests: `hikari_crash_probe <record> <segv|abort>`.

#include "hikari/app/crash_record.h"

#include <QCoreApplication>

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace {

// Out of line, so the crash has a frame of this program's own.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void crashHere(volatile int *at)
{
    *at = 42;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
#ifdef _WIN32
    // No Windows Error Reporting window on a CI runner.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    const rlimit noCore{0, 0};
    setrlimit(RLIMIT_CORE, &noCore);
#endif
    QCoreApplication app(argc, argv);
    hikari::app::crash::install(QString::fromLocal8Bit(argv[1]), QCoreApplication::applicationDirPath(),
                                QStringLiteral("0.1.0-test.1"));
    if (std::strcmp(argv[2], "abort") == 0)
        std::abort();
    crashHere(static_cast<volatile int *>(std::getenv("HIKARI_CRASH_PROBE_NEVER_SET") ? &argc : nullptr));
    return 0;
}
