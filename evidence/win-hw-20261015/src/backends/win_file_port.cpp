#include "hikari/backends/win_file_port.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>

namespace hikari::backends {

using application::PermitId;
using application::WriteOutcome;

std::wstring windowsPath(const std::string &utf8)
{
    std::wstring wide;
    if (!utf8.empty()) {
        const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                                            nullptr, 0);
        wide.resize(static_cast<std::size_t>(std::max(n, 0)));
        if (n > 0)
            ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                                  wide.data(), n);
    }
    std::ranges::replace(wide, L'/', L'\\');
    const bool drive = wide.size() >= 3 && wide[1] == L':' && wide[2] == L'\\';
    if (drive)
        wide.insert(0, L"\\\\?\\");
    return wide;
}

WinFilePort::WinFilePort(Completion completion) : WinFilePort(std::move(completion), Options{}) {}

WinFilePort::WinFilePort(Completion completion, Options options)
    : m_completion(std::move(completion)), m_options(std::move(options))
{
}

WinFilePort::~WinFilePort()
{
    waitIdle();
}

void WinFilePort::waitIdle()
{
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

void WinFilePort::startWrite(PermitId permit, const application::DestinationKey &destination,
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

void WinFilePort::requestCancel(PermitId permit)
{
    std::lock_guard lock(m_mutex);
    if (const auto it = m_jobs.find(permit); it != m_jobs.end())
        it->second->cancel = true;
}

WriteOutcome WinFilePort::writeFile(PermitId permit, const std::string &path, const std::vector<std::byte> &bytes,
                                    const std::atomic<bool> &cancel)
{
    const std::wstring target = windowsPath(path);
    const auto slash = target.find_last_of(L'\\');
    const std::wstring dir = slash == std::wstring::npos ? L"." : target.substr(0, slash);
    const std::wstring name = slash == std::wstring::npos ? target : target.substr(slash + 1);
    const std::wstring temp = dir + L"\\." + name + L".hikari-tmp-" + std::to_wstring(::GetCurrentProcessId()) +
                              L"-" + std::to_wstring(permit.value);

    HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return WriteOutcome::Failed;
    auto abandon = [&](WriteOutcome outcome) {
        if (file != INVALID_HANDLE_VALUE)
            ::CloseHandle(file);
        ::DeleteFileW(temp.c_str());
        return outcome;
    };
    for (std::size_t offset = 0; offset < bytes.size();) {
        if (cancel)
            return abandon(WriteOutcome::Cancelled);
        const std::size_t want = std::min(m_options.chunkSize, bytes.size() - offset);
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data() + offset, static_cast<DWORD>(want), &written, nullptr) || written == 0)
            return abandon(WriteOutcome::Failed);
        offset += written;
    }
    if (!::FlushFileBuffers(file))
        return abandon(WriteOutcome::Failed);
    if (m_options.beforePublish)
        m_options.beforePublish(permit);
    if (cancel)
        return abandon(WriteOutcome::Cancelled);
    const BOOL closed = ::CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (!closed)
        return abandon(WriteOutcome::Failed);
    // Keep the destination's visible attributes (not read-only: a read-only
    // destination is not silently overwritten; the rename then fails).
    const DWORD attributes = ::GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_READONLY))
        ::SetFileAttributesW(temp.c_str(), attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE |
                                                         FILE_ATTRIBUTE_NOT_CONTENT_INDEXED));
    // Atomic publish. Write-through returns only once the rename is on disk.
    if (!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return abandon(WriteOutcome::Failed); // e.g. a sharing violation: the original stays
    return WriteOutcome::Written;
}

} // namespace hikari::backends
