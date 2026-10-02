#pragma once

// The helper side of a helper process (H1): plain C++ over binary stdin and
// stdout. It announces its protocol version, waits for Welcome or Refuse,
// then runs requests one at a time. A reader thread keeps receiving, so a
// Cancel can reach a running request.

#include "hikari/backends/helper_protocol.h"

#include <atomic>
#include <functional>
#include <string>

namespace hikari::backends::helper {

class Responder {
public:
    virtual ~Responder() = default;
    virtual void progress(std::vector<std::byte> payload) = 0;
    virtual void reply(std::vector<std::byte> payload) = 0;
    // Ends the request; later calls are ignored.
    virtual void terminal(Outcome outcome, std::vector<std::byte> payload = {}) = 0;
    virtual bool cancelled() const = 0;
};

using RequestHandler = std::function<void(const Frame &request, Responder &responder)>;

// Runs until stdin closes or the host refuses the version. Returns the
// process exit code: 0 after a normal end, 3 when refused.
int runHelper(const std::string &name, std::uint32_t protocolVersion, const RequestHandler &handler);

} // namespace hikari::backends::helper
