#include "hikari/backends/posix_file_reader.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace hikari::backends {

std::expected<std::vector<std::byte>, application::ReadError>
PosixFileReader::read(const application::DestinationKey &destination)
{
    const int fd = ::open(destination.value.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return std::unexpected(application::ReadError::NotFound);
        if (errno == EACCES || errno == EPERM)
            return std::unexpected(application::ReadError::AccessDenied);
        return std::unexpected(application::ReadError::Failed);
    }
    std::vector<std::byte> bytes;
    std::byte buffer[1 << 16];
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ::close(fd);
            return std::unexpected(application::ReadError::Failed);
        }
        if (n == 0)
            break;
        bytes.insert(bytes.end(), buffer, buffer + n);
    }
    ::close(fd);
    return bytes;
}

} // namespace hikari::backends
