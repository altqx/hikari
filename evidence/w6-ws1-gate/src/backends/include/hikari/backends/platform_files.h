#pragma once

// The native file reader and write owner for the current platform.

#include "hikari/application/document_files.h"

#include <functional>
#include <memory>

namespace hikari::backends {

class PlatformFilePort : public application::FilePort {
public:
    // Waits for every started write to report (shutdown and tests).
    virtual void waitIdle() = 0;
};

using WriteCompletion = std::function<void(application::PermitId, application::WriteOutcome)>;

std::unique_ptr<application::FileReadPort> makeFileReader();
// `completion` runs on a worker thread; the caller marshals it to its own.
std::unique_ptr<PlatformFilePort> makeFilePort(WriteCompletion completion);

} // namespace hikari::backends
