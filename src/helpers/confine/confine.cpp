// hikarisub-confine <command line>: runs the command line with a restricted Low-integrity copy of this process's token,
// in this process's job, on its desktop, with only its three standard handles, and exits with the command's exit code.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>

namespace {

constexpr int kRefused = 0x4846;

int refuse(const char *step)
{
    std::fprintf(stderr, "hikarisub-confine: %s failed (%lu)\n", step, GetLastError());
    return kRefused;
}

std::wstring commandAfterOurOwn()
{
    const wchar_t *line = GetCommandLineW();
    if (*line == L'"') {
        for (++line; *line && *line != L'"'; ++line) {
        }
        if (*line)
            ++line;
    } else {
        while (*line && *line != L' ' && *line != L'\t')
            ++line;
    }
    while (*line == L' ' || *line == L'\t')
        ++line;
    return line;
}

std::vector<BYTE> wellKnownSid(WELL_KNOWN_SID_TYPE type)
{
    std::vector<BYTE> sid(SECURITY_MAX_SID_SIZE);
    DWORD size = DWORD(sid.size());
    if (!CreateWellKnownSid(type, nullptr, sid.data(), &size))
        return {};
    sid.resize(size);
    return sid;
}

HANDLE lowRightsToken()
{
    HANDLE self = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
                          &self))
        return nullptr;
    std::vector<std::vector<BYTE>> groups;
    for (WELL_KNOWN_SID_TYPE type : {WinBuiltinAdministratorsSid, WinBuiltinPowerUsersSid, WinBuiltinAccountOperatorsSid,
                                     WinBuiltinSystemOperatorsSid, WinBuiltinBackupOperatorsSid, WinBuiltinPrintOperatorsSid,
                                     WinBuiltinNetworkConfigurationOperatorsSid})
        if (std::vector<BYTE> sid = wellKnownSid(type); !sid.empty())
            groups.push_back(std::move(sid));
    std::vector<SID_AND_ATTRIBUTES> denyOnly;
    for (std::vector<BYTE> &sid : groups)
        denyOnly.push_back({sid.data(), 0});
    HANDLE token = nullptr;
    const BOOL made = CreateRestrictedToken(self, DISABLE_MAX_PRIVILEGE, DWORD(denyOnly.size()), denyOnly.data(), 0,
                                            nullptr, 0, nullptr, &token);
    CloseHandle(self);
    if (!made)
        return nullptr;
    std::vector<BYTE> low = wellKnownSid(WinLowLabelSid);
    TOKEN_MANDATORY_LABEL label{{low.data(), SE_GROUP_INTEGRITY}};
    if (low.empty() || !SetTokenInformation(token, TokenIntegrityLevel, &label, DWORD(sizeof label + low.size()))) {
        CloseHandle(token);
        return nullptr;
    }
    return token;
}

BOOL WINAPI letTheCommandAnswer(DWORD)
{
    return TRUE;
}

}

int wmain()
{
    std::wstring command = commandAfterOurOwn();
    if (command.empty()) {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return refuse("reading the command line");
    }
    const HANDLE token = lowRightsToken();
    if (!token)
        return refuse("making the low-rights token");

    std::vector<HANDLE> handles;
    for (DWORD which : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        const HANDLE h = GetStdHandle(which);
        if (h && h != INVALID_HANDLE_VALUE && SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) &&
            std::find(handles.begin(), handles.end(), h) == handles.end())
            handles.push_back(h);
    }
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<BYTE> attributes(size);
    auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size) ||
        (!handles.empty() && !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                                        handles.size() * sizeof(HANDLE), nullptr, nullptr)))
        return refuse("listing the standard handles");

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.lpAttributeList = list;
    PROCESS_INFORMATION started{};
    if (!CreateProcessAsUserW(token, nullptr, command.data(), nullptr, nullptr, !handles.empty(),
                              EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                              &startup.StartupInfo, &started))
        return refuse("starting the command");
    CloseHandle(token);
    CloseHandle(started.hThread);
    DeleteProcThreadAttributeList(list);
    SetConsoleCtrlHandler(letTheCommandAnswer, TRUE);
    WaitForSingleObject(started.hProcess, INFINITE);
    DWORD code = kRefused;
    GetExitCodeProcess(started.hProcess, &code);
    CloseHandle(started.hProcess);
    return int(code);
}
