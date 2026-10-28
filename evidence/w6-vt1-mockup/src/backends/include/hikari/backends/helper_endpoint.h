#pragma once

// The helper side of a helper process (H1): plain C++ over binary stdin and
// stdout. It announces its protocol version, waits for Welcome or Refuse,
// then runs requests one at a time. A reader thread keeps receiving, so a
// Cancel can reach a running request, including one waiting in call().
//
// The protocol owns the original stdout. Before anything else runs, file
// descriptor 1 (and on Windows the process's standard output handle) is
// pointed at stderr, so output from scripts, native modules or libraries
// lands in the host's diagnostics and never inside a frame.

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

// Runs until stdin closes or the host refuses the version. Returns the
// process exit code: 0 after a normal end, 3 when refused.
int runHelper(const std::string &name, std::uint32_t protocolVersion, const RequestHandler &handler);

// Writes a frame on the protocol channel without any responder checks; only
// for tests that need a misbehaving helper.
void sendUncheckedFrame(const Frame &frame);

} // namespace hikari::backends::helper
