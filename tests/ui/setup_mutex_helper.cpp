// A stand-in for an update's setup in update_apply_tests: it waits <delay>
// ms as if starting up, then holds the mutex <name> for <hold> ms.
//   hikari_setup_mutex_helper <name> <delay> <hold>
// Plain Win32 with no Qt, so it starts at once even on a loaded machine;
// the delay is its own, not the machine's.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cwchar>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4)
        return 2;
    Sleep(DWORD(std::wcstoul(argv[2], nullptr, 10)));
    HANDLE held = CreateMutexW(nullptr, TRUE, argv[1]);
    if (!held)
        return 1;
    Sleep(DWORD(std::wcstoul(argv[3], nullptr, 10)));
    ReleaseMutex(held);
    CloseHandle(held);
    return 0;
}
