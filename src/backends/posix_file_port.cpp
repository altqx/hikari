#include "hikari/backends/posix_file_port.h"

#include <cerrno>
#include <cstdio>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hikari::backends {

using application::PermitId;
using application::WriteOutcome;

namespace {

std::string directoryOf(const std::string &path)
{
    const auto slash = path.rfind('/');
    if (slash == std::string::npos)
        return ".";
    return slash == 0 ? "/" : path.substr(0, slash);
}

bool writeAll(int fd, const std::byte *data, std::size_t size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

PosixFilePort::PosixFilePort(Completion completion) : PosixFilePort(std::move(completion), Options{}) {}

PosixFilePort::PosixFilePort(Completion completion, Options options)
    : m_completion(std::move(completion)), m_options(std::move(options))
{
}

PosixFilePort::~PosixFilePort()
{
    waitIdle();
}

void PosixFilePort::waitIdle()
{
    // Jobs stay registered while joined, so a cancel request that arrives
    // during the wait still reaches its worker.
    std::vector<Job *> pending;
    {
        std::lock_guard lock(m_mutex);
        for (auto &[id, job] : m_jobs)
            pending.push_back(job.get());
    }
    for (Job *job : pending)
        if (job->worker.joinable())
            job->worker.join();
    std::lock_guard lock(m_mutex);
    std::erase_if(m_jobs, [](const auto &entry) { return !entry.second->worker.joinable(); });
}

void PosixFilePort::startWrite(PermitId permit, const application::DestinationKey &destination,
                               std::vector<std::byte> bytes)
{
    auto job = std::make_unique<Job>();
    Job *raw = job.get();
    {
        std::lock_guard lock(m_mutex);
        m_jobs[permit] = std::move(job);
    }
    raw->worker = std::thread([this, permit, path = destination.value, data = std::move(bytes), raw] {
        const WriteOutcome outcome = writeFile(permit, path, data, raw->cancel);
        if (m_completion)
            m_completion(permit, outcome);
    });
}

void PosixFilePort::requestCancel(PermitId permit)
{
    std::lock_guard lock(m_mutex);
    if (const auto it = m_jobs.find(permit); it != m_jobs.end())
        it->second->cancel = true;
}

WriteOutcome PosixFilePort::writeFile(PermitId permit, const std::string &path, const std::vector<std::byte> &bytes,
                                      const std::atomic<bool> &cancel)
{
    const std::string dir = directoryOf(path);
    const std::string temp = dir + "/." + path.substr(path.rfind('/') + 1) + ".hikari-tmp-" +
                             std::to_string(::getpid()) + "-" + std::to_string(permit.value);

    // Keep an existing destination's permission bits; otherwise honour umask.
    mode_t mode = 0666;
    struct stat existing {};
    if (::stat(path.c_str(), &existing) == 0)
        mode = existing.st_mode & 07777;

    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd < 0)
        return WriteOutcome::Failed;
    if (::stat(path.c_str(), &existing) == 0)
        ::fchmod(fd, existing.st_mode & 07777); // not reduced by umask
    auto abandon = [&](WriteOutcome outcome) {
        ::close(fd);
        ::unlink(temp.c_str());
        return outcome;
    };
    for (std::size_t offset = 0; offset < bytes.size(); offset += m_options.chunkSize) {
        if (cancel)
            return abandon(WriteOutcome::Cancelled);
        const std::size_t n = std::min(m_options.chunkSize, bytes.size() - offset);
        if (!writeAll(fd, bytes.data() + offset, n))
            return abandon(WriteOutcome::Failed);
    }
    if (::fsync(fd) != 0)
        return abandon(WriteOutcome::Failed);
    if (m_options.beforePublish)
        m_options.beforePublish(permit);
    if (cancel)
        return abandon(WriteOutcome::Cancelled);
    if (::close(fd) != 0) {
        ::unlink(temp.c_str());
        return WriteOutcome::Failed;
    }
    if (::rename(temp.c_str(), path.c_str()) != 0) {
        ::unlink(temp.c_str());
        return WriteOutcome::Failed;
    }
    // Published. The rename is durable only once the directory entry is synced.
    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0)
        return WriteOutcome::DurabilityUncertain;
    const bool synced = ::fsync(dfd) == 0;
    ::close(dfd);
    return synced ? WriteOutcome::Written : WriteOutcome::DurabilityUncertain;
}

} // namespace hikari::backends
