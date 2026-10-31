#pragma once

#include "hikari/application/write_coordinator.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace hikari::backends {

// Separators become '\' and an absolute drive path gets the "\\?\" prefix, so deep paths are
// not cut at MAX_PATH.
std::wstring windowsPath(const std::string &utf8);

// ReplaceFileW is not used: when its final rename fails without a backup name, the original
// no longer exists.
class WinFilePort : public application::FilePort {
public:
    using Completion = std::function<void(application::PermitId, application::WriteOutcome)>;

    struct Options {
        std::size_t chunkSize = 1 << 20; // cancellation is checked between chunks
        // A virus scanner or the indexer may hold the file open briefly; the replace is retried this long.
        std::chrono::milliseconds replacePatience{3000};
        // Test hook: runs on the worker just before publishing (rename).
        std::function<void(application::PermitId)> beforePublish;
    };

    explicit WinFilePort(Completion completion);
    WinFilePort(Completion completion, Options options);
    ~WinFilePort() override;

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
