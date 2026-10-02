#include "hikari/backends/process_metrics.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <string>
#endif

namespace hikari::backends {

std::int64_t processResidentBytes(std::int64_t pid)
{
    if (pid <= 0)
        return 0;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return 0;
    PROCESS_MEMORY_COUNTERS counters{};
    const BOOL ok = GetProcessMemoryInfo(process, &counters, sizeof counters);
    CloseHandle(process);
    return ok ? static_cast<std::int64_t>(counters.WorkingSetSize) : 0;
#else
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            std::int64_t kb = 0;
            status >> kb;
            return kb * 1024;
        }
        status.ignore(4096, '\n');
    }
    return 0;
#endif
}

} // namespace hikari::backends
