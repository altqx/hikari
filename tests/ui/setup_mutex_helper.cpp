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
