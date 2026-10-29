#pragma once

#include "hikari/backends/helper_protocol.h"

#include <expected>
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
    // A synchronous host service call. Blocks until the host answers; returns
    // Cancelled when the request is cancelled or the host goes away first,
    // or the host's outcome when it is not Ok.
    virtual std::expected<std::vector<std::byte>, Outcome> call(std::vector<std::byte> payload) = 0;
};

using RequestHandler = std::function<void(const Frame &request, Responder &responder)>;

// Sends Hello, waits for Welcome or Refuse, then runs requests one at a time; a Cancel reaches a
// running request. First points fd 1 (and the Windows stdout handle) at stderr, so stray output
// never lands in a frame. Returns 0 once stdin closes, 3 when refused or the host left first.
int runHelper(const std::string &name, std::uint32_t protocolVersion, const RequestHandler &handler);

// For tests only: writes a frame without any responder checks.
void sendUncheckedFrame(const Frame &frame);

} // namespace hikari::backends::helper
