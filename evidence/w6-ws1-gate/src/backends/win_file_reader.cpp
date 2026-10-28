#include "hikari/backends/win_file_reader.h"
#include "hikari/backends/win_file_port.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace hikari::backends {

std::expected<std::vector<std::byte>, application::ReadError>
WinFileReader::read(const application::DestinationKey &destination)
{
    // Share everything: reading must not block other programs or our writer.
    HANDLE file = ::CreateFileW(windowsPath(destination.value).c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD e = ::GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return std::unexpected(application::ReadError::NotFound);
        if (e == ERROR_ACCESS_DENIED)
            return std::unexpected(application::ReadError::AccessDenied);
        return std::unexpected(application::ReadError::Failed);
    }
    std::vector<std::byte> bytes;
    std::byte buffer[1 << 16];
    for (;;) {
        DWORD n = 0;
        if (!::ReadFile(file, buffer, sizeof buffer, &n, nullptr)) {
            ::CloseHandle(file);
            return std::unexpected(application::ReadError::Failed);
        }
        if (n == 0)
            break;
        bytes.insert(bytes.end(), buffer, buffer + n);
    }
    ::CloseHandle(file);
    return bytes;
}

} // namespace hikari::backends
