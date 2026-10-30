#include "hikari/app/composition.h"

#ifdef __linux__
#include <climits>
#include <cstring>
#include <sys/auxv.h>
#include <unistd.h>

namespace {

// Qt 6.12.0 takes applicationDirPath() from AT_EXECFN, so a symlink hides qt.conf and our files; reverted upstream
// in https://code.qt.io/cgit/qt/qtbase.git/commit/?id=54d78dc8ccd7758ebeb200beb42eacab52b39b97
void reexecFromRealPath(char **argv)
{
    const auto *invoked = reinterpret_cast<const char *>(getauxval(AT_EXECFN));
    char real[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", real, sizeof real - 1);
    if (!invoked || *invoked != '/' || length <= 0)
        return;
    real[length] = '\0';
    if (std::strcmp(invoked, real) != 0)
        execv(real, argv);
}

}
#endif

int main(int argc, char **argv)
{
#ifdef __linux__
    reexecFromRealPath(argv);
#endif
    return hikari::app::run(argc, argv, hikari::app::StartupMode::Run);
}
