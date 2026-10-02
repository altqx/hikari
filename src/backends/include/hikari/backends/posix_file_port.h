#pragma once

// Native write owner for Linux (F2-L; docs/qt/proposals/application-lifecycle.md).
// Implements the application's FilePort with an atomic replace: write a
// temporary file beside the destination, fsync it, rename it over the
// destination, then fsync the directory. The original file is never truncated
// in place, so a failed or cancelled write leaves it untouched.

#include "hikari/application/write_coordinator.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace hikari::backends {

class PosixFilePort : public application::FilePort {
public:
    // Receives each terminal outcome, on the worker thread. The application
    // marshals it to its own thread before calling WriteCoordinator::complete.
    using Completion = std::function<void(application::PermitId, application::WriteOutcome)>;

    struct Options {
        std::size_t chunkSize = 1 << 20; // cancellation is checked between chunks
        // Test hook: runs on the worker just before publishing (rename).
        std::function<void(application::PermitId)> beforePublish;
    };

    explicit PosixFilePort(Completion completion);
    PosixFilePort(Completion completion, Options options);
    ~PosixFilePort() override; // joins outstanding writes

    void startWrite(application::PermitId permit, const application::DestinationKey &destination,
                    std::vector<std::byte> bytes) override;
    void requestCancel(application::PermitId permit) override;

    // Waits for every started write to report.
    void waitIdle();

private:
    struct Job {
        std::atomic<bool> cancel{false};
        std::thread worker;
    };
    application::WriteOutcome writeFile(application::PermitId permit, const std::string &path,
                                        const std::vector<std::byte> &bytes, const std::atomic<bool> &cancel);

    Completion m_completion;
    Options m_options;
    std::mutex m_mutex;
    std::map<application::PermitId, std::unique_ptr<Job>> m_jobs;
};

} // namespace hikari::backends
