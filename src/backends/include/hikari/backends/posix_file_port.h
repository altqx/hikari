#pragma once

#include "hikari/application/write_coordinator.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace hikari::backends {

// Atomic replace: a failed or cancelled write leaves the destination untouched.
class PosixFilePort : public application::FilePort {
public:
    // Called with each terminal outcome on the worker thread.
    using Completion = std::function<void(application::PermitId, application::WriteOutcome)>;

    struct Options {
        std::size_t chunkSize = 1 << 20; // cancellation is checked between chunks
        // Test hook: runs on the worker just before the rename.
        std::function<void(application::PermitId)> beforePublish;
    };

    explicit PosixFilePort(Completion completion);
    PosixFilePort(Completion completion, Options options);
    ~PosixFilePort() override; // joins outstanding writes

    void startWrite(application::PermitId permit, const application::DestinationKey &destination,
                    std::vector<std::byte> bytes) override;
    void requestCancel(application::PermitId permit) override;

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

}
